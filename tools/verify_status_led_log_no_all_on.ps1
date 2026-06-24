[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Port = "",
    [int]$Baud = 115200,
    [string]$OutputDir = "",
    [int]$DiagCount = 220
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$aiwPath = "C:\Users\Billy\Desktop\Denzic\ai-collaboration-workflow\scripts\aiw.ps1"
$serialCaptureScript = Join-Path $PSScriptRoot "send_serial_and_capture.ps1"

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repoRoot ".cache\validation\status-led-log-no-all-on-$stamp"
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir = Join-Path $repoRoot $OutputDir
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path

$summaryPath = Join-Path $OutputDir "summary.json"
$capturePath = Join-Path $OutputDir "serial-status-led.txt"

function Get-SerialPorts {
    try {
        return @([System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object)
    } catch {
        return @()
    }
}

function Resolve-TestPort {
    if (-not [string]::IsNullOrWhiteSpace($Port)) {
        return $Port
    }
    $ports = @(Get-SerialPorts)
    if ($ports.Count -ne 1) {
        throw "Expected exactly one serial port when -Port is omitted; found $($ports -join ',')."
    }
    return $ports[0]
}

function Get-LatestLine {
    param(
        [Parameter(Mandatory = $true)][string[]]$Lines,
        [Parameter(Mandatory = $true)][string]$Pattern
    )
    $matches = @($Lines | Where-Object { $_ -like $Pattern })
    if ($matches.Count -eq 0) {
        return ""
    }
    return $matches[-1]
}

function Get-RgbMap {
    param([AllowEmptyString()][Parameter(Mandatory = $true)][string]$Line)
    $result = [ordered]@{}
    if ([string]::IsNullOrWhiteSpace($Line)) {
        return $result
    }
    foreach ($name in @("PWR", "BLE", "REC", "AI", "OK", "WARN")) {
        $pattern = "{0}:(\d+),(\d+),(\d+)" -f [regex]::Escape($name)
        $match = [regex]::Match($Line, $pattern)
        if ($match.Success) {
            $result[$name] = [ordered]@{
                r = [int]$match.Groups[1].Value
                g = [int]$match.Groups[2].Value
                b = [int]$match.Groups[3].Value
                on = (([int]$match.Groups[1].Value) -gt 0 -or
                      ([int]$match.Groups[2].Value) -gt 0 -or
                      ([int]$match.Groups[3].Value) -gt 0)
            }
        }
    }
    return $result
}

function Test-RgbOn {
    param(
        [Parameter(Mandatory = $true)]$RgbMap,
        [Parameter(Mandatory = $true)][string]$Name
    )
    return $RgbMap.Contains($Name) -and [bool]$RgbMap[$Name].on
}

function Decode-FrameIntensityA {
    param([uint32]$Value)
    return [ordered]@{
        PWR = [int]($Value -band 0xff)
        BLE = [int](($Value -shr 8) -band 0xff)
        REC = [int](($Value -shr 16) -band 0xff)
        AI = [int](($Value -shr 24) -band 0xff)
    }
}

function Decode-FrameIntensityB {
    param([uint32]$Value)
    return [ordered]@{
        OK = [int]($Value -band 0xff)
        WARN = [int](($Value -shr 8) -band 0xff)
        EC11 = [int](($Value -shr 16) -band 0xff)
        EDGE = [int](($Value -shr 24) -band 0xff)
    }
}

function Add-Failure {
    param(
        [System.Collections.Generic.List[string]]$Failures,
        [Parameter(Mandatory = $true)][string]$Message
    )
    $Failures.Add($Message) | Out-Null
}

$testPort = Resolve-TestPort
$commandList = ("~POWER:STATUS;;~LED:STATUS;;~DIAGLOG:LAST:{0}:status_led;;~LED:STATUS" -f $DiagCount)
$output = & $aiwPath with-lock -Resource $testPort -Wait -WaitTimeoutSeconds 120 -TimeoutMinutes 2 `
    -Purpose "oai2 status LED log no-all-on verification" `
    -Run pwsh -NoProfile -File $serialCaptureScript -Port $testPort -Baud $Baud `
    -InitialReadMs 900 -CommandReadMs 8000 -CommandList $commandList -OutputPath $capturePath 2>&1
$captureExit = $LASTEXITCODE
if ($captureExit -ne 0) {
    $output | Set-Content -LiteralPath (Join-Path $OutputDir "capture-error.txt") -Encoding UTF8
    throw "serial capture failed exit=$captureExit"
}

$lines = @(Get-Content -LiteralPath $capturePath)
$stateLine = Get-LatestLine -Lines $lines -Pattern "~LED:STATUS detail=state*"
$rgbLine = Get-LatestLine -Lines $lines -Pattern "~LED:STATUS detail=rgb status_rgb=*"
$summaryLine = Get-LatestLine -Lines $lines -Pattern "~LED:STATUS profile=*detail=summary*"
$powerLine = Get-LatestLine -Lines $lines -Pattern "*~POWER:STATUS state=*"

$failures = [System.Collections.Generic.List[string]]::new()
$warnings = [System.Collections.Generic.List[string]]::new()
if ([string]::IsNullOrWhiteSpace($stateLine)) {
    Add-Failure $failures "missing current ~LED:STATUS detail=state line"
}
if ([string]::IsNullOrWhiteSpace($rgbLine)) {
    Add-Failure $failures "missing current ~LED:STATUS detail=rgb line"
}
if ([string]::IsNullOrWhiteSpace($summaryLine)) {
    Add-Failure $failures "missing current ~LED:STATUS summary line"
}

$rgbMap = Get-RgbMap -Line $rgbLine
$statusTailAnyOn =
    (Test-RgbOn -RgbMap $rgbMap -Name "REC") -or
    (Test-RgbOn -RgbMap $rgbMap -Name "AI") -or
    (Test-RgbOn -RgbMap $rgbMap -Name "OK") -or
    (Test-RgbOn -RgbMap $rgbMap -Name "WARN")
$statusTailAllOn =
    (Test-RgbOn -RgbMap $rgbMap -Name "REC") -and
    (Test-RgbOn -RgbMap $rgbMap -Name "AI") -and
    (Test-RgbOn -RgbMap $rgbMap -Name "OK") -and
    (Test-RgbOn -RgbMap $rgbMap -Name "WARN")

if ($statusTailAllOn) {
    Add-Failure $failures ("current ~LED:STATUS rgb shows LED3-LED6 on together: {0}" -f $rgbLine)
}

$quietCurrentState =
    $stateLine -match 'rec_active=0' -and
    $stateLine -match 'processing=0' -and
    $stateLine -match 'error_domain=none'
if ($quietCurrentState -and $statusTailAnyOn) {
    Add-Failure $failures ("quiet current LED state has REC/AI/OK/WARN lit: {0}" -f $rgbLine)
}

if ($summaryLine -match 'active_flags=.*REC:1,AI:1,OK:1,WARN:1') {
    Add-Failure $failures ("current active_flags show LED3-LED6 on together: {0}" -f $summaryLine)
}
if ($quietCurrentState -and $summaryLine -match 'active_flags=.*(REC:1|AI:1|OK:1|WARN:1)') {
    Add-Failure $failures ("quiet current active_flags include REC/AI/OK/WARN: {0}" -f $summaryLine)
}

$visualEvents = [System.Collections.Generic.List[object]]::new()
$frameEvents = [System.Collections.Generic.List[object]]::new()
$jsonParseErrors = 0
foreach ($line in $lines) {
    if ($line -notmatch '^\{') {
        continue
    }
    try {
        $event = $line | ConvertFrom-Json
    } catch {
        $jsonParseErrors++
        continue
    }
    if ($event.src -ne "status_led") {
        continue
    }
    if ([int]$event.evt -eq 6) {
        $activeFlags = [uint32]$event.a1
        $record = [ordered]@{
            t = [int]$event.t
            active_flags = [int]$activeFlags
            rec = (($activeFlags -band 4) -ne 0)
            ai = (($activeFlags -band 8) -ne 0)
            ok = (($activeFlags -band 16) -ne 0)
            warn = (($activeFlags -band 32) -ne 0)
            raw = $line
        }
        $visualEvents.Add($record) | Out-Null
        if (($activeFlags -band 0x3c) -eq 0x3c) {
            Add-Failure $failures ("diag visual event shows LED3-LED6 on together: {0}" -f $line)
        }
    } elseif ([int]$event.evt -eq 8) {
        $a = Decode-FrameIntensityA -Value ([uint32]$event.a2)
        $b = Decode-FrameIntensityB -Value ([uint32]$event.a3)
        $record = [ordered]@{
            t = [int]$event.t
            status_color_classes = [int]$event.a1
            intensity_a = $a
            intensity_b = $b
            raw = $line
        }
        $frameEvents.Add($record) | Out-Null
        if ($a.REC -gt 0 -and $a.AI -gt 0 -and $b.OK -gt 0 -and $b.WARN -gt 0) {
            Add-Failure $failures ("diag frame event shows LED3-LED6 intensity on together: {0}" -f $line)
        }
    }
}

if ($visualEvents.Count -eq 0) {
    $warnings.Add("no status_led DIAG_LED_VISUAL_STATE events were present in the bounded diag tail") | Out-Null
}
if ($frameEvents.Count -eq 0) {
    $warnings.Add("no status_led DIAG_LED_FRAME_RGB events were present in the bounded diag tail") | Out-Null
}
if ($jsonParseErrors -gt 0) {
    Add-Failure $failures ("failed to parse {0} JSON diag line(s)" -f $jsonParseErrors)
}

$result = if ($failures.Count -eq 0) { "PASS" } else { "FAIL" }
$visualEventArray = @($visualEvents.ToArray())
$frameEventArray = @($frameEvents.ToArray())
$warningArray = @($warnings.ToArray())
$failureArray = @($failures.ToArray())
$recentVisualEventRaw = @($visualEventArray | Select-Object -Last 12 | ForEach-Object { [string]$_.raw })
$recentFrameEventRaw = @($frameEventArray | Select-Object -Last 12 | ForEach-Object { [string]$_.raw })
$summary = [ordered]@{
    schema = "listener.status_led.log_no_all_on.v1"
    generated_at = (Get-Date).ToString("o")
    agent = "codex"
    result = $result
    port = $testPort
    output_dir = $OutputDir
    capture = $capturePath
    power_status = $powerLine
    led_state = $stateLine
    led_rgb = $rgbLine
    led_summary = $summaryLine
    quiet_current_state = $quietCurrentState
    current_status_tail_any_on = $statusTailAnyOn
    current_status_tail_all_on = $statusTailAllOn
    diag_visual_event_count = $visualEvents.Count
    diag_frame_event_count = $frameEvents.Count
    diag_visual_active_flags = @($visualEventArray | ForEach-Object { $_.active_flags } | Sort-Object -Unique)
    recent_visual_events = $recentVisualEventRaw
    recent_frame_events = $recentFrameEventRaw
    warnings = $warningArray
    failures = $failureArray
}
($summary | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $summaryPath -Encoding UTF8

Write-Host ("result={0}" -f $result)
Write-Host ("summary={0}" -f $summaryPath)
Write-Host ("capture={0}" -f $capturePath)
if ($result -ne "PASS") {
    foreach ($failure in $failures) {
        Write-Host ("FAIL: {0}" -f $failure)
    }
    exit 1
}
foreach ($warning in $warnings) {
    Write-Host ("WARN: {0}" -f $warning)
}
Write-Host "PASS: status LED log shows no REC/AI/OK/WARN all-on regression."
