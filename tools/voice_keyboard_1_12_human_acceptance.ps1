param(
    [string]$OutputDir = "",
    [string]$TypeRepo = "C:\Users\Billy\Desktop\Denzic\Listener\Listener-Type",
    [ValidateSet("Full", "NoTranscript")]
    [string]$Scenario = "Full",
    [switch]$OpenNotepad
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repoRoot "docs\validation\voice-keyboard-firmware-full-function-test-1.12\human-go-no-go-$stamp"
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$sessionPath = Join-Path $OutputDir "human-acceptance-session.jsonl"
$summaryJsonPath = Join-Path $OutputDir "human-acceptance-summary.json"
$summaryMdPath = Join-Path $OutputDir "human-acceptance-summary.md"
$runtimeEvidenceDir = Join-Path $OutputDir "runtime-evidence"

function Get-GitSnapshot {
    param([Parameter(Mandatory = $true)][string]$Path)
    try {
        [pscustomobject]@{
            path = (Resolve-Path $Path).Path
            commit = (& git -C $Path rev-parse HEAD).Trim()
            branch = (& git -C $Path branch --show-current).Trim()
            status = ((& git -C $Path status --short --branch) -join "`n")
        }
    } catch {
        [pscustomobject]@{
            path = $Path
            commit = ""
            branch = ""
            status = "unavailable: $($_.Exception.Message)"
        }
    }
}

function Add-JsonLine {
    param([Parameter(Mandatory = $true)][object]$Object)
    ($Object | ConvertTo-Json -Depth 8 -Compress) | Add-Content -LiteralPath $sessionPath -Encoding UTF8
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

function Show-TopMostMessageBox {
    param(
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][string]$Message,
        [System.Windows.Forms.MessageBoxButtons]$Buttons = [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]$Icon = [System.Windows.Forms.MessageBoxIcon]::Information
    )

    $owner = [System.Windows.Forms.Form]::new()
    try {
        $owner.TopMost = $true
        $owner.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen
        $owner.Size = [System.Drawing.Size]::new(1, 1)
        $owner.ShowInTaskbar = $false
        $owner.WindowState = [System.Windows.Forms.FormWindowState]::Minimized
        $owner.Show()
        return [System.Windows.Forms.MessageBox]::Show($owner, $Message, $Title, $Buttons, $Icon)
    } finally {
        $owner.Close()
        $owner.Dispose()
    }
}

function Show-StepPrompt {
    param(
        [Parameter(Mandatory = $true)][object]$Step,
        [Parameter(Mandatory = $true)][int]$Index,
        [Parameter(Mandatory = $true)][int]$Total
    )

    $form = [System.Windows.Forms.Form]::new()
    $form.Text = "Listener 1.12 人工验收 ($Index/$Total)"
    $form.Size = [System.Drawing.Size]::new(860, 650)
    $form.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen
    $form.TopMost = $true
    $form.MinimizeBox = $false
    $form.MaximizeBox = $false

    $title = [System.Windows.Forms.Label]::new()
    $title.Text = "$Index/$Total  $($Step.title)"
    $title.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 13, [System.Drawing.FontStyle]::Bold)
    $title.AutoSize = $false
    $title.Location = [System.Drawing.Point]::new(18, 16)
    $title.Size = [System.Drawing.Size]::new(805, 34)
    $form.Controls.Add($title)

    $instructions = [System.Windows.Forms.TextBox]::new()
    $instructions.Multiline = $true
    $instructions.ReadOnly = $true
    $instructions.ScrollBars = [System.Windows.Forms.ScrollBars]::Vertical
    $instructions.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 10)
    $instructions.Location = [System.Drawing.Point]::new(18, 58)
    $instructions.Size = [System.Drawing.Size]::new(805, 300)
    $instructions.Text = $Step.instructions
    $form.Controls.Add($instructions)

    $notesLabel = [System.Windows.Forms.Label]::new()
    $notesLabel.Text = "备注：如果 FAIL 或 SKIP，请写清楚看到的问题；PASS 可留空。"
    $notesLabel.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 9)
    $notesLabel.AutoSize = $false
    $notesLabel.Location = [System.Drawing.Point]::new(18, 372)
    $notesLabel.Size = [System.Drawing.Size]::new(805, 24)
    $form.Controls.Add($notesLabel)

    $notes = [System.Windows.Forms.TextBox]::new()
    $notes.Multiline = $true
    $notes.ScrollBars = [System.Windows.Forms.ScrollBars]::Vertical
    $notes.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 10)
    $notes.Location = [System.Drawing.Point]::new(18, 400)
    $notes.Size = [System.Drawing.Size]::new(805, 108)
    $form.Controls.Add($notes)

    $buttonY = 528
    $passButton = [System.Windows.Forms.Button]::new()
    $passButton.Text = $Step.pass_text
    $passButton.Location = [System.Drawing.Point]::new(18, $buttonY)
    $passButton.Size = [System.Drawing.Size]::new(180, 42)
    $passButton.BackColor = [System.Drawing.Color]::FromArgb(219, 244, 226)
    $passButton.Add_Click({
        $form.Tag = [pscustomobject]@{ result = "PASS"; notes = $notes.Text }
        $form.Close()
    })
    $form.Controls.Add($passButton)

    $failButton = [System.Windows.Forms.Button]::new()
    $failButton.Text = "FAIL"
    $failButton.Location = [System.Drawing.Point]::new(218, $buttonY)
    $failButton.Size = [System.Drawing.Size]::new(180, 42)
    $failButton.BackColor = [System.Drawing.Color]::FromArgb(255, 225, 225)
    $failButton.Add_Click({
        $form.Tag = [pscustomobject]@{ result = "FAIL"; notes = $notes.Text }
        $form.Close()
    })
    $form.Controls.Add($failButton)

    $skipButton = [System.Windows.Forms.Button]::new()
    $skipButton.Text = "SKIP/稍后"
    $skipButton.Location = [System.Drawing.Point]::new(418, $buttonY)
    $skipButton.Size = [System.Drawing.Size]::new(180, 42)
    $skipButton.BackColor = [System.Drawing.Color]::FromArgb(238, 238, 238)
    $skipButton.Add_Click({
        $form.Tag = [pscustomobject]@{ result = "SKIP"; notes = $notes.Text }
        $form.Close()
    })
    $form.Controls.Add($skipButton)

    $abortButton = [System.Windows.Forms.Button]::new()
    $abortButton.Text = "终止验收"
    $abortButton.Location = [System.Drawing.Point]::new(643, $buttonY)
    $abortButton.Size = [System.Drawing.Size]::new(180, 42)
    $abortButton.Add_Click({
        $form.Tag = [pscustomobject]@{ result = "ABORT"; notes = $notes.Text }
        $form.Close()
    })
    $form.Controls.Add($abortButton)

    $form.AcceptButton = $passButton
    [void]$form.ShowDialog()

    if ($null -eq $form.Tag) {
        return [pscustomobject]@{ result = "ABORT"; notes = "window closed" }
    }
    return $form.Tag
}

