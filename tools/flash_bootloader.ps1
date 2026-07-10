param(
    [string]$Port = "COMx",
    [string]$Target = "esp32s3",
    [string]$BuildDir = $env:LISTENER_IDF_BUILD_DIR,
    [int]$Baud = 115200,
    [ValidateSet("default_reset", "usb_reset", "no_reset", "no_reset_no_sync")]
    [string]$Before = "default_reset",
    [ValidateSet("hard_reset", "soft_reset", "no_reset", "no_reset_stub", "watchdog_reset")]
    [string]$After = "hard_reset",
    [switch]$NoStub,
    [switch]$NoCompress,
    [switch]$SkipFlashIdCheck,
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

function Invoke-CapturedCommand {
    param(
        [Parameter(Mandatory = $true)][string]$File,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $output = @(& $File @Arguments 2>&1)
    $exitCode = $LASTEXITCODE
    foreach ($line in $output) {
        Write-Host ([string]$line)
    }
    if ($exitCode -ne 0) {
        throw "$File $($Arguments -join ' ') failed with exit code $exitCode"
    }
    return ($output | ForEach-Object { [string]$_ }) -join [Environment]::NewLine
}

function Test-UsableFlashIdOutput {
    param([Parameter(Mandatory = $true)][string]$Text)

    if ($Text -match '(?im)^\s*Device:\s*(?:ffff|0000)\s*$') {
        return $false
    }
    if ($Text -match '(?im)^\s*Manufacturer:\s*(?:ff|00)\s*$') {
        return $false
    }
    if ($Text -match '(?im)^\s*Detected flash size:\s*Unknown\s*$') {
        return $false
    }
    return $true
}

function Get-Sha256FileHash {
    param([Parameter(Mandatory = $true)][string]$Path)

    $resolvedPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
    $getFileHash = Get-Command Get-FileHash -ErrorAction SilentlyContinue
    if ($getFileHash) {
        $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $resolvedPath
        return [PSCustomObject]@{
            Path = $hash.Path
            Hash = $hash.Hash
        }
    }

    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $stream = [System.IO.File]::OpenRead($resolvedPath)
        try {
            $bytes = $sha256.ComputeHash($stream)
        } finally {
            $stream.Dispose()
        }
    } finally {
        $sha256.Dispose()
    }

    return [PSCustomObject]@{
        Path = $resolvedPath
        Hash = (-join ($bytes | ForEach-Object { $_.ToString("X2") }))
    }
}

$resolvedPort = Resolve-FlasherPort -RequestedPort $Port
$buildDirResolved = Get-ShortBuildDir -ProjectRoot $projectRoot
$bootloaderPath = Join-Path $buildDirResolved "bootloader\bootloader.bin"
$esptoolPathForDisplay = if ([string]::IsNullOrWhiteSpace($env:IDF_PATH)) {
    "<IDF_PATH>\components\esptool_py\esptool\esptool.py"
} else {
    Join-Path $env:IDF_PATH "components\esptool_py\esptool\esptool.py"
}

Write-Host "Bootloader-only flash"
Write-Host "  Port: $resolvedPort"
Write-Host "  Target: $Target"
Write-Host "  Baud: $Baud"
Write-Host "  Mode: $(if ($NoStub) { 'ROM no-stub' } else { 'IDF bootloader-flash' })"
if ($NoStub) {
    Write-Host "  Transfer: $(if ($NoCompress) { 'no-compress' } else { 'esptool default' })"
}
Write-Host "  Reset: before=$Before after=$After"
Write-Host "  Build dir: $buildDirResolved"
if ($NoStub) {
    Write-Host "  Build command: pwsh -NoProfile -File .\tools\idf.ps1 -B `"$buildDirResolved`" bootloader"
    $compressDisplay = if ($NoCompress) { " --no-compress" } else { "" }
    Write-Host "  Flash command: python `"$esptoolPathForDisplay`" --chip $Target -p $resolvedPort -b $Baud --before=$Before --after=$After --no-stub write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB$compressDisplay 0x0 `"$bootloaderPath`""
} else {
    Write-Host "  Command: pwsh -NoProfile -File .\tools\idf.ps1 -B `"$buildDirResolved`" -p $resolvedPort -b $Baud bootloader-flash"
}

if (Test-Path -LiteralPath $bootloaderPath -PathType Leaf) {
    $hash = Get-Sha256FileHash -Path $bootloaderPath
    Write-Host "  Current bootloader: $($hash.Path)"
    Write-Host "  Current SHA256: $($hash.Hash)"
}

if ($DryRun) {
    Write-Host "DRY RUN: no flash performed."
    exit 0
}

. (Join-Path $PSScriptRoot "idf_env.ps1") -Target $Target
if ($NoStub) {
    $esptoolPath = Join-Path $env:IDF_PATH "components\esptool_py\esptool\esptool.py"
    if (-not $SkipFlashIdCheck) {
        Write-Host "Checking SPI flash identity before write..."
        $flashIdOutput = Invoke-CapturedCommand -File "python" -Arguments @(
            $esptoolPath,
            "--chip", $Target,
            "-p", $resolvedPort,
            "-b", ([string]$Baud),
            "--before=$Before",
            "--after=no_reset",
            "--no-stub",
            "flash_id"
        )
        if (-not (Test-UsableFlashIdOutput -Text $flashIdOutput)) {
            Write-Host "ERROR: SPI flash identity is not usable. The chip connected, but flash_id returned an invalid/unknown flash device. Check USB power, cable, board power-hold, and SPI flash/module hardware before retrying."
            exit 42
        }
    }

    Invoke-CheckedCommand -File "pwsh" -Arguments @(
        "-NoProfile",
        "-File",
        (Join-Path $PSScriptRoot "idf.ps1"),
        "-B", $buildDirResolved,
        "bootloader"
    )
    $writeFlashArgs = @(
        $esptoolPath,
        "--chip", $Target,
        "-p", $resolvedPort,
        "-b", ([string]$Baud),
        "--before=$Before",
        "--after=$After",
        "--no-stub",
        "write_flash",
        "--flash_mode", "dio",
        "--flash_freq", "80m",
        "--flash_size", "16MB"
    )
    if ($NoCompress) {
        $writeFlashArgs += "--no-compress"
    }
    $writeFlashArgs += @("0x0", $bootloaderPath)
    Invoke-CheckedCommand -File "python" -Arguments $writeFlashArgs
} else {
    Invoke-CheckedCommand -File "pwsh" -Arguments @(
        "-NoProfile",
        "-File",
        (Join-Path $PSScriptRoot "idf.ps1"),
        "-B", $buildDirResolved,
        "-p", $resolvedPort,
        "-b", ([string]$Baud),
        "bootloader-flash"
    )
}

if (Test-Path -LiteralPath $bootloaderPath -PathType Leaf) {
    $hash = Get-Sha256FileHash -Path $bootloaderPath
    Write-Host "Bootloader flash completed."
    Write-Host "  File: $($hash.Path)"
    Write-Host "  SHA256: $($hash.Hash)"
}
