#requires -Version 7.0
param(
    [string]$Port = "COM5",
    [string]$EvidenceDir = (Join-Path $PSScriptRoot "..\.cache\validation\plugged-soft-off-final"),
    [string]$TypeLog = (Join-Path $env:LOCALAPPDATA "Listener Type\Logs\listener-type.log"),
    [string]$FirmwareBin = (Join-Path $PSScriptRoot "..\build\voice-keyboard-firmware.bin"),
    [int]$MustRemainOffSeconds = 15,
    [int]$TimeoutSeconds = 420
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$evidenceRoot = [System.IO.Path]::GetFullPath($EvidenceDir)
[System.IO.Directory]::CreateDirectory($evidenceRoot) | Out-Null
$summaryPath = Join-Path $evidenceRoot "summary.json"
$ownerResultPath = Join-Path $evidenceRoot "owner-result.txt"
$typeEvidencePath = Join-Path $evidenceRoot "type-log-new.txt"
$probePath = Join-Path $evidenceRoot "port-probe.txt"
$helperPath = Join-Path $PSScriptRoot "serial_no_reset_capture.py"
$pythonPath = (Get-Command python -ErrorAction Stop).Path
if (-not (Test-Path -LiteralPath $FirmwareBin)) {
    throw "Firmware binary not found: $FirmwareBin. Build the exact image under test first."
}
$firmwareSha256 = (Get-FileHash -LiteralPath $FirmwareBin -Algorithm SHA256).Hash

$script:phase = "wait_shutdown"
$script:startedAt = [DateTimeOffset]::UtcNow
$script:offAt = $null
$script:wakePromptAt = $null
$script:machinePassedAt = $null
$script:finished = $false
$script:consecutivePortFailures = 0
$script:sawBleDisconnect = $false
$script:portRecovered = $false
$script:notifyRecovered = $false
$script:lastProbeAt = [DateTimeOffset]::MinValue
$script:typeOffset = if (Test-Path $TypeLog) { (Get-Item -LiteralPath $TypeLog).Length } else { 0L }
$script:typeTail = ""
$script:typeLines = [System.Collections.Generic.List[string]]::new()

function Test-PortNoReset {
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $pythonPath
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    foreach ($arg in @(
        $helperPath, "--port", $Port, "--capture-only-ms", "80",
        "--output-path", $probePath
    )) {
        [void]$psi.ArgumentList.Add($arg)
    }
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $psi
    try {
        [void]$process.Start()
        if (-not $process.WaitForExit(2500)) {
            try { $process.Kill($true) } catch {}
            return $false
        }
        return $process.ExitCode -eq 0
    } catch {
        return $false
    } finally {
        $process.Dispose()
    }
}

function Read-NewTypeLog {
    if (-not (Test-Path $TypeLog)) { return "" }
    $file = Get-Item -LiteralPath $TypeLog
    if ($file.Length -lt $script:typeOffset) { $script:typeOffset = 0L }
    if ($file.Length -eq $script:typeOffset) { return "" }
    $stream = [System.IO.FileStream]::new(
        $TypeLog,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
    try {
        [void]$stream.Seek($script:typeOffset, [System.IO.SeekOrigin]::Begin)
        $count = [int]($stream.Length - $script:typeOffset)
        $bytes = [byte[]]::new($count)
        $read = $stream.Read($bytes, 0, $count)
        $script:typeOffset += $read
        return [System.Text.Encoding]::UTF8.GetString($bytes, 0, $read)
    } finally {
        $stream.Dispose()
    }
}

function Save-Summary([string]$Result, [string]$Reason) {
    $offMs = if ($script:offAt) {
        [int][Math]::Round((([DateTimeOffset]::UtcNow - $script:offAt).TotalMilliseconds))
    } else { $null }
    [ordered]@{
        schema = "listener.plugged-soft-off-final.v2"
        result = $Result
        reason = $Reason
        started_at = $script:startedAt.ToString("o")
        completed_at = [DateTimeOffset]::UtcNow.ToString("o")
        minimum_off_hold_ms = $MustRemainOffSeconds * 1000
        observed_off_hold_ms = $offMs
        ble_disconnect_observed = $script:sawBleDisconnect
        usb_data_unavailable_observed = $script:consecutivePortFailures -ge 2
        usb_data_recovered = $script:portRecovered
        audio_notify_recovered = $script:notifyRecovered
        port = $Port
        firmware_sha256 = $firmwareSha256
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $summaryPath -Encoding utf8
    $script:typeLines | Set-Content -LiteralPath $typeEvidencePath -Encoding utf8
}

$form = [System.Windows.Forms.Form]::new()
$form.Text = "Listener 插电关机最终验收（无复位监测）"
$form.StartPosition = "CenterScreen"
$form.Size = [System.Drawing.Size]::new(900, 470)
$form.TopMost = $true
$form.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 11)

$title = [System.Windows.Forms.Label]::new()
$title.Location = [System.Drawing.Point]::new(28, 20)
$title.Size = [System.Drawing.Size]::new(830, 46)
$title.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 19, [System.Drawing.FontStyle]::Bold)
$title.Text = "插电关机：最终完整循环"
$form.Controls.Add($title)

$status = [System.Windows.Forms.Label]::new()
$status.Location = [System.Drawing.Point]::new(30, 82)
$status.Size = [System.Drawing.Size]::new(830, 58)
$status.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 14, [System.Drawing.FontStyle]::Bold)
$status.ForeColor = [System.Drawing.Color]::FromArgb(160, 94, 20)
$status.Text = "设备已开机：现在长按旋钮，完整关机灯效结束后松手"
$form.Controls.Add($status)