function Convert-TableText {
    param([string]$Text)
    if ($null -eq $Text) { return "" }
    return (($Text -replace "\|", "\|") -replace "`r?`n", "<br>")
}

function ConvertTo-SafeFileName {
    param([Parameter(Mandatory = $true)][string]$Value)
    $safe = ($Value -replace '[^A-Za-z0-9_.-]+', '-').Trim('-')
    if ([string]::IsNullOrWhiteSpace($safe)) { return "evidence" }
    return $safe
}

function Get-ListenerTypeLogPaths {
    $paths = [System.Collections.Generic.List[string]]::new()
    $bases = @($env:LOCALAPPDATA, $env:APPDATA) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    foreach ($base in $bases) {
        foreach ($relative in @(
            "Listener Type\Logs\listener-type.log",
            "Listener Type\logs\listener-type.log",
            "listener-type\Logs\listener-type.log",
            "listener-type\logs\listener-type.log"
        )) {
            $candidate = Join-Path $base $relative
            if ((Test-Path -LiteralPath $candidate -PathType Leaf) -and -not $paths.Contains($candidate)) {
                $paths.Add($candidate) | Out-Null
            }
        }
    }
    return @($paths)
}

function Save-ListenerTypeEvidence {
    param([Parameter(Mandatory = $true)][string]$Label)

    New-Item -ItemType Directory -Force -Path $runtimeEvidenceDir | Out-Null
    $safeLabel = ConvertTo-SafeFileName -Value $Label
    $processPath = Join-Path $runtimeEvidenceDir "$safeLabel-listener-type-processes.json"
    $logRecords = [System.Collections.Generic.List[object]]::new()

    $processes = @(Get-Process listener-type -ErrorAction SilentlyContinue | ForEach-Object {
        $path = ""
        try { $path = $_.Path } catch { $path = "" }
        [pscustomobject]@{
            id = $_.Id
            path = $path
            start_time = $_.StartTime
            main_window_title = $_.MainWindowTitle
        }
    })
    $processes | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $processPath -Encoding UTF8

    $index = 0
    foreach ($logPath in Get-ListenerTypeLogPaths) {
        $index += 1
        $leaf = [System.IO.Path]::GetFileNameWithoutExtension($logPath)
        if ([string]::IsNullOrWhiteSpace($leaf)) { $leaf = "listener-type" }
        $tailPath = Join-Path $runtimeEvidenceDir ("{0}-{1}-{2}-tail.log" -f $safeLabel, $index, $leaf)
        try {
            Get-Content -LiteralPath $logPath -Tail 2500 -ErrorAction Stop |
                Set-Content -LiteralPath $tailPath -Encoding UTF8
            $logRecords.Add([pscustomobject]@{
                source = $logPath
                tail = $tailPath
                captured = $true
            }) | Out-Null
        } catch {
            $logRecords.Add([pscustomobject]@{
                source = $logPath
                tail = $tailPath
                captured = $false
                error = $_.Exception.Message
            }) | Out-Null
        }
    }

    return [pscustomobject]@{
        label = $Label
        time = (Get-Date).ToString("o")
        process_json = $processPath
        logs = @($logRecords)
    }
}

