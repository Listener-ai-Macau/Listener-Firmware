[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Port = "",
    [int]$Baud = 115200,
    [int]$BatteryIdleSeconds = 75,
    [int]$PortDisappearTimeoutSeconds = 30,
    [int]$PortReappearTimeoutSeconds = 120,
    [int]$PostReplugSettleSeconds = 8,
    [string]$OutputDir = "",
    [switch]$NoPrompt,
    [switch]$PlanOnly
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

$AgentName = "codex"
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$aiwPath = "C:\Users\Billy\Desktop\Denzic\ai-collaboration-workflow\scripts\aiw.ps1"
$serialCaptureScript = Join-Path $PSScriptRoot "send_serial_and_capture.ps1"

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repoRoot ".cache\validation\idle-power-measurement-guided-$stamp"
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir = Join-Path $repoRoot $OutputDir
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path

$transcriptPath = Join-Path $OutputDir "guided-transcript.txt"
$summaryJsonPath = Join-Path $OutputDir "guided-summary.json"
$summaryMdPath = Join-Path $OutputDir "guided-summary.md"
Set-Content -LiteralPath $transcriptPath -Value @() -Encoding UTF8

$script:Records = [System.Collections.Generic.List[object]]::new()
$script:Errors = [System.Collections.Generic.List[string]]::new()

function Add-Transcript {
    param([Parameter(Mandatory = $true)][string]$Message)

    $line = "[{0:o}] {1}" -f (Get-Date), $Message
    Write-Host $line
    Add-Content -LiteralPath $transcriptPath -Value $line -Encoding UTF8
}

function Add-ErrorLine {
    param([Parameter(Mandatory = $true)][string]$Message)

    $script:Errors.Add($Message) | Out-Null
    Add-Transcript "ERROR: $Message"
}

function Play-PromptSound {
    try {
        [System.Media.SystemSounds]::Exclamation.Play()
        Start-Sleep -Milliseconds 180
        [System.Media.SystemSounds]::Asterisk.Play()
    } catch {
        try {
            [Console]::Beep(880, 140)
            [Console]::Beep(1175, 180)
        } catch {
            # Sound is best-effort; prompts must still work on muted/headless systems.
        }
    }
}

function Show-TopMostMessageBox {
    param(
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][string]$Message,
        [System.Windows.Forms.MessageBoxIcon]$Icon = [System.Windows.Forms.MessageBoxIcon]::Information
    )

    Add-Transcript ("operator_prompt title=[{0}] {1} message={2}" -f $AgentName, $Title, ($Message -replace "`r?`n", " / "))
    if ($NoPrompt.IsPresent) {
        return [System.Windows.Forms.DialogResult]::OK
    }

    Play-PromptSound
    $owner = [System.Windows.Forms.Form]::new()
    try {
        $owner.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen
        $owner.TopMost = $true
        $owner.ShowInTaskbar = $false
        $owner.WindowState = [System.Windows.Forms.FormWindowState]::Minimized
        $owner.Show()
        $owner.Hide()
        return [System.Windows.Forms.MessageBox]::Show($owner, $Message, "[$AgentName] $Title", [System.Windows.Forms.MessageBoxButtons]::OK, $Icon)
    } finally {
        $owner.Dispose()
    }
}