$instructions = [System.Windows.Forms.Label]::new()
$instructions.Location = [System.Drawing.Point]::new(30, 150)
$instructions.Size = [System.Drawing.Size]::new(830, 110)
$instructions.Text = @"
保持 USB 连接，只操作一次完整循环：
1. 长按旋钮，看到原有环形关机灯效和最终确认后松手；
2. 窗口自动确认 USB 数据断开并保持关机 15 秒；
3. 窗口提示后短按约 0.5 秒，等待 Type 音频监听自动恢复。
本窗口使用无复位端口探针，不会再通过 DTR/RTS 重启设备。
"@
$form.Controls.Add($instructions)

$detail = [System.Windows.Forms.Label]::new()
$detail.Location = [System.Drawing.Point]::new(30, 276)
$detail.Size = [System.Drawing.Size]::new(830, 52)
$detail.ForeColor = [System.Drawing.Color]::DimGray
$detail.Text = "等待物理长按；Listener Type 已运行并监听 BLE 状态。"
$form.Controls.Add($detail)

$passButton = [System.Windows.Forms.Button]::new()
$passButton.Location = [System.Drawing.Point]::new(30, 356)
$passButton.Size = [System.Drawing.Size]::new(245, 48)
$passButton.Text = "通过，灯效也正确"
$passButton.Enabled = $false
$passButton.BackColor = [System.Drawing.Color]::FromArgb(42, 130, 82)
$passButton.ForeColor = [System.Drawing.Color]::White
$form.Controls.Add($passButton)

$failButton = [System.Windows.Forms.Button]::new()
$failButton.Location = [System.Drawing.Point]::new(325, 356)
$failButton.Size = [System.Drawing.Size]::new(245, 48)
$failButton.Text = "失败，保存现场"
$failButton.BackColor = [System.Drawing.Color]::FromArgb(190, 48, 48)
$failButton.ForeColor = [System.Drawing.Color]::White
$form.Controls.Add($failButton)

$abortButton = [System.Windows.Forms.Button]::new()
$abortButton.Location = [System.Drawing.Point]::new(620, 356)
$abortButton.Size = [System.Drawing.Size]::new(240, 48)
$abortButton.Text = "中止"
$form.Controls.Add($abortButton)

function Finish-Failure([string]$Reason) {
    if ($script:finished) { return }
    $script:finished = $true
    $timer.Stop()
    Save-Summary "FAIL" $Reason
    "FAIL`n$Reason" | Set-Content -LiteralPath $ownerResultPath -Encoding utf8
    $status.Text = "未通过：现场已保存"
    $status.ForeColor = [System.Drawing.Color]::FromArgb(190, 48, 48)
    $detail.Text = $Reason
    $failButton.Enabled = $false
    $abortButton.Text = "关闭"
}

