param(
    [string]$Port = "COMx",
    [string]$Target = "esp32s3",
    [string]$BuildDir = $env:LISTENER_IDF_BUILD_DIR,
    [int[]]$Baud = @(115200, 57600, 9600),
    [ValidateSet("default_reset", "usb_reset", "no_reset", "no_reset_no_sync")]
    [string]$Before = "default_reset",
    [ValidateSet("hard_reset", "soft_reset", "no_reset", "no_reset_stub", "watchdog_reset")]
    [string]$After = "hard_reset",
    [Alias("dry-run")]
    [switch]$DryRun,
    [switch]$Yes,
    [Alias("no-pause")]
    [switch]$NoPause,
    [switch]$UseStub,
    [switch]$NoSafetyPrompt,
    [switch]$SkipFlashIdCheck
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $PSCommandPath
$repoRoot = Split-Path -Parent $scriptDir
$flashScript = Join-Path $scriptDir "flash_bootloader.ps1"

function Write-Section {
    param([Parameter(Mandatory = $true)][string]$Text)
    Write-Host ""
    Write-Host "=== $Text ==="
}

function Get-PresentSerialPortSummary {
    $records = @()

    if (Get-Command Get-PnpDevice -ErrorAction SilentlyContinue) {
        try {
            $records += @(Get-PnpDevice -PresentOnly -Class Ports -ErrorAction Stop | ForEach-Object {
                $friendly = [string]$_.FriendlyName
                if ($friendly -match '\((COM\d+)\)') {
                    [PSCustomObject]@{
                        Port = $Matches[1].ToUpperInvariant()
                        FriendlyName = $friendly
                    }
                }
            })
        } catch {
            $records += @()
        }
    }

    if ($records.Count -eq 0) {
        try {
            $records += @(Get-CimInstance Win32_SerialPort -ErrorAction Stop | ForEach-Object {
                if ($_.DeviceID -match '^COM\d+$') {
                    [PSCustomObject]@{
                        Port = ([string]$_.DeviceID).ToUpperInvariant()
                        FriendlyName = [string]$_.Name
                    }
                }
            })
        } catch {
            $records += @()
        }
    }

    $seen = @{}
    foreach ($record in @($records)) {
        if ($null -eq $record -or [string]::IsNullOrWhiteSpace([string]$record.Port)) {
            continue
        }
        $key = ([string]$record.Port).ToUpperInvariant()
        if (-not $seen.ContainsKey($key)) {
            $seen[$key] = $record
        }
    }

    return @($seen.Values | Sort-Object Port | ForEach-Object {
        "$($_.Port): $($_.FriendlyName)"
    })
}

function Resolve-PowerShellExecutable {
    $pwsh = Get-Command pwsh.exe -ErrorAction SilentlyContinue
    if ($pwsh) {
        return $pwsh.Source
    }

    $windowsPowerShell = Get-Command powershell.exe -ErrorAction SilentlyContinue
    if ($windowsPowerShell) {
        return $windowsPowerShell.Source
    }

    throw "Neither pwsh.exe nor powershell.exe is available on PATH."
}

function Show-SafetyPrompt {
    if ($DryRun -or $NoSafetyPrompt) {
        return
    }

    Write-Section "Operator action"
    Write-Host "Do not long-press the device power/on key during this repair."
    Write-Host "A long press can power the board off and make the COM port disappear."

    try {
        Add-Type -AssemblyName System.Windows.Forms -ErrorAction Stop
        [void][System.Windows.Forms.MessageBox]::Show(
            "修复过程中不要长按电源/开机键。长按可能会让设备关机并导致串口消失。点 OK 后保持 USB 插着，等待工具完成。",
            "Listener bootloader repair",
            [System.Windows.Forms.MessageBoxButtons]::OK,
            [System.Windows.Forms.MessageBoxIcon]::Warning
        )
    } catch {
        [void](Read-Host "Press Enter to continue without long-pressing the power/on key")
    }
}

if (-not (Test-Path -LiteralPath $flashScript -PathType Leaf)) {
    throw "Missing $flashScript"
}

Write-Section "Listener ESP32-S3 bootloader repair"
Write-Host "This flashes only the bootloader area at 0x0."
Write-Host "It does not erase the app, pairing settings, NVS, or diag_log partitions."
Write-Host "Repo: $repoRoot"

Write-Section "Detected serial ports"
$ports = @(Get-PresentSerialPortSummary)
if ($ports.Count -eq 0) {
    Write-Host "No serial ports are currently visible."
} else {
    $ports | ForEach-Object { Write-Host "  $_" }
}

if (-not $PSBoundParameters.ContainsKey("Port")) {
    Write-Host ""
    $portInput = Read-Host "COM port (Enter = auto-detect ESP32-S3)"
    if (-not [string]::IsNullOrWhiteSpace($portInput)) {
        $Port = $portInput.Trim()
    }
}

if (-not $DryRun -and -not $Yes) {
    Write-Host ""
    Write-Host "Ready to run bootloader-only flash on port '$Port'."
    $answer = Read-Host "Type Y and press Enter to continue"
    if ($answer -notin @("Y", "y", "YES", "yes")) {
        Write-Host "Cancelled."
        exit 2
    }
}

Show-SafetyPrompt

$baseFlashArgs = @(
    "-NoProfile",
    "-ExecutionPolicy",
    "Bypass",
    "-File",
    $flashScript,
    "-Port",
    $Port,
    "-Target",
    $Target,
    "-Before",
    $Before,
    "-After",
    $After
)

if (-not [string]::IsNullOrWhiteSpace($BuildDir)) {
    $baseFlashArgs += @("-BuildDir", $BuildDir)
}
if ($DryRun) {
    $baseFlashArgs += "-DryRun"
}
if (-not $UseStub) {
    $baseFlashArgs += @("-NoStub", "-NoCompress")
    if ($SkipFlashIdCheck) {
        $baseFlashArgs += "-SkipFlashIdCheck"
    }
}

$powerShellExe = Resolve-PowerShellExecutable

$failures = @()
foreach ($baudValue in @($Baud)) {
    $flashArgs = @($baseFlashArgs + @("-Baud", ([string]$baudValue)))

    Write-Section "Running baud $baudValue"
    Write-Host "$powerShellExe $($flashArgs -join ' ')"
    & $powerShellExe @flashArgs
    $exitCode = $LASTEXITCODE
    if ($exitCode -eq 0) {
        Write-Section "Done"
        if ($DryRun) {
            Write-Host "Dry run completed; no flash was performed."
        } else {
            Write-Host "Bootloader-only flash completed."
        }
        exit 0
    }
    if ($exitCode -eq 42) {
        throw "SPI flash identity precheck failed. The ESP32-S3 ROM is reachable, but the SPI flash/module is not readable enough to repair safely."
    }

    $failures += "baud $baudValue exit $exitCode"
    Write-Warning "Bootloader flash attempt failed at baud $baudValue with exit code $exitCode."
    if (-not $DryRun) {
        Start-Sleep -Seconds 2
    }
}

throw "All bootloader flash attempts failed: $($failures -join '; ')"
