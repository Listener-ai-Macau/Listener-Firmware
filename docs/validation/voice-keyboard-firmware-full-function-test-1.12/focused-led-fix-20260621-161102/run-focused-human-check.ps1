param(
    [string]$FirmwareRepo = "C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware-pwr-shutdown-new-board",
    [string]$TypeRepo = "C:\Users\Billy\Desktop\Denzic\Listener\Listener-Type",
    [string]$Port = "COM10"
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName Microsoft.VisualBasic

$artifactDir = $PSScriptRoot
$serialLog = Join-Path $artifactDir "firmware-human-focused-serial.log"
$serialErr = Join-Path $artifactDir "firmware-human-focused-serial.err.log"
$typeTail = Join-Path $artifactDir "listener-type-human-focused-tail.log"
$resultJson = Join-Path $artifactDir "human-focused-result.json"
$summaryMd = Join-Path $artifactDir "human-focused-summary.md"

$captureScript = Join-Path $FirmwareRepo "tools\capture_serial.ps1"
$capture = Start-Process -FilePath "pwsh" `
    -ArgumentList @("-NoProfile", "-File", $captureScript, "-Port", $Port, "-DurationSeconds", "75") `
    -WindowStyle Hidden `
    -RedirectStandardOutput $serialLog `
    -RedirectStandardError $serialErr `
    -PassThru

$message = @"
只验这次新增/修复项，不需要重复已经 PASS 的完整验收。

1. 确认 Listener Type 还在运行。
2. 按一次设备 KEY3 开始录音，说一句短句，再按 KEY3 停止。
3. 观察 LED4/AI：录音期间和刚停止转写时不要提前紫色闪；胶囊文字出现后，进入后处理时才开始紫色“哒 哒哒”思考节奏。
4. 成功结束时 LED5/OK 应该是绿色；没有识别或失败才看 LED6/WARN 黄色。
5. 空闲插 USB 时，LED1/PWR 白和 LED2/BLE 蓝亮度应接近、都不刺眼；蓝灯代表 Type audio notify ready。

点“是”表示通过，“否”表示未通过，“取消”表示跳过。
"@

$dialog = [System.Windows.Forms.MessageBox]::Show(
    $message,
    "Listener 聚焦灯效确认",
    [System.Windows.Forms.MessageBoxButtons]::YesNoCancel,
    [System.Windows.Forms.MessageBoxIcon]::Question,
    [System.Windows.Forms.MessageBoxDefaultButton]::Button1,
    [System.Windows.Forms.MessageBoxOptions]::DefaultDesktopOnly
)

$note = ""
if ($dialog -eq [System.Windows.Forms.DialogResult]::No) {
    $note = [Microsoft.VisualBasic.Interaction]::InputBox(
        "请简单写一下没通过的现象，例如：紫灯仍提前闪 / OK 不是绿色 / 蓝白亮度不一致。",
        "记录未通过原因",
        ""
    )
}

if (-not $capture.HasExited) {
    Wait-Process -Id $capture.Id -Timeout 80 -ErrorAction SilentlyContinue
}
if (-not $capture.HasExited) {
    Stop-Process -Id $capture.Id -Force -ErrorAction SilentlyContinue
}

$typeSource = Join-Path $TypeRepo "tests\artifacts\manual_type_launch\tauri-dev-20260621-154431.out.log"
if (Test-Path -LiteralPath $typeSource) {
    Get-Content -LiteralPath $typeSource -Tail 500 | Set-Content -LiteralPath $typeTail -Encoding UTF8
}

$status = switch ($dialog) {
    ([System.Windows.Forms.DialogResult]::Yes) { "PASS" }
    ([System.Windows.Forms.DialogResult]::No) { "FAIL" }
    default { "SKIP" }
}

$result = [ordered]@{
    checked_at = (Get-Date).ToString("o")
    status = $status
    note = $note
    port = $Port
    scope = @(
        "AI LED starts after text appears, not during recording/stop transcription",
        "OK green only on success; warning yellow only on no transcript/failure",
        "Idle USB PWR/BLE brightness is balanced and BLE means Type notify ready"
    )
    artifacts = @(
        $serialLog,
        $serialErr,
        $typeTail
    )
}

($result | ConvertTo-Json -Depth 4) | Set-Content -LiteralPath $resultJson -Encoding UTF8

@(
    "# Focused LED Fix Human Check",
    "",
    "- Status: $status",
    "- Note: $note",
    "- Firmware serial: $serialLog",
    "- Type tail: $typeTail"
) | Set-Content -LiteralPath $summaryMd -Encoding UTF8

Write-Host "human_focused_status=$status"
Write-Host "summary=$summaryMd"