$firmwareSnapshot = Get-GitSnapshot -Path $repoRoot
$typeSnapshot = Get-GitSnapshot -Path $TypeRepo
$typeProcesses = @(Get-Process listener-type -ErrorAction SilentlyContinue | ForEach-Object {
    [pscustomobject]@{
        id = $_.Id
        path = $_.Path
        start_time = $_.StartTime
        main_window_title = $_.MainWindowTitle
    }
})

$sessionStart = [pscustomobject]@{
    event = "session_start"
    time = (Get-Date).ToString("o")
    scenario = $Scenario
    output_dir = (Resolve-Path $OutputDir).Path
    firmware = $firmwareSnapshot
    listener_type = $typeSnapshot
    listener_type_processes = $typeProcesses
    listener_type_evidence = Save-ListenerTypeEvidence -Label "session-start"
}
Add-JsonLine $sessionStart

if ($OpenNotepad) {
    try {
        Start-Process notepad.exe | Out-Null
    } catch {
        Add-JsonLine ([pscustomobject]@{
            event = "open_notepad_failed"
            time = (Get-Date).ToString("o")
            error = $_.Exception.Message
        })
    }
}

$intro = if ($Scenario -eq "NoTranscript") {
@"
接下来只复验 1.12 刚才失败的无识别 / WARN 场景。

这个向导会弹窗：
1. 无识别 / 失败 WARN
2. 最终 go/no-go

每一步请按实际观察选择 PASS、FAIL 或 SKIP。
点击 OK 开始；点击 Cancel 退出。
"@
} else {
@"
接下来做 1.12 人工整机验收。

这个向导会逐步弹窗：
1. 物理 KEY3 / 30 秒真实录音
2. 胶囊取消同步
3. 无识别 / 失败 WARN
4. 重连 / 重新配对抽查
5. 设置持久化和亮度抽查
6. Clean install / OOBE 判断
7. 最终 go/no-go

每一步请按实际观察选择 PASS、FAIL 或 SKIP。
点击 OK 开始；点击 Cancel 退出。
"@
}

$introResult = Show-TopMostMessageBox `
    -Title "Listener 1.12 人工验收向导" `
    -Message $intro `
    -Buttons ([System.Windows.Forms.MessageBoxButtons]::OKCancel) `
    -Icon ([System.Windows.Forms.MessageBoxIcon]::Information)

if ($introResult -ne [System.Windows.Forms.DialogResult]::OK) {
    Add-JsonLine ([pscustomobject]@{ event = "session_cancelled_at_intro"; time = (Get-Date).ToString("o") })
    Write-Host "human_acceptance_status=ABORT output_dir=$OutputDir"
    exit 0
}

