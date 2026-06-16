[CmdletBinding(PositionalBinding = $false)]
param(
    [ValidateSet("help", "ports", "probe", "check-flash", "flash", "restore-bootloader", "erase-otadata", "erase-flash")]
    [string]$Action = "help",
    [string]$Port = "COMx",
    [string]$Target = "esp32s3",
    [string]$BuildDir = $env:LISTENER_IDF_BUILD_DIR,
    [int]$Baud = 460800,
    [string]$OutputDir = "",
    [switch]$NoBuild,
    [switch]$PreserveOtaData,
    [switch]$ConfirmEraseFlash
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

try {
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    [Console]::InputEncoding = $utf8NoBom
    [Console]::OutputEncoding = $utf8NoBom
    $OutputEncoding = $utf8NoBom
} catch {
}

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $ProjectRoot ".cache\device_maintenance"
}

function Show-Usage {
    Write-Host "Listener device maintenance"
    Write-Host ""
    Write-Host "Read-only:"
    Write-Host "  powershell -ExecutionPolicy Bypass -File .\tools\device_maintenance.ps1 -Action ports"
    Write-Host "  powershell -ExecutionPolicy Bypass -File .\tools\device_maintenance.ps1 -Action probe -Port COMx"
    Write-Host "  powershell -ExecutionPolicy Bypass -File .\tools\device_maintenance.ps1 -Action check-flash -Port COMx"
    Write-Host ""
    Write-Host "Recovery / write:"
    Write-Host "  powershell -ExecutionPolicy Bypass -File .\tools\device_maintenance.ps1 -Action flash -Port COMx"
    Write-Host "  powershell -ExecutionPolicy Bypass -File .\tools\device_maintenance.ps1 -Action restore-bootloader -Port COMx"
    Write-Host "  powershell -ExecutionPolicy Bypass -File .\tools\device_maintenance.ps1 -Action erase-otadata -Port COMx"
    Write-Host "  powershell -ExecutionPolicy Bypass -File .\tools\device_maintenance.ps1 -Action erase-flash -Port COMx -ConfirmEraseFlash"
    Write-Host ""
    Write-Host "COMx resolves to the only present ESP32-S3 USB serial port."
}

function Get-ShortBuildDir {
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
                $portName = $null
                foreach ($match in [regex]::Matches($friendly, '(?i)\bCOM\d+\b')) {
                    $portName = $match.Value.ToUpperInvariant()
                    break
                }
                if ($portName) {
                    [pscustomobject]@{
                        Port = $portName
                        FriendlyName = $friendly
                        InstanceId = [string]$_.InstanceId
                        Source = "pnp"
                    }
                }
            })
        } catch {
            $records += @()
        }
    }

    if ($records.Count -eq 0 -and (Get-Command Get-CimInstance -ErrorAction SilentlyContinue)) {
        try {
            $records += @(Get-CimInstance Win32_SerialPort -ErrorAction Stop | ForEach-Object {
                if ([string]$_.DeviceID -match '^(?i)COM\d+$') {
                    [pscustomobject]@{
                        Port = ([string]$_.DeviceID).ToUpperInvariant()
                        FriendlyName = [string]$_.Name
                        InstanceId = [string]$_.PNPDeviceID
                        Source = "cim"
                    }
                }
            })
        } catch {
            $records += @()
        }
    }

    try {
        foreach ($serialPort in [System.IO.Ports.SerialPort]::GetPortNames()) {
            if ([string]$serialPort -match '^(?i)COM\d+$') {
                $records += [pscustomobject]@{
                    Port = ([string]$serialPort).ToUpperInvariant()
                    FriendlyName = "System.IO.Ports"
                    InstanceId = ""
                    Source = "serialport"
                }
            }
        }
    } catch {
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
    return @($unique | Sort-Object Port)
}

function Resolve-MaintenancePort {
    $requested = ([string]$Port).Trim()
    if ($requested -notmatch '^(?i)COMx$') {
        if ($requested -notmatch '^(?i)COM\d+$') {
            throw "Invalid port '$Port'. Use COM7, COMx, or omit -Port for auto-detect."
        }
        return $requested.ToUpperInvariant()
    }

    $ports = @(Get-PresentSerialPorts)
    if ($ports.Count -eq 0) {
        throw "No serial ports were detected. Connect the ESP32-S3 board, then retry."
    }

    $espPorts = @($ports | Where-Object {
        [string]$_.InstanceId -match 'VID_303A&PID_1001' -or
        [string]$_.FriendlyName -match '(?i)ESP32|USB JTAG|USB-Serial|serial debug'
    })
    if ($espPorts.Count -eq 1) {
        Write-Host "Resolved COMx to $($espPorts[0].Port)."
        return ([string]$espPorts[0].Port).ToUpperInvariant()
    }
    if ($ports.Count -eq 1) {
        Write-Host "Resolved COMx to $($ports[0].Port) (only serial port present)."
        return ([string]$ports[0].Port).ToUpperInvariant()
    }

    $summary = @($ports | ForEach-Object { "$($_.Port): $($_.FriendlyName)" }) -join "; "
    throw "Multiple serial ports were detected. Pass -Port explicitly. Ports: $summary"
}

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory = $true)][string]$File,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    Write-Host ("> {0} {1}" -f $File, ($Arguments -join " "))
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$File $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

