param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [string]$BluetoothAddress = "A4CB8FF459A6",
    [string]$DeviceName = "listener",
    [string]$OutputDir = (Split-Path -Parent $PSCommandPath),
    [int]$PostReadySettleSeconds = 30,
    [int]$Baud = 115200
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..\..")).Path
$outputDirResolved = (Resolve-Path -LiteralPath $OutputDir -ErrorAction SilentlyContinue)
if ($null -eq $outputDirResolved) {
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    $outputDirResolved = Resolve-Path -LiteralPath $OutputDir
}
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$flashLog = Join-Path $outputDirResolved ("connected-sync-flash-{0}.log" -f $stamp)
$bleLog = Join-Path $outputDirResolved ("connected-sync-ble-{0}.log" -f $stamp)
$serialLog = Join-Path $outputDirResolved ("connected-sync-led-status-{0}.log" -f $stamp)
$summaryPath = Join-Path $outputDirResolved ("connected-sync-hardware-summary-{0}.md" -f $stamp)

Push-Location $repoRoot
try {
    & pwsh -NoProfile -File .\tools\flash.ps1 -Port $Port -Target esp32s3 -NoBuild *> $flashLog
    if ($LASTEXITCODE -ne 0) {
        throw "flash failed exit_code=$LASTEXITCODE log=$flashLog"
    }

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\ensure_ble_hid_connection.ps1 `
        -DeviceName $DeviceName `
        -BluetoothAddress $BluetoothAddress `
        -DurationSeconds 35 `
        -PollIntervalSeconds 2 `
        -ExitOnReady *> $bleLog
    if ($LASTEXITCODE -ne 0) {
        throw "BLE ensure failed exit_code=$LASTEXITCODE log=$bleLog"
    }

    Start-Sleep -Seconds $PostReadySettleSeconds

    $serial = [System.IO.Ports.SerialPort]::new($Port, $Baud)
    $serial.ReadTimeout = 200
    $serial.WriteTimeout = 1000
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    $chunks = [System.Collections.Generic.List[string]]::new()
    try {
        $serial.Open()
        $serial.DtrEnable = $false
        $serial.RtsEnable = $false
        Start-Sleep -Milliseconds 250
        $serial.Write("~LED:STATUS`n")
        $deadline = [DateTimeOffset]::Now.AddSeconds(4)
        while ([DateTimeOffset]::Now -lt $deadline) {
            try {
                $text = $serial.ReadExisting()
                if (-not [string]::IsNullOrEmpty($text)) {
                    $chunks.Add($text)
                }
            } catch [TimeoutException] {
            }
            Start-Sleep -Milliseconds 100
        }
    } finally {
        if ($serial.IsOpen) {
            $serial.Close()
        }
        $serial.Dispose()
    }

    $serialText = ($chunks -join "")
    Set-Content -LiteralPath $serialLog -Value $serialText -Encoding UTF8

    $statusLines = @(
        $serialText -split "\r?\n" |
            Where-Object { $_ -match "~LED:STATUS\s+profile=" }
    )
    if ($statusLines.Count -eq 0) {
        throw "missing ~LED:STATUS response log=$serialLog"
    }
    $lastStatus = [string]$statusLines[-1]
    if ($lastStatus -notmatch "\bble=connected\b") {
        throw "final status is not connected: $lastStatus"
    }
    if ($lastStatus -match "\bble=(pairing|reconnecting)\b") {
        throw "final status regressed to pairing/reconnecting: $lastStatus"
    }
    if ($lastStatus -match "\bble_confidence_ms_left=([0-9]+)\b" -and [int]$Matches[1] -gt 0) {
        throw "connected confidence window did not drain after settle: $lastStatus"
    }
    if ($lastStatus -match "\boobe_confidence_ms_left=([0-9]+)\b" -and [int]$Matches[1] -gt 0) {
        throw "OOBE confidence window did not drain after settle: $lastStatus"
    }

    $summary = @(
        "# BLE Status LED Connected Sync Hardware Probe",
        "",
        "- Port: $Port",
        "- BLE address: $BluetoothAddress",
        "- Flash log: $([System.IO.Path]::GetFileName($flashLog))",
        "- BLE ensure log: $([System.IO.Path]::GetFileName($bleLog))",
        "- Serial status log: $([System.IO.Path]::GetFileName($serialLog))",
        "- Final status: $lastStatus",
        "- Result: PASS"
    )
    Set-Content -LiteralPath $summaryPath -Value ($summary -join "`r`n") -Encoding UTF8
    Write-Output "PASS: BLE connected LED hardware probe final status connected after bounded confidence window."
    Write-Output "summary=$summaryPath"
    Write-Output "serial=$serialLog"
} finally {
    Pop-Location
}
