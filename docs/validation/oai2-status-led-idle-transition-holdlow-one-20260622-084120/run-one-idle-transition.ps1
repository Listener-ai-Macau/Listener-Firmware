[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Port = "COM10",
    [int]$Baud = 115200,
    [string]$OutputDir = "."
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if (-not [System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir = Join-Path (Get-Location) $OutputDir
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$summaryPath = Join-Path $OutputDir "one-idle-transition-summary.md"
$sessionPath = Join-Path $OutputDir "one-idle-transition-session.jsonl"
$serialPath = Join-Path $OutputDir "one-idle-transition-serial.log"

Add-Type -AssemblyName System.Windows.Forms
[System.Windows.Forms.Application]::EnableVisualStyles()

function Show-Box {
    param(
        [Parameter(Mandatory = $true)][string]$Message,
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][System.Windows.Forms.MessageBoxButtons]$Buttons,
        [Parameter(Mandatory = $true)][System.Windows.Forms.MessageBoxIcon]$Icon
    )
    $owner = [System.Windows.Forms.Form]::new()
    try {
        $owner.StartPosition = "CenterScreen"
        $owner.ShowInTaskbar = $false
        $owner.TopMost = $true
        $owner.WindowState = [System.Windows.Forms.FormWindowState]::Minimized
        $owner.Show()
        $owner.Activate()
        return [System.Windows.Forms.MessageBox]::Show($owner, $Message, $Title, $Buttons, $Icon)
    } finally {
        $owner.Close()
        $owner.Dispose()
    }
}

function Read-SerialFor {
    param(
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$Serial,
        [Parameter(Mandatory = $true)][int]$Milliseconds
    )
    $deadline = (Get-Date).AddMilliseconds($Milliseconds)
    $chunks = [System.Collections.Generic.List[string]]::new()
    while ((Get-Date) -lt $deadline) {
        $text = $Serial.ReadExisting()
        if (-not [string]::IsNullOrEmpty($text)) {
            $chunks.Add($text) | Out-Null
        }
        Start-Sleep -Milliseconds 40
    }
    return ($chunks -join "")
}

function Send-Cmd {
    param(
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$Serial,
        [Parameter(Mandatory = $true)][string]$Command,
        [int]$ReadMs = 900
    )
    $script:serialLines.Add(("> {0}" -f $Command)) | Out-Null
    if ($Command -match "^(WAIT|SLEEP)\s+(\d+)$") {
        $response = Read-SerialFor -Serial $Serial -Milliseconds ([int]$Matches[2])
    } else {
        $Serial.Write(("{0}`n" -f $Command))
        $response = Read-SerialFor -Serial $Serial -Milliseconds $ReadMs
    }
    foreach ($line in ($response -split "`r?`n")) {
        if (-not [string]::IsNullOrWhiteSpace($line)) {
            $script:serialLines.Add($line) | Out-Null
        }
    }
    return [PSCustomObject]@{ command = $Command; response = $response }
}

$serialLines = [System.Collections.Generic.List[string]]::new()
$serial = $null
$result = "ABORT"
$observed = ""
$responses = @()
$postResponses = @()

try {
    $intro = Show-Box `
        -Title "oai2 idle单场景验收" `
        -Buttons ([System.Windows.Forms.MessageBoxButtons]::OKCancel) `
        -Icon ([System.Windows.Forms.MessageBoxIcon]::Information) `
        -Message "oai2：这次只测一个场景。`n`n点确定后，我会先跑状态 REC/AI，然后切到 connected idle。`n`n只判断切到 connected idle 之后：LED3-LED6 是否还会规律性全闪/全亮。前置 REC/AI 自己的动态不算失败。"
    if ($intro -ne [System.Windows.Forms.DialogResult]::OK) {
        throw "operator_cancelled_before_start"
    }

    $serial = [System.IO.Ports.SerialPort]::new($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $serial.ReadTimeout = 80
    $serial.WriteTimeout = 5000
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    $serial.Open()
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    [void](Read-SerialFor -Serial $serial -Milliseconds 600)

    foreach ($cmd in @(
        "~LED:PREVIEW clear",
        "~LED:OFF",
        "~LED:PREVIEW recording_processing_status_led_only",
        "~LED:REC_LEVEL 100 60000",
        "WAIT 1800",
        "~LED:PREVIEW connected",
        "WAIT 10000"
    )) {
        $readMs = 900
        if ($cmd -like "~LED:STATUS*" -or $cmd -like "~DIAGLOG*") { $readMs = 1800 }
        $responses += Send-Cmd -Serial $serial -Command $cmd -ReadMs $readMs
        Start-Sleep -Milliseconds 100
    }

    $choice = Show-Box `
        -Title "oai2 idle单场景结果" `
        -Buttons ([System.Windows.Forms.MessageBoxButtons]::YesNoCancel) `
        -Icon ([System.Windows.Forms.MessageBoxIcon]::Question) `
        -Message "刚才 connected idle 后，LED3-LED6 还有规律性全闪/全亮吗？`n`n点 是 = 还会闪，失败。`n点 否 = 没有复现，通过。`n点 取消 = 不确定/中止。"
    if ($choice -eq [System.Windows.Forms.DialogResult]::Yes) {
        $result = "FAIL"
    } elseif ($choice -eq [System.Windows.Forms.DialogResult]::No) {
        $result = "PASS"
    } else {
        $result = "ABORT"
    }
    $observed = "operator_dialog_result=$choice"

    foreach ($cmd in @("~LED:STATUS", "~DIAGLOG:LAST:80:status_led", "~POWER:STATUS")) {
        $postResponses += Send-Cmd -Serial $serial -Command $cmd -ReadMs 2200
        Start-Sleep -Milliseconds 100
    }
    [void](Send-Cmd -Serial $serial -Command "~LED:OFF" -ReadMs 400)
} finally {
    if ($null -ne $serial) {
        if ($serial.IsOpen) {
            $serial.Close()
        }
        $serial.Dispose()
    }
}

$serialLines | Set-Content -LiteralPath $serialPath -Encoding UTF8
[PSCustomObject]@{
    at = (Get-Date).ToString("o")
    port = $Port
    result = $result
    observed = $observed
    responses = $responses
    post_responses = $postResponses
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $sessionPath -Encoding UTF8

$lines = @(
    "# oai2 one-scene idle transition validation",
    "",
    "- Result: $result",
    "- Port: $Port",
    "- Scenario: status REC/AI -> connected idle",
    "- Serial log: $serialPath",
    "- Session JSON: $sessionPath",
    "- Observation: $observed"
)
$lines | Set-Content -LiteralPath $summaryPath -Encoding UTF8

Write-Host "result=$result"
Write-Host "summary=$summaryPath"
if ($result -eq "PASS") {
    exit 0
}
exit 1
