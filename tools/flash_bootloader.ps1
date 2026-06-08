param(
    [string]$Port = "COMx",
    [string]$Target = "esp32s3",
    [string]$BuildDir = $env:LISTENER_IDF_BUILD_DIR,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot

function Get-ShortBuildDir {
    param([Parameter(Mandatory = $true)][string]$ProjectRoot)

    if (-not [string]::IsNullOrWhiteSpace($BuildDir)) {
        return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDir)
    }

    if ($ProjectRoot.Length -lt 80) {
        return Join-Path $ProjectRoot "build"
    }

    $sha1 = [System.Security.Cryptography.SHA1]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($ProjectRoot.ToLowerInvariant())
        $hashBytes = $sha1.ComputeHash($bytes)
        $hash = -join ($hashBytes[0..5] | ForEach-Object { $_.ToString("x2") })
    } finally {
        $sha1.Dispose()
    }
    return Join-Path ([System.IO.Path]::GetTempPath()) "listener-idf-build-$hash"
}

function Get-PresentSerialPorts {
    $records = @()

    if (Get-Command Get-PnpDevice -ErrorAction SilentlyContinue) {
        try {
            $records += @(Get-PnpDevice -PresentOnly -Class Ports -ErrorAction Stop | ForEach-Object {
                $friendly = [string]$_.FriendlyName
                if ($friendly -match '\((COM\d+)\)') {
                    [PSCustomObject]@{
                        Port = $Matches[1].ToUpperInvariant()
                        FriendlyName = $friendly
                        InstanceId = [string]$_.InstanceId
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
                        InstanceId = [string]$_.PNPDeviceID
                    }
                }
            })
        } catch {
            $records += @()
        }
    }

    $seen = @{}
    $unique = @()
    foreach ($record in @($records)) {
        if ($null -eq $record -or [string]::IsNullOrWhiteSpace([string]$record.Port)) {
            continue
        }
        $key = ([string]$record.Port).ToUpperInvariant()
        if (-not $seen.ContainsKey($key)) {
            $seen[$key] = $true
            $unique += $record
        }
    }
    return @($unique)
}

function Resolve-FlasherPort {
    param([AllowNull()][string]$RequestedPort)

    $requested = ([string]$RequestedPort).Trim()
    if (-not [string]::IsNullOrWhiteSpace($requested) -and $requested -notmatch '^(?i)COMx$') {
        if ($requested -notmatch '^(?i)COM\d+$') {
            throw "Invalid port '$RequestedPort'. Use COM7, COMx, or omit -Port for auto-detect."
        }
        return $requested.ToUpperInvariant()
    }

    $ports = @(Get-PresentSerialPorts)
    if ($ports.Count -eq 0) {
        throw "No present serial ports were detected. Connect the ESP32-S3 USB serial device or pass -Port COMn."
    }

    $espPorts = @($ports | Where-Object {
        [string]$_.InstanceId -match 'VID_303A&PID_1001' -or
        [string]$_.FriendlyName -match '(?i)ESP32|USB JTAG|serial debug'
    })
    if ($espPorts.Count -eq 1) {
        return ([string]$espPorts[0].Port).ToUpperInvariant()
    }
    if ($ports.Count -eq 1) {
        return ([string]$ports[0].Port).ToUpperInvariant()
    }

    $summary = @($ports | ForEach-Object {
        "$($_.Port): $($_.FriendlyName)"
    }) -join "; "
    throw "Multiple serial ports were detected. Pass -Port explicitly. Ports: $summary"
}

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory = $true)][string]$File,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    & $File @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$File $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

$resolvedPort = Resolve-FlasherPort -RequestedPort $Port
$buildDirResolved = Get-ShortBuildDir -ProjectRoot $projectRoot
$bootloaderPath = Join-Path $buildDirResolved "bootloader\bootloader.bin"

Write-Host "Bootloader-only flash"
Write-Host "  Port: $resolvedPort"
Write-Host "  Target: $Target"
Write-Host "  Build dir: $buildDirResolved"
Write-Host "  Command: idf.py -B `"$buildDirResolved`" -p $resolvedPort bootloader-flash"

if (Test-Path -LiteralPath $bootloaderPath -PathType Leaf) {
    $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $bootloaderPath
    Write-Host "  Current bootloader: $($hash.Path)"
    Write-Host "  Current SHA256: $($hash.Hash)"
}

if ($DryRun) {
    Write-Host "DRY RUN: no flash performed."
    exit 0
}

. (Join-Path $PSScriptRoot "idf_env.ps1") -Target $Target
Invoke-CheckedCommand -File "idf.py" -Arguments @("-B", $buildDirResolved, "-p", $resolvedPort, "bootloader-flash")

if (Test-Path -LiteralPath $bootloaderPath -PathType Leaf) {
    $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $bootloaderPath
    Write-Host "Bootloader flash completed."
    Write-Host "  File: $($hash.Path)"
    Write-Host "  SHA256: $($hash.Hash)"
}