function Show-MeasurementForm {
    param(
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][string]$Instruction,
        [string]$DefaultCurrent = ""
    )

    Add-Transcript ("measurement_prompt title=[{0}] {1}" -f $AgentName, $Title)
    if ($NoPrompt.IsPresent) {
        return [PSCustomObject]@{
            result = "SKIP"
            current_ma = $DefaultCurrent
            observation = ""
        }
    }

    Play-PromptSound
    $form = [System.Windows.Forms.Form]::new()
    $form.Text = "[$AgentName] $Title"
    $form.Size = [System.Drawing.Size]::new(640, 420)
    $form.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen
    $form.TopMost = $true
    $form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedDialog
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false

    $label = [System.Windows.Forms.Label]::new()
    $label.Text = $Instruction
    $label.Location = [System.Drawing.Point]::new(16, 16)
    $label.Size = [System.Drawing.Size]::new(590, 115)
    $label.AutoSize = $false
    $form.Controls.Add($label)

    $currentLabel = [System.Windows.Forms.Label]::new()
    $currentLabel.Text = "电流读数 mA（例如 9-20 或 47）："
    $currentLabel.Location = [System.Drawing.Point]::new(16, 146)
    $currentLabel.Size = [System.Drawing.Size]::new(250, 24)
    $form.Controls.Add($currentLabel)

    $currentText = [System.Windows.Forms.TextBox]::new()
    $currentText.Text = $DefaultCurrent
    $currentText.Location = [System.Drawing.Point]::new(280, 144)
    $currentText.Size = [System.Drawing.Size]::new(320, 24)
    $form.Controls.Add($currentText)

    $notesLabel = [System.Windows.Forms.Label]::new()
    $notesLabel.Text = "观察备注（是否稳定/波动、灯是否正常、是否听到声音）："
    $notesLabel.Location = [System.Drawing.Point]::new(16, 184)
    $notesLabel.Size = [System.Drawing.Size]::new(520, 24)
    $form.Controls.Add($notesLabel)

    $notesText = [System.Windows.Forms.TextBox]::new()
    $notesText.Multiline = $true
    $notesText.ScrollBars = [System.Windows.Forms.ScrollBars]::Vertical
    $notesText.Location = [System.Drawing.Point]::new(16, 212)
    $notesText.Size = [System.Drawing.Size]::new(584, 94)
    $form.Controls.Add($notesText)

    $result = "SKIP"
    $passButton = [System.Windows.Forms.Button]::new()
    $passButton.Text = "通过"
    $passButton.Location = [System.Drawing.Point]::new(306, 326)
    $passButton.Size = [System.Drawing.Size]::new(90, 32)
    $passButton.Add_Click({
        $script:MeasurementResult = "PASS"
        $form.DialogResult = [System.Windows.Forms.DialogResult]::OK
        $form.Close()
    })
    $form.Controls.Add($passButton)

    $failButton = [System.Windows.Forms.Button]::new()
    $failButton.Text = "失败"
    $failButton.Location = [System.Drawing.Point]::new(408, 326)
    $failButton.Size = [System.Drawing.Size]::new(90, 32)
    $failButton.Add_Click({
        $script:MeasurementResult = "FAIL"
        $form.DialogResult = [System.Windows.Forms.DialogResult]::OK
        $form.Close()
    })
    $form.Controls.Add($failButton)

    $skipButton = [System.Windows.Forms.Button]::new()
    $skipButton.Text = "跳过"
    $skipButton.Location = [System.Drawing.Point]::new(510, 326)
    $skipButton.Size = [System.Drawing.Size]::new(90, 32)
    $skipButton.Add_Click({
        $script:MeasurementResult = "SKIP"
        $form.DialogResult = [System.Windows.Forms.DialogResult]::OK
        $form.Close()
    })
    $form.Controls.Add($skipButton)

    $script:MeasurementResult = "SKIP"
    [void]$form.ShowDialog()
    $result = $script:MeasurementResult
    $currentValue = $currentText.Text.Trim()
    $observationValue = $notesText.Text.Trim()
    $form.Dispose()

    return [PSCustomObject]@{
        result = $result
        current_ma = $currentValue
        observation = $observationValue
    }
}