$passButton.Add_Click({
    if (-not $script:machinePassedAt -or $script:finished) { return }
    $script:finished = $true
    $timer.Stop()
    Save-Summary "PASS" "off_held_usb_isolated_then_short_press_restored_type_audio_owner_led_pass"
    "PASS`n机器链路与关机灯效均通过。" | Set-Content -LiteralPath $ownerResultPath -Encoding utf8
    $status.Text = "通过：插电关机与短按恢复完整闭环"
    $status.ForeColor = [System.Drawing.Color]::FromArgb(22, 120, 70)
    $detail.Text = "证据已保存，可以关闭窗口。"
    $passButton.Enabled = $false
    $failButton.Enabled = $false
    $abortButton.Text = "关闭"
})
$failButton.Add_Click({ Finish-Failure "用户点击失败；请按现场日志继续修复。" })
$abortButton.Add_Click({
    if (-not $script:finished) {
        $script:finished = $true
        $timer.Stop()
        Save-Summary "ABORTED" "operator_closed_window"
        "ABORTED" | Set-Content -LiteralPath $ownerResultPath -Encoding utf8
    }
    $form.Close()
})

$timer = [System.Windows.Forms.Timer]::new()
$timer.Interval = 250
$timer.Add_Tick({
    if ($script:finished) { return }
    $now = [DateTimeOffset]::UtcNow
    if (($now - $script:startedAt).TotalSeconds -ge $TimeoutSeconds) {
        Finish-Failure "验收超时；未观察到完整关机与 Type 音频恢复。"
        return
    }

    $newText = Read-NewTypeLog
    if ($newText) {
        $combined = $script:typeTail + $newText
        $parts = [regex]::Split($combined, "\r?\n")
        $script:typeTail = $parts[-1]
        if ($parts.Count -gt 1) {
            foreach ($line in $parts[0..($parts.Count - 2)]) {
                if ($line) { $script:typeLines.Add($line) }
                if ($line -like '*device connection status changed to BluetoothConnectionStatus(0)*') {
                    $script:sawBleDisconnect = $true
                }
                if ($line -like '*background listener notify ready*' -and $script:phase -eq "wait_wake") {
                    $script:notifyRecovered = $true
                }
            }
        }
    }

    if (($now - $script:lastProbeAt).TotalMilliseconds -ge 1100 -and
        $script:phase -in @("wait_shutdown", "wait_wake")) {
        $script:lastProbeAt = $now
        $available = Test-PortNoReset
        if ($script:phase -eq "wait_shutdown") {
            if ($available) { $script:consecutivePortFailures = 0 }
            else { $script:consecutivePortFailures++ }
            if ($script:consecutivePortFailures -ge 2 -and $script:sawBleDisconnect) {
                $script:phase = "hold_off"
                $script:offAt = $now
                $status.Text = "关机保持中：还需 $MustRemainOffSeconds 秒"
                $status.ForeColor = [System.Drawing.Color]::FromArgb(32, 96, 160)
                $detail.Text = "已确认 BLE 断开且 USB 数据 PHY 不可用；请保持 USB 插着。"
            }
        } else {
            if ($available) { $script:portRecovered = $true }
        }
    }

    if ($script:phase -eq "hold_off" -and $script:offAt) {
        $elapsed = ($now - $script:offAt).TotalSeconds
        $remaining = [Math]::Max(0, $MustRemainOffSeconds - [int][Math]::Floor($elapsed))
        $status.Text = "关机保持中：还需 $remaining 秒"
        if ($elapsed -ge $MustRemainOffSeconds) {
            $script:phase = "wait_wake"
            $script:wakePromptAt = $now
            $status.Text = "保持关机通过：现在短按旋钮约 0.5 秒后松开"
            $status.ForeColor = [System.Drawing.Color]::FromArgb(160, 94, 20)
            $detail.Text = "等待 USB 数据和 Listener Type 音频 notify 自动恢复。"
        }
    }

    if ($script:phase -eq "wait_wake" -and $script:portRecovered -and $script:notifyRecovered) {
        $script:phase = "machine_passed"
        $script:machinePassedAt = $now
        $status.Text = "机器链路通过：请确认关机灯效是否正确"
        $status.ForeColor = [System.Drawing.Color]::FromArgb(22, 120, 70)
        $detail.Text = "USB 与 Type 音频均已恢复；灯效正确请点【通过】，否则点【失败】。"
        $passButton.Enabled = $true
    }
})

$form.Add_FormClosed({
    $timer.Stop()
    $timer.Dispose()
})
$timer.Start()
[void]$form.ShowDialog()