$steps = @(
    [pscustomobject]@{
        id = "physical-key3-30s-recording"
        title = "实体 KEY3 30 秒真实录音"
        pass_text = "PASS"
        instructions = @"
请打开记事本或任意可输入文本框。

操作：
1. 用设备实体 KEY3 开始录音，不要用串口模拟。
2. 连续说约 30 秒。可以描述当前验收，例如：“这是 1.12 真实录音验收，录音结束后应该插入文字，REC 灯录音时亮，AI 灯处理中亮，OK 灯成功后亮。”
3. 再按实体 KEY3 停止。

通过标准：
- Type 胶囊正常出现、录音、处理中、退出。
- 目标输入框插入了可接受的文字。
- REC 录音时亮，停止后清掉。
- AI 处理中亮。
- 成功退出后 OK 绿灯亮；WARN 不应亮。
- 没有明显闪烁回退，LED5/LED6 不跟随 LED3/LED4 闪。
"@
    },
    [pscustomobject]@{
        id = "capsule-cancel-sync"
        title = "胶囊取消同步固件"
        pass_text = "PASS"
        instructions = @"
操作：
1. 用实体 KEY3 开始录音。
2. 看到 Type 胶囊进入录音后，在胶囊里点击取消 / X。

通过标准：
- 录音立刻停止，胶囊退出。
- 固件 REC 灯清掉。
- 不应该插入文字。
- OK 不应亮；WARN 可短暂提示取消/失败，但不能卡住。
- 设备能马上进入下一次录音准备状态。
"@
    },
    [pscustomobject]@{
        id = "no-transcript-warning"
        title = "无识别 / 失败时 WARN"
        pass_text = "PASS"
        instructions = @"
操作：
1. 用实体 KEY3 开始录音。
2. 保持安静约 3-5 秒，或刻意制造一次没有有效语音的录音。
3. 再按实体 KEY3 停止，或用胶囊确认结束。

通过标准：
- 没有有效文字时不应显示成功 OK。
- WARN 黄灯应亮，表示没有识别或处理失败。
- 不应错误插入一段正常文字。
- 胶囊应正常退出，不应卡在 processing。
"@
    },
    [pscustomobject]@{
        id = "reconnect-repair-spot-check"
        title = "重连 / 重新配对抽查"
        pass_text = "PASS"
        instructions = @"
操作任选一个：
- 关闭再打开设备，等待 Type 自动恢复连接；或
- 断开/重连蓝牙；或
- 如果你不想动蓝牙，至少观察 Type 当前显示设备已连接、下一轮录音仍可用。

通过标准：
- Type 能恢复到可录音状态。
- BLE 蓝灯语义合理：连接后稳定，不长时间卡在异常重连提示。
- 恢复后再做一次短录音仍能启动/停止。
- 不出现必须重启电脑或手动清缓存才能恢复的情况。
"@
    },
    [pscustomobject]@{
        id = "settings-brightness-persistence"
        title = "设置持久化 / 亮度抽查"
        pass_text = "PASS"
        instructions = @"
操作：
1. 打开 Type 的设备设置。
2. 抽查亮度或低功耗/电源设置是否能读到当前设备值。
3. 如果愿意，可轻微改一次亮度再恢复原值。
4. 重启 Type 或让设备重连后，再确认设置没有明显丢失。

通过标准：
- Type 读写设备设置正常。
- PWR/BLE/REC/AI/OK/WARN 受同一亮度上限约束，不出现 PWR 特别刺眼、BLE 过暗这种明显不一致。
- 设置不会在重连后莫名恢复旧值。
"@
    },
    [pscustomobject]@{
        id = "clean-install-oobe"
        title = "Clean install / OOBE 判断"
        pass_text = "PASS"
        instructions = @"
这一步用于判断“普通用户路径”。

如果你当前已经能用正常安装包/正常入口启动 Type，并能像普通用户一样配对设备，选 PASS。

如果现在只是开发环境 debug 进程、还没有跑安装包/OOBE，选 SKIP，并备注“未做 clean install / OOBE”。

通过标准：
- 非开发工具链入口能启动 Type。
- 首次配对/恢复配对路径不需要串口命令或手填 UUID。
- 用户能进入可录音状态。
"@
    },
    [pscustomobject]@{
        id = "final-go-no-go"
        title = "最终 Go / No-Go 决策"
        pass_text = "GO / 可以进入下一步"
        instructions = @"
请基于刚才所有观察做最终判断。

选 GO / PASS 的条件：
- 真实录音链路可用。
- 胶囊和固件灯效语义一致。
- OK/WARN 没有反。
- 没有明显阻塞出货体验的问题。

选 FAIL 的条件：
- 有阻塞级问题，例如无法录音、无法插入、灯效语义错误、取消/确认不同步、重连后不可用。

选 SKIP/稍后：
- 还需要补 clean install、长时间使用或其它外部条件才能下结论。

备注里请写残留问题。没有残留就写“无明显残留问题”。
"@
    }
)

if ($Scenario -eq "NoTranscript") {
    $steps = @($steps | Where-Object { $_.id -in @("no-transcript-warning", "final-go-no-go") })
}

