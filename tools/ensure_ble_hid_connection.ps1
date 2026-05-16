[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$DeviceName = "listener",
    [string]$BluetoothAddress = "",
    [int]$DurationSeconds = 120,
    [int]$PollIntervalSeconds = 3,
    [switch]$ExitOnReady = $false
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Devices.Bluetooth.BluetoothLEDevice, Windows.Devices.Bluetooth, ContentType = WindowsRuntime]
$null = [Windows.Devices.Bluetooth.GenericAttributeProfile.GattSession, Windows.Devices.Bluetooth, ContentType = WindowsRuntime]
$null = [Windows.Devices.Bluetooth.BluetoothCacheMode, Windows.Devices.Bluetooth, ContentType = WindowsRuntime]
$null = [Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceServicesResult, Windows.Devices.Bluetooth, ContentType = WindowsRuntime]

function Invoke-WinRtAsync {
    param(
        [Parameter(Mandatory = $true)]
        [object]$AsyncOp,
        [Parameter(Mandatory = $true)]
        [Type]$ResultType
    )

    $method = [System.WindowsRuntimeSystemExtensions].GetMethods() |
        Where-Object {
            $_.Name -eq "AsTask" -and
            $_.IsGenericMethodDefinition -and
            $_.GetGenericArguments().Count -eq 1 -and
            $_.GetParameters().Count -eq 1
        } |
        Select-Object -First 1

    if ($null -eq $method) {
        throw "System.WindowsRuntimeSystemExtensions.AsTask<T>(IAsyncOperation<T>) not found."
    }

    $task = $method.MakeGenericMethod($ResultType).Invoke($null, @($AsyncOp))
    return $task.GetAwaiter().GetResult()
}

function Convert-HexAddressToUInt64 {
    param(
        [Parameter(Mandatory = $true)]
        [string]$HexAddress
    )

    $normalized = ($HexAddress -replace "[^0-9A-Fa-f]", "").ToUpperInvariant()
    if ($normalized.Length -ne 12) {
        throw "Bluetooth address must contain exactly 12 hex digits. Current value: $HexAddress"
    }

    return [UInt64]::Parse($normalized, [System.Globalization.NumberStyles]::HexNumber)
}

function Get-TargetBluetoothAddress {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PreferredName,
        [string]$PreferredAddress
    )

    if (-not [string]::IsNullOrWhiteSpace($PreferredAddress)) {
        return (Convert-HexAddressToUInt64 -HexAddress $PreferredAddress)
    }

    $device = Get-PnpDevice -Class Bluetooth |
        Where-Object { $_.FriendlyName -eq $PreferredName } |
        Select-Object -First 1

    if ($null -eq $device) {
        throw "Unable to find Bluetooth device named '$PreferredName' from Get-PnpDevice."
    }

    if ($device.InstanceId -notmatch "DEV_([0-9A-Fa-f]{12})") {
        throw "Unable to extract Bluetooth address from InstanceId: $($device.InstanceId)"
    }

    return (Convert-HexAddressToUInt64 -HexAddress $Matches[1])
}

function Open-BleDevice {
    param(
        [Parameter(Mandatory = $true)]
        [UInt64]$TargetAddress
    )

    $device = Invoke-WinRtAsync `
        -AsyncOp ([Windows.Devices.Bluetooth.BluetoothLEDevice]::FromBluetoothAddressAsync($TargetAddress)) `
        -ResultType ([Windows.Devices.Bluetooth.BluetoothLEDevice])

    if ($null -eq $device) {
        return $null
    }

    if ([string]::IsNullOrWhiteSpace($device.DeviceId)) {
        return $device
    }

    $device_by_id = Invoke-WinRtAsync `
        -AsyncOp ([Windows.Devices.Bluetooth.BluetoothLEDevice]::FromIdAsync($device.DeviceId)) `
        -ResultType ([Windows.Devices.Bluetooth.BluetoothLEDevice])

    if ($null -ne $device_by_id) {
        return $device_by_id
    }

    return $device
}

function Enable-GattMaintainConnection {
    param(
        [Parameter(Mandatory = $true)]
        [Windows.Devices.Bluetooth.BluetoothLEDevice]$Device,
        [ref]$ServiceHandles,
        [ref]$Sessions
    )

    $result = Invoke-WinRtAsync `
        -AsyncOp ($Device.GetGattServicesAsync([Windows.Devices.Bluetooth.BluetoothCacheMode]::Cached)) `
        -ResultType ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceServicesResult])

    $service_count = 0
    $session_count = 0

    if ($null -ne $result.Services) {
        foreach ($service in $result.Services) {
            $service_count += 1
            $ServiceHandles.Value += $service

            $session = $service.Session

            if ($null -eq $session) {
                continue
            }

            $session.MaintainConnection = $true
            $Sessions.Value += $session
            $session_count += 1
        }
    }

    return [PSCustomObject]@{
        CommunicationStatus = $result.Status
        ServiceCount        = $service_count
        SessionCount        = $session_count
    }
}

$target_address = Get-TargetBluetoothAddress -PreferredName $DeviceName -PreferredAddress $BluetoothAddress
$target_address_hex = "{0:X12}" -f $target_address
$deadline = (Get-Date).AddSeconds($DurationSeconds)
$service_handles = @()
$sessions = @()
$ready = $false

Write-Host "ensure_ble_hid_connection: target=$DeviceName addr=$target_address_hex"

while ((Get-Date) -lt $deadline) {
    try {
        $device = Open-BleDevice -TargetAddress $target_address
        if ($null -eq $device) {
            Write-Warning "ensure_ble_hid_connection: BluetoothLEDevice is null, retrying"
            Start-Sleep -Seconds $PollIntervalSeconds
            continue
        }

        $result = Enable-GattMaintainConnection -Device $device -ServiceHandles ([ref]$service_handles) -Sessions ([ref]$sessions)
        Write-Host (
            "ensure_ble_hid_connection: status={0} gatt={1} services={2} sessions={3}" -f
            $device.ConnectionStatus,
            $result.CommunicationStatus,
            $result.ServiceCount,
            $result.SessionCount)

        if ($result.SessionCount -gt 0) {
            $ready = $true
            if ($ExitOnReady) {
                break
            }
        }
    } catch {
        Write-Warning "ensure_ble_hid_connection: $($_.Exception.Message)"
    }

    Start-Sleep -Seconds $PollIntervalSeconds
}

if ($ready) {
    Write-Host "ensure_ble_hid_connection: maintain-connection session active"
    exit 0
}

Write-Warning "ensure_ble_hid_connection: unable to establish maintain-connection session before timeout"
exit 1