function Invoke-LoggedCommand {
    param(
        [Parameter(Mandatory = $true)][string]$File,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$LogPath
    )

    Add-Content -LiteralPath $LogPath -Value ("> {0} {1}" -f $File, ($Arguments -join " "))
    $output = @(& $File @Arguments 2>&1)
    $exitCode = $LASTEXITCODE
    foreach ($line in $output) {
        Write-Host $line
        Add-Content -LiteralPath $LogPath -Value ([string]$line)
    }
    Add-Content -LiteralPath $LogPath -Value ("exit_code={0}" -f $exitCode)
    if ($exitCode -ne 0) {
        throw "$File $($Arguments -join ' ') failed with exit code $exitCode"
    }
}

function Enter-IdfEnvironment {
    . (Join-Path $PSScriptRoot "idf_env.ps1") -Target $Target
}

function Get-EsptoolPath {
    if ([string]::IsNullOrWhiteSpace($env:IDF_PATH)) {
        throw "IDF_PATH is not set. The ESP-IDF environment was not activated."
    }
    $path = Join-Path $env:IDF_PATH "components\esptool_py\esptool\esptool.py"
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "esptool.py not found at $path"
    }
    return $path
}

function Invoke-Esptool {
    param(
        [Parameter(Mandatory = $true)][string]$ResolvedPort,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$LogPath
    )

    $python = (Get-Command python -ErrorAction Stop).Path
    $esptool = Get-EsptoolPath
    $common = @($esptool, "--chip", $Target, "-p", $ResolvedPort, "-b", [string]$Baud)
    Invoke-LoggedCommand -File $python -Arguments ($common + $Arguments) -LogPath $LogPath
}

function Get-FirstByteHex {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $null
    }
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -eq 0) {
        return $null
    }
    return ("0x{0:X2}" -f $bytes[0])
}

function Read-FlashRegion {
    param(
        [Parameter(Mandatory = $true)][string]$ResolvedPort,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Offset,
        [Parameter(Mandatory = $true)][string]$Size,
        [Parameter(Mandatory = $true)][string]$Directory,
        [Parameter(Mandatory = $true)][string]$LogPath
    )

    $filePath = Join-Path $Directory ("{0}-{1}-{2}.bin" -f $Name, $Offset, $Size)
    Invoke-Esptool -ResolvedPort $ResolvedPort -LogPath $LogPath -Arguments @(
        "--before", "default_reset",
        "--after", "hard_reset",
        "read_flash",
        $Offset,
        $Size,
        $filePath
    )
    $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $filePath
    $item = Get-Item -LiteralPath $filePath
    return [ordered]@{
        name = $Name
        offset = $Offset
        size = $Size
        file = $filePath
        bytes_read = $item.Length
        sha256 = $hash.Hash.ToLowerInvariant()
        first_byte = Get-FirstByteHex -Path $filePath
    }
}

function Invoke-Ports {
    $ports = @(Get-PresentSerialPorts)
    if ($ports.Count -eq 0) {
        Write-Host "No serial ports detected."
        return
    }
    $ports | Format-Table Port, FriendlyName, InstanceId, Source -AutoSize
}

function Invoke-Probe {
    $resolvedPort = Resolve-MaintenancePort
    Enter-IdfEnvironment
    $runDir = New-RunDirectory -Prefix "probe"
    $logPath = Join-Path $runDir "probe.log"

    Invoke-Esptool -ResolvedPort $resolvedPort -LogPath $logPath -Arguments @("--before", "default_reset", "--after", "hard_reset", "chip_id")
    Invoke-Esptool -ResolvedPort $resolvedPort -LogPath $logPath -Arguments @("--before", "default_reset", "--after", "hard_reset", "flash_id")
    Invoke-Esptool -ResolvedPort $resolvedPort -LogPath $logPath -Arguments @("--before", "default_reset", "--after", "hard_reset", "read_mac")

    Write-Host "Probe log: $logPath"
}

function New-RunDirectory {
    param([Parameter(Mandatory = $true)][string]$Prefix)

    $root = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    $runDir = Join-Path $root ("{0}-{1}" -f $Prefix, (Get-Date -Format "yyyyMMdd-HHmmss"))
    New-Item -ItemType Directory -Force -Path $runDir | Out-Null
    return $runDir
}