function Get-SerialPorts {
    try {
        return @([System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object)
    } catch {
        return @()
    }
}

function Format-Ports {
    param([string[]]$Ports)

    if ($Ports.Count -eq 0) {
        return "<none>"
    }
    return ($Ports -join ",")
}

function Resolve-TestPort {
    if (-not [string]::IsNullOrWhiteSpace($Port)) {
        return $Port
    }

    $ports = @(Get-SerialPorts)
    if ($ports.Count -ne 1) {
        throw "Expected exactly one serial port when -Port is omitted; found $(Format-Ports $ports)."
    }
    return $ports[0]
}

function Wait-PortAbsent {
    param(
        [Parameter(Mandatory = $true)][string]$SerialPortName,
        [Parameter(Mandatory = $true)][int]$TimeoutSeconds
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $ports = @(Get-SerialPorts)
        Add-Transcript ("port_poll expect=absent target={0} ports={1}" -f $SerialPortName, (Format-Ports $ports))
        if ($ports -notcontains $SerialPortName) {
            return $true
        }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Wait-PortPresent {
    param(
        [Parameter(Mandatory = $true)][string]$SerialPortName,
        [Parameter(Mandatory = $true)][int]$TimeoutSeconds
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $ports = @(Get-SerialPorts)
        Add-Transcript ("port_poll expect=present target={0} ports={1}" -f $SerialPortName, (Format-Ports $ports))
        if ($ports -contains $SerialPortName) {
            return $SerialPortName
        }
        if ($ports.Count -eq 1) {
            return $ports[0]
        }
        Start-Sleep -Milliseconds 500
    }
    return ""
}

function Invoke-Capture {
    param(
        [Parameter(Mandatory = $true)][string]$SerialPortName,
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string[]]$Commands,
        [int]$InitialReadMs = 800,
        [int]$CommandReadMs = 1400
    )

    $path = Join-Path $OutputDir ("serial-{0}.txt" -f $Label)
    $commandList = ($Commands -join ";;")
    $args = @(
        "with-lock",
        "-Resource", $SerialPortName,
        "-Wait",
        "-WaitTimeoutSeconds", "120",
        "-TimeoutMinutes", "2",
        "-Purpose", "oai2 guided idle power measurement $Label",
        "-Run",
        "pwsh",
        "-NoProfile",
        "-File", $serialCaptureScript,
        "-Port", $SerialPortName,
        "-Baud", "$Baud",
        "-InitialReadMs", "$InitialReadMs",
        "-CommandReadMs", "$CommandReadMs",
        "-CommandList", $commandList,
        "-OutputPath", $path
    )

    Add-Transcript ("capture_start label={0} port={1} path={2}" -f $Label, $SerialPortName, $path)
    $output = & $aiwPath @args 2>&1
    $exit = $LASTEXITCODE
    foreach ($line in $output) {
        Add-Transcript ("capture_output label={0} {1}" -f $Label, $line)
    }
    if ($exit -ne 0) {
        Add-ErrorLine ("capture failed label={0} exit={1}" -f $Label, $exit)
    }
    return $path
}

function Get-LatestLine {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Prefix
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return ""
    }
    $lines = @(Get-Content -LiteralPath $Path | Where-Object { $_ -like "$Prefix*" })
    if ($lines.Count -eq 0) {
        return ""
    }
    return $lines[-1]
}

function Convert-KeyValueLine {
    param([string]$Line)

    $map = [ordered]@{}
    if ([string]::IsNullOrWhiteSpace($Line)) {
        return $map
    }
    foreach ($part in ($Line -split "\s+")) {
        if ($part -match "^([^=\s]+)=(.*)$") {
            $map[$matches[1]] = $matches[2].Trim('"')
        }
    }
    return $map
}

function Add-Record {
    param([Parameter(Mandatory = $true)][object]$Record)

    $script:Records.Add($Record) | Out-Null
}

function Write-Summary {
    param(
        [Parameter(Mandatory = $true)][string]$Result,
        [string]$InitialPort = "",
        [string]$PostPort = ""
    )

    $summary = [ordered]@{
        schema = "listener.idle_power_measurement_guided.v1"
        generated_at = (Get-Date).ToString("o")
        agent = $AgentName
        result = $Result
        initial_port = $InitialPort
        post_port = $PostPort
        battery_idle_seconds = $BatteryIdleSeconds
        output_dir = $OutputDir
        transcript = $transcriptPath
        records = @($script:Records)
        errors = @($script:Errors)
    }
    ($summary | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $summaryJsonPath -Encoding UTF8

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("# Guided idle power measurement") | Out-Null
    $lines.Add("") | Out-Null
    $lines.Add(("- Result: {0}" -f $Result)) | Out-Null
    $lines.Add(("- Agent: {0}" -f $AgentName)) | Out-Null
    $lines.Add(("- Initial port: {0}" -f $InitialPort)) | Out-Null
    $lines.Add(("- Post port: {0}" -f $PostPort)) | Out-Null
    $lines.Add(("- Battery idle wait: {0}s" -f $BatteryIdleSeconds)) | Out-Null
    $lines.Add(("- Transcript: {0}" -f $transcriptPath)) | Out-Null
    $lines.Add("") | Out-Null
    $lines.Add("## Records") | Out-Null
    foreach ($record in $script:Records) {
        $lines.Add(("- {0}: {1}" -f $record.phase, ($record | ConvertTo-Json -Compress -Depth 5))) | Out-Null
    }
    if ($script:Errors.Count -gt 0) {
        $lines.Add("") | Out-Null
        $lines.Add("## Errors") | Out-Null
        foreach ($errorLine in $script:Errors) {
            $lines.Add(("- {0}" -f $errorLine)) | Out-Null
        }
    }
    $lines | Set-Content -LiteralPath $summaryMdPath -Encoding UTF8
}

Add-Transcript ("start agent={0} port={1} output_dir={2} battery_idle_seconds={3} plan_only={4}" -f $AgentName, $Port, $OutputDir, $BatteryIdleSeconds, $PlanOnly.IsPresent)
if ($PlanOnly.IsPresent) {
    Add-Record ([PSCustomObject]@{
        phase = "plan"
        steps = @(
            "capture plugged POWER:STATUS/IDLE/PM",
            "prompt operator to unplug USB-C",
            "wait for COM port disappearance",
            "wait battery idle interval",
            "prompt operator to record DMM current",
            "prompt operator to replug USB-C",
            "wait for USB boot and CDC settle",
            "wait for COM port return and capture POWER:STATUS/IDLE/PM/LED"
        )
    })
    Write-Summary -Result "PLAN_ONLY"
    Write-Host ("result=PLAN_ONLY`nsummary={0}`ntranscript={1}" -f $summaryMdPath, $transcriptPath)
    exit 0
}

$initialPort = Resolve-TestPort
$postPort = ""

Show-TopMostMessageBox `
    -Title "低功耗测量：准备插电基线" `
    -Message "请确认 Listener 已通过 USB-C 连接电脑，当前串口是 $initialPort。不要按按键，不要录音。点击确定后我会短暂读取 POWER/PM/LED 状态。"

$preCapture = Invoke-Capture -SerialPortName $initialPort -Label "01-plugged-baseline" -Commands @(
    "~POWER:STATUS",
    "~POWER:IDLE",
    "~POWER:PM",
    "~LED:STATUS"
) -InitialReadMs 800 -CommandReadMs 1500
$preIdleLine = Get-LatestLine -Path $preCapture -Prefix "~POWER:IDLE_DIAG"
$preIdle = Convert-KeyValueLine -Line $preIdleLine
Add-Record ([PSCustomObject]@{
    phase = "plugged_baseline"
    artifact = $preCapture
    idle_diag = $preIdle
})

$pluggedObservation = Show-MeasurementForm `
    -Title "插电基线读数" `
    -Instruction "如果你现在已经接着电流表，可以记录插电状态下的电流。这个读数只做对照，不决定低功耗是否通过。填好后点[通过]；不测就点[跳过]。" `
    -DefaultCurrent ""
Add-Record ([PSCustomObject]@{
    phase = "plugged_observation"
    result = $pluggedObservation.result
    current_ma = $pluggedObservation.current_ma
    observation = $pluggedObservation.observation
})

Show-TopMostMessageBox `
    -Title "低功耗测量：现在拔 USB-C" `
    -Message "现在请拔掉 Listener 的 USB-C 线。拔掉以后不要按任何按键，也不要重新插线。拔掉后点击确定；脚本会等待 Windows 里的 $initialPort 消失。"

$portDisappeared = Wait-PortAbsent -SerialPortName $initialPort -TimeoutSeconds $PortDisappearTimeoutSeconds
Add-Record ([PSCustomObject]@{
    phase = "unplug"
    port_disappeared = $portDisappeared
})
if (-not $portDisappeared) {
    Add-ErrorLine ("serial port did not disappear after unplug: port={0} timeout_s={1}" -f $initialPort, $PortDisappearTimeoutSeconds)
}

if ($portDisappeared) {
    Show-TopMostMessageBox `
        -Title "低功耗测量：等待离电 idle" `
        -Message "已检测到 $initialPort 消失。点击确定后开始等待 $BatteryIdleSeconds 秒。等待期间不要按键、不要移动设备、不要插回 USB；请观察电流是否从高电流降到低功耗波动区间。"
    Add-Transcript ("battery_idle_wait_start seconds={0}" -f $BatteryIdleSeconds)
    Start-Sleep -Seconds $BatteryIdleSeconds
    Add-Transcript ("battery_idle_wait_complete seconds={0}" -f $BatteryIdleSeconds)

    $batteryObservation = Show-MeasurementForm `
        -Title "离电 idle 电流读数" `
        -Instruction "现在读取电流表。预期如果已经进入 light sleep/低功耗 idle，应明显低于 40-50mA，可能在 10-20mA 区间波动。请填电流读数，并说明是否稳定/波动、PWR/BLE 灯是否符合预期。" `
        -DefaultCurrent ""
    Add-Record ([PSCustomObject]@{
        phase = "battery_idle_observation"
        result = $batteryObservation.result
        current_ma = $batteryObservation.current_ma
        observation = $batteryObservation.observation
    })
    if ($batteryObservation.result -eq "FAIL") {
        Add-ErrorLine ("operator marked battery idle current as FAIL current_ma={0} observation={1}" -f $batteryObservation.current_ma, $batteryObservation.observation)
    }

    Show-TopMostMessageBox `
        -Title "低功耗测量：现在插回 USB-C" `
        -Message "现在请插回 Listener 的 USB-C 线。插好后点击确定；脚本会等待串口恢复，再等 $PostReplugSettleSeconds 秒让 USB/固件启动稳定，然后抓取 POWER:STATUS、POWER:IDLE、PM lock 和 LED 状态。"

    $postPort = Wait-PortPresent -SerialPortName $initialPort -TimeoutSeconds $PortReappearTimeoutSeconds
    Add-Record ([PSCustomObject]@{
        phase = "replug"
        target_port = $initialPort
        post_port = $postPort
        port_reappeared = -not [string]::IsNullOrWhiteSpace($postPort)
    })
    if ([string]::IsNullOrWhiteSpace($postPort)) {
        Add-ErrorLine ("serial port did not reappear after replug: target={0} timeout_s={1}" -f $initialPort, $PortReappearTimeoutSeconds)
    } else {
        Add-Transcript ("post_replug_settle_start seconds={0}" -f $PostReplugSettleSeconds)
        Start-Sleep -Seconds $PostReplugSettleSeconds
        Add-Transcript ("post_replug_settle_complete seconds={0}" -f $PostReplugSettleSeconds)
        $postCapture = Invoke-Capture -SerialPortName $postPort -Label "02-post-replug" -Commands @(
            "~POWER:STATUS",
            "~POWER:IDLE",
            "~POWER:PM",
            "~LED:STATUS",
            "~DIAGLOG:LAST:80:power"
        ) -InitialReadMs 6000 -CommandReadMs 1800
        $postIdleLine = Get-LatestLine -Path $postCapture -Prefix "~POWER:IDLE_DIAG"
        $postIdle = Convert-KeyValueLine -Line $postIdleLine
        Add-Record ([PSCustomObject]@{
            phase = "post_replug_capture"
            artifact = $postCapture
            idle_diag = $postIdle
        })
    }
}

$result = if ($script:Errors.Count -gt 0) { "FAIL" } else { "PASS" }
Write-Summary -Result $result -InitialPort $initialPort -PostPort $postPort
Add-Transcript ("summary={0}" -f $summaryJsonPath)
Add-Transcript ("summary_md={0}" -f $summaryMdPath)
Write-Host ("result={0}`nsummary={1}`ntranscript={2}" -f $result, $summaryMdPath, $transcriptPath)

if ($result -ne "PASS") {
    exit 1
}