$records = [System.Collections.Generic.List[object]]::new()
$aborted = $false
for ($i = 0; $i -lt $steps.Count; $i++) {
    $step = $steps[$i]
    $answer = Show-StepPrompt -Step $step -Index ($i + 1) -Total $steps.Count
    $stepEvidence = Save-ListenerTypeEvidence -Label ("step-{0:00}-{1}" -f ($i + 1), $step.id)
    $record = [pscustomobject]@{
        event = "human_step"
        time = (Get-Date).ToString("o")
        index = $i + 1
        total = $steps.Count
        id = $step.id
        title = $step.title
        result = $answer.result
        notes = $answer.notes
        listener_type_evidence = $stepEvidence
    }
    $records.Add($record) | Out-Null
    Add-JsonLine $record
    if ($answer.result -eq "ABORT") {
        $aborted = $true
        break
    }
}

$failCount = @($records | Where-Object { $_.result -eq "FAIL" }).Count
$skipCount = @($records | Where-Object { $_.result -eq "SKIP" }).Count
$passCount = @($records | Where-Object { $_.result -eq "PASS" }).Count
$final = @($records | Where-Object { $_.id -eq "final-go-no-go" } | Select-Object -Last 1)

$status = if ($aborted) {
    "ABORT"
} elseif ($failCount -gt 0) {
    "FAIL"
} elseif ($final.Count -eq 0 -or $final[0].result -ne "PASS") {
    "INCOMPLETE"
} elseif ($skipCount -gt 0) {
    "INCOMPLETE"
} else {
    "PASS"
}

$summary = [pscustomobject]@{
    status = $status
    time = (Get-Date).ToString("o")
    scenario = $Scenario
    output_dir = (Resolve-Path $OutputDir).Path
    pass_count = $passCount
    fail_count = $failCount
    skip_count = $skipCount
    aborted = $aborted
    firmware = $firmwareSnapshot
    listener_type = $typeSnapshot
    listener_type_processes = $typeProcesses
    listener_type_evidence = Save-ListenerTypeEvidence -Label "session-end"
    records = @($records)
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryJsonPath -Encoding UTF8

$md = [System.Collections.Generic.List[string]]::new()
$md.Add("# Voice Keyboard 1.12 Human Go/No-Go") | Out-Null
$md.Add("") | Out-Null
$md.Add("- Status: $status") | Out-Null
$md.Add("- Time: $($summary.time)") | Out-Null
$md.Add("- Firmware commit: $($firmwareSnapshot.commit)") | Out-Null
$md.Add("- Listener-Type commit: $($typeSnapshot.commit)") | Out-Null
$md.Add("- Session JSONL: $sessionPath") | Out-Null
$md.Add("- Summary JSON: $summaryJsonPath") | Out-Null
$md.Add("") | Out-Null
$md.Add("| # | id | result | notes |") | Out-Null
$md.Add("|---|---|---|---|") | Out-Null
foreach ($record in $records) {
    $md.Add("| $($record.index) | $($record.id) | $($record.result) | $(Convert-TableText $record.notes) |") | Out-Null
}
$md.Add("") | Out-Null
if ($status -eq "PASS") {
    $md.Add("Human gate result: PASS. Operator marked final go/no-go as GO and all required prompts passed.") | Out-Null
} elseif ($status -eq "INCOMPLETE") {
    $md.Add("Human gate result: INCOMPLETE. Remaining SKIP or missing final GO decision must be resolved before unblocking 1.12.") | Out-Null
} elseif ($status -eq "FAIL") {
    $md.Add("Human gate result: FAIL. Resolve failed observations before unblocking 1.12.") | Out-Null
} else {
    $md.Add("Human gate result: ABORT. No acceptance decision recorded.") | Out-Null
}
$md | Set-Content -LiteralPath $summaryMdPath -Encoding UTF8

Add-JsonLine ([pscustomobject]@{
    event = "session_summary"
    time = (Get-Date).ToString("o")
    status = $status
    summary_json = $summaryJsonPath
    summary_md = $summaryMdPath
})

$doneText = @"
1.12 人工验收向导已结束。

结果：$status

证据目录：
$OutputDir

摘要：
$summaryMdPath
"@
[void](Show-TopMostMessageBox -Title "Listener 1.12 人工验收结果" -Message $doneText -Buttons ([System.Windows.Forms.MessageBoxButtons]::OK) -Icon ([System.Windows.Forms.MessageBoxIcon]::Information))

Write-Host "human_acceptance_status=$status"
Write-Host "output_dir=$OutputDir"
Write-Host "summary_md=$summaryMdPath"
Write-Host "summary_json=$summaryJsonPath"