function Invoke-CheckFlash {
    $resolvedPort = Resolve-MaintenancePort
    Enter-IdfEnvironment
    $runDir = New-RunDirectory -Prefix "flash-check"
    $logPath = Join-Path $runDir "esptool.log"

    Invoke-Esptool -ResolvedPort $resolvedPort -LogPath $logPath -Arguments @("--before", "default_reset", "--after", "hard_reset", "flash_id")
    Invoke-Esptool -ResolvedPort $resolvedPort -LogPath $logPath -Arguments @("--before", "default_reset", "--after", "hard_reset", "read_mac")

    $regions = @()
    $regions += Read-FlashRegion -ResolvedPort $resolvedPort -Name "bootloader-head" -Offset "0x0" -Size "0x1000" -Directory $runDir -LogPath $logPath
    $regions += Read-FlashRegion -ResolvedPort $resolvedPort -Name "partition-table" -Offset "0x8000" -Size "0x1000" -Directory $runDir -LogPath $logPath
    $regions += Read-FlashRegion -ResolvedPort $resolvedPort -Name "otadata" -Offset "0xf000" -Size "0x2000" -Directory $runDir -LogPath $logPath
    $regions += Read-FlashRegion -ResolvedPort $resolvedPort -Name "ota0-head" -Offset "0x20000" -Size "0x1000" -Directory $runDir -LogPath $logPath
    $regions += Read-FlashRegion -ResolvedPort $resolvedPort -Name "ota1-head" -Offset "0x620000" -Size "0x1000" -Directory $runDir -LogPath $logPath

    $summary = [ordered]@{
        created_at = (Get-Date).ToString("o")
        port = $resolvedPort
        target = $Target
        baud = $Baud
        output_dir = $runDir
        regions = $regions
        notes = @(
            "ESP image regions normally start with first_byte 0xE9.",
            "This check reads only small headers and does not modify flash."
        )
    }
    $summaryPath = Join-Path $runDir "summary.json"
    $summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryPath -Encoding utf8

    Write-Host "Flash check summary: $summaryPath"
    $regions | Format-Table name, offset, bytes_read, first_byte, sha256 -AutoSize
}

function Invoke-FlashFirmware {
    $commandArgs = @(
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "flash.ps1"),
        "-Port", $Port,
        "-Target", $Target
    )
    if (-not [string]::IsNullOrWhiteSpace($BuildDir)) {
        $commandArgs += @("-BuildDir", $BuildDir)
    }
    if ($NoBuild) {
        $commandArgs += "-NoBuild"
    }
    if ($PreserveOtaData) {
        $commandArgs += "-PreserveOtaData"
    }
    Invoke-CheckedCommand -File "powershell" -Arguments $commandArgs
}

function Invoke-RestoreBootloader {
    $commandArgs = @(
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "flash_bootloader.ps1"),
        "-Port", $Port,
        "-Target", $Target
    )
    if (-not [string]::IsNullOrWhiteSpace($BuildDir)) {
        $commandArgs += @("-BuildDir", $BuildDir)
    }
    Invoke-CheckedCommand -File "powershell" -Arguments $commandArgs
}

function Invoke-EraseOtaData {
    $resolvedPort = Resolve-MaintenancePort
    $buildDirResolved = Get-ShortBuildDir
    if (-not $NoBuild) {
        Invoke-CheckedCommand -File "powershell" -Arguments @(
            "-ExecutionPolicy", "Bypass",
            "-File", (Join-Path $PSScriptRoot "build.ps1"),
            "-Target", $Target,
            "-BuildDir", $buildDirResolved
        )
    }
    Enter-IdfEnvironment
    Invoke-CheckedCommand -File "idf.py" -Arguments @("-B", $buildDirResolved, "-p", $resolvedPort, "erase-otadata")
}

function Invoke-EraseFlash {
    if (-not $ConfirmEraseFlash) {
        throw "erase-flash removes the whole flash. Re-run with -ConfirmEraseFlash if this is intended."
    }

    $resolvedPort = Resolve-MaintenancePort
    Enter-IdfEnvironment
    $runDir = New-RunDirectory -Prefix "erase-flash"
    $logPath = Join-Path $runDir "erase-flash.log"
    Invoke-Esptool -ResolvedPort $resolvedPort -LogPath $logPath -Arguments @("--before", "default_reset", "--after", "hard_reset", "erase_flash")
    Write-Host "Erase flash log: $logPath"
}

switch ($Action.ToLowerInvariant()) {
    "help" { Show-Usage }
    "ports" { Invoke-Ports }
    "probe" { Invoke-Probe }
    "check-flash" { Invoke-CheckFlash }
    "flash" { Invoke-FlashFirmware }
    "restore-bootloader" { Invoke-RestoreBootloader }
    "erase-otadata" { Invoke-EraseOtaData }
    "erase-flash" { Invoke-EraseFlash }
}
