param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [string]$Target = "esp32s3",
    [string]$BuildDir = $env:LISTENER_IDF_BUILD_DIR,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$project_root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "idf_env.ps1") -Target $Target

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

function Invoke-CheckedCommand {
    param(
        [string]$File,
        [string[]]$Arguments
    )

    & $File @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$File $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

function Get-Esp32UsbPnpDevices {
    if ($env:OS -ne "Windows_NT") {
        return @()
    }
    if (-not (Get-Command Get-PnpDevice -ErrorAction SilentlyContinue)) {
        return @()
    }

    try {
        return @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object {
            [string]$_.InstanceId -match '^USB\\VID_303A&PID_1001'
        })
    } catch {
        return @()
    }
}

function Repair-Esp32UsbPnpDevices {
    if ($env:OS -ne "Windows_NT") {
        return
    }
    if ($env:LISTENER_SKIP_ESP32_USB_AUTO_ENABLE -match '^(?i:1|true|yes|on)$') {
        return
    }
    if (-not (Get-Command Enable-PnpDevice -ErrorAction SilentlyContinue)) {
        return
    }

    $disabled = @(Get-Esp32UsbPnpDevices | Where-Object {
        [string]$_.Problem -eq "CM_PROB_DISABLED" -or [string]$_.Status -eq "Error"
    })
    if ($disabled.Count -eq 0) {
        return
    }

    foreach ($device in $disabled) {
        try {
            Write-Host "Enabling disabled ESP32-S3 USB device: $($device.FriendlyName) [$($device.InstanceId)]"
            $device | Enable-PnpDevice -Confirm:$false | Out-Null
        } catch {
            Write-Warning "Could not enable ESP32-S3 USB device '$($device.InstanceId)': $($_.Exception.Message)"
        }
    }
    Start-Sleep -Seconds 2
}

function Get-Esp32SerialPortsFromPnp {
    $ports = New-Object System.Collections.Generic.List[string]
    foreach ($device in @(Get-Esp32UsbPnpDevices | Where-Object { [string]$_.Class -eq "Ports" })) {
        $text = "$($device.FriendlyName) $($device.InstanceId)"
        foreach ($match in [regex]::Matches($text, '(?i)\bCOM\d+\b')) {
            $ports.Add($match.Value.ToUpperInvariant())
        }
    }
    return @($ports | Sort-Object -Unique)
}

function Test-SerialPortNamePresent {
    param([Parameter(Mandatory = $true)][string]$SerialPortName)

    $target = $SerialPortName.ToUpperInvariant()
    try {
        if (@([System.IO.Ports.SerialPort]::GetPortNames() | ForEach-Object { $_.ToUpperInvariant() }) -contains $target) {
            return $true
        }
    } catch {
    }
    return [bool](@(Get-Esp32SerialPortsFromPnp) -contains $target)
}

function Resolve-FlashPort {
    param([Parameter(Mandatory = $true)][string]$RequestedPort)

    Repair-Esp32UsbPnpDevices

    if ($RequestedPort -notmatch '^(?i:COMx)$') {
        if (-not (Test-SerialPortNamePresent -SerialPortName $RequestedPort)) {
            Repair-Esp32UsbPnpDevices
        }
        if (-not (Test-SerialPortNamePresent -SerialPortName $RequestedPort)) {
            $knownEsp32 = @(Get-Esp32SerialPortsFromPnp)
            $knownSerial = @([System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object)
            Write-Warning "Requested port '$RequestedPort' is not present. ESP32-like ports: $($knownEsp32 -join ', '); serial ports: $($knownSerial -join ', ')"
        }
        return $RequestedPort
    }

    $esp32Ports = @(Get-Esp32SerialPortsFromPnp)
    if ($esp32Ports.Count -eq 1) {
        Write-Host "Resolved COMx to '$($esp32Ports[0])' from ESP32-S3 USB PnP."
        return [string]$esp32Ports[0]
    }

    $serialPorts = @([System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object -Unique)
    if ($serialPorts.Count -eq 1) {
        Write-Host "Resolved COMx to '$($serialPorts[0])' from the only present serial port."
        return [string]$serialPorts[0]
    }

    throw "COMx requires exactly one ESP32-S3 serial device. ESP32-like ports: $($esp32Ports -join ', '); serial ports: $($serialPorts -join ', ')"
}

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDirResolved = Get-ShortBuildDir -ProjectRoot $projectRoot
$resolvedPort = Resolve-FlashPort -RequestedPort $Port

if (-not $NoBuild) {
    Invoke-CheckedCommand -File "powershell" -Arguments @(
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        (Join-Path $PSScriptRoot "build.ps1"),
        "-Target",
        $Target,
        "-BuildDir",
        $buildDirResolved
    )
}

Invoke-CheckedCommand -File "idf.py" -Arguments @("-B", $buildDirResolved, "-p", $resolvedPort, "flash")
