[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$DeviceName = "listener",
    [string]$BluetoothAddress = "",
    [string]$SerialPort = "",
    [int]$SerialBaud = 115200,
    [switch]$FirmwareRecovery = $false,
    [switch]$RemoveWindowsDeviceCache = $false,
    [switch]$StopListenerType = $false,
    [switch]$OpenBluetoothSettings = $false,
    [switch]$ProbeOtaGatt = $false,
    [int]$MaintainConnectionDurationSeconds = 12,
    [int]$PollIntervalSeconds = 2
)

$ErrorActionPreference = "Stop"

function Normalize-BluetoothAddress {
    param([string]$Address)

    $normalized = ($Address -replace "[^0-9A-Fa-f]", "").ToUpperInvariant()
    if ([string]::IsNullOrWhiteSpace($normalized)) {
        return ""
    }
    if ($normalized.Length -ne 12) {
        throw "Bluetooth address must contain exactly 12 hex digits. Current value: $Address"
    }
    return $normalized
}

function Resolve-BluetoothAddress {
    param(
        [string]$PreferredName,
        [string]$PreferredAddress
    )

    $normalized = Normalize-BluetoothAddress -Address $PreferredAddress
    if (-not [string]::IsNullOrWhiteSpace($normalized)) {
        return $normalized
    }

    $device = Get-PnpDevice -Class Bluetooth -ErrorAction SilentlyContinue |
        Where-Object { $_.FriendlyName -eq $PreferredName -and $_.InstanceId -match "DEV_([0-9A-Fa-f]{12})" } |
        Select-Object -First 1
    if ($null -eq $device) {
        return ""
    }
    return $Matches[1].ToUpperInvariant()
}

function Stop-ListenerTypeProcesses {
    $processes = Get-Process -ErrorAction SilentlyContinue |
        Where-Object {
            $_.ProcessName -like "Listener*" -or
            $_.ProcessName -eq "listener-type" -or
            $_.Path -like "*Listener Type*"
        }

    foreach ($process in $processes) {
        Write-Host "reset_listener_ble_host: stopping process pid=$($process.Id) name=$($process.ProcessName)"
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
}

function Send-FirmwareRecovery {
    param(
        [Parameter(Mandatory = $true)][string]$PortName,
        [int]$BaudRate
    )

    Write-Host "reset_listener_ble_host: sending firmware recovery over $PortName"
    $port = [System.IO.Ports.SerialPort]::new(
        $PortName,
        $BaudRate,
        [System.IO.Ports.Parity]::None,
        8,
        [System.IO.Ports.StopBits]::One)
    $port.ReadTimeout = 500
    $port.WriteTimeout = 1000
    $port.DtrEnable = $false
    $port.RtsEnable = $false

    try {
        $port.Open()
        Start-Sleep -Milliseconds 300
        $port.Write("~VREC:RECOVERY`n")
        Start-Sleep -Seconds 5

        $deadline = (Get-Date).AddSeconds(8)
        while ((Get-Date) -lt $deadline) {
            try {
                $line = $port.ReadLine()
                if (-not [string]::IsNullOrWhiteSpace($line)) {
                    Write-Host "firmware: $line"
                }
            } catch {
            }
        }
    } finally {
        if ($port.IsOpen) {
            $port.Close()
        }
        $port.Dispose()
    }
}

function Remove-WindowsBluetoothCache {
    param(
        [string]$PreferredName,
        [string]$Address
    )

    $normalized = Normalize-BluetoothAddress -Address $Address
    if ([string]::IsNullOrWhiteSpace($normalized)) {
        throw "RemoveWindowsDeviceCache requires BluetoothAddress or an existing PnP DEV_ address."
    }

    $targets = Get-PnpDevice -Class Bluetooth -ErrorAction SilentlyContinue |
        Where-Object {
            $_.InstanceId -like "*$normalized*" -or
            $_.FriendlyName -eq $PreferredName
        }

    if ($null -eq $targets -or @($targets).Count -eq 0) {
        Write-Warning "reset_listener_ble_host: no Bluetooth PnP nodes matched $PreferredName/$normalized"
        return
    }

    foreach ($device in $targets) {
        Write-Host "reset_listener_ble_host: removing PnP node status=$($device.Status) name=$($device.FriendlyName)"
        & pnputil.exe /remove-device "$($device.InstanceId)"
    }

    Write-Host "reset_listener_ble_host: scanning devices"
    & pnputil.exe /scan-devices | Out-Host
}

$restartScript = Join-Path $PSScriptRoot "restart_windows_bluetooth.ps1"
$recoverScript = Join-Path $PSScriptRoot "recover_ble_hid_host.ps1"
$probeScript = Join-Path $PSScriptRoot "probe_ble_ota_gatt.ps1"

$resolvedAddress = Resolve-BluetoothAddress -PreferredName $DeviceName -PreferredAddress $BluetoothAddress
Write-Host "reset_listener_ble_host: target name=$DeviceName addr=$resolvedAddress"

if ($StopListenerType) {
    Stop-ListenerTypeProcesses
}

if ($FirmwareRecovery) {
    if ([string]::IsNullOrWhiteSpace($SerialPort)) {
        throw "FirmwareRecovery requires -SerialPort, for example -SerialPort COM5."
    }
    Send-FirmwareRecovery -PortName $SerialPort -BaudRate $SerialBaud
}

Write-Host "reset_listener_ble_host: restarting Windows Bluetooth service"
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $restartScript

if ($RemoveWindowsDeviceCache) {
    Remove-WindowsBluetoothCache -PreferredName $DeviceName -Address $resolvedAddress
    Start-Sleep -Seconds 3
    Write-Host "reset_listener_ble_host: restarting Windows Bluetooth service after cache removal"
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $restartScript
}

if ($OpenBluetoothSettings) {
    Write-Host "reset_listener_ble_host: opening Windows Bluetooth settings for manual re-pair"
    Start-Process "ms-settings:bluetooth"
}

if (-not [string]::IsNullOrWhiteSpace($resolvedAddress)) {
    Write-Host "reset_listener_ble_host: requesting maintain-connection"
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $recoverScript `
        -DeviceName $DeviceName `
        -BluetoothAddress $resolvedAddress `
        -MaintainConnectionDurationSeconds $MaintainConnectionDurationSeconds `
        -PollIntervalSeconds $PollIntervalSeconds
} else {
    Write-Warning "reset_listener_ble_host: address unresolved; skipping maintain-connection"
}

if ($ProbeOtaGatt) {
    if ([string]::IsNullOrWhiteSpace($resolvedAddress)) {
        Write-Warning "reset_listener_ble_host: address unresolved; probing by device name only"
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $probeScript -DeviceName $DeviceName
    } else {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $probeScript -DeviceName $DeviceName -BluetoothAddress $resolvedAddress
    }
}
