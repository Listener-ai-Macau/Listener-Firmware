[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$DeviceName = "listener",
    [string]$BluetoothAddress = "",
    [int]$DurationSeconds = 120,
    [int]$PollIntervalSeconds = 3,
    [switch]$ExitOnReady = $false
)

$ErrorActionPreference = "Stop"

function Test-WinRtBleProjection {
    try {
        $null = [Windows.Devices.Bluetooth.BluetoothLEDevice]
        $null = [Windows.Devices.Bluetooth.GenericAttributeProfile.GattSession]
        $null = [Windows.Devices.Bluetooth.BluetoothCacheMode]
        $null = [Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceServicesResult]
        return $true
    } catch {
        return $false
    }
}

function Initialize-WinRtBleProjection {
    if (Test-WinRtBleProjection) {
        return
    }

    $packageRoot = Join-Path $env:USERPROFILE ".nuget\packages\microsoft.windows.sdk.net.ref"
    if (Test-Path -LiteralPath $packageRoot) {
        $candidate = Get-ChildItem -LiteralPath $packageRoot -Directory |
            Sort-Object Name -Descending |
            ForEach-Object {
                Get-ChildItem -LiteralPath $_.FullName -Recurse -Filter "Microsoft.Windows.SDK.NET.dll" -ErrorAction SilentlyContinue |
                    Where-Object { $_.FullName -match "\\lib\\net[0-9.]+" } |
                    Select-Object -First 1
            } |
            Where-Object { $null -ne $_ } |
            Select-Object -First 1
        if ($null -ne $candidate) {
            $winRtRuntime = Join-Path (Split-Path -Parent $candidate.FullName) "WinRT.Runtime.dll"
            if (Test-Path -LiteralPath $winRtRuntime) {
                Add-Type -Path $winRtRuntime
                Add-Type -Path $candidate.FullName
            }
        }
    }

    if (-not (Test-WinRtBleProjection)) {
        try {
            Add-Type -AssemblyName System.Runtime.WindowsRuntime -ErrorAction SilentlyContinue
        } catch {
        }
    }

    if (-not (Test-WinRtBleProjection)) {
        throw "Unable to load WinRT BLE projection in pwsh. Install/restore Microsoft.Windows.SDK.NET.Ref or run setup so Microsoft.Windows.SDK.NET.dll and WinRT.Runtime.dll are available under `$env:USERPROFILE\.nuget\packages."
    }
}

Initialize-WinRtBleProjection

function Invoke-WinRtAsync {
    param(
        [Parameter(Mandatory = $true)]
        [object]$AsyncOp,
        [Parameter(Mandatory = $true)]
        [Type]$ResultType
    )

    $methods = @()
    foreach ($candidate in [System.WindowsRuntimeSystemExtensions].GetMethods()) {
        if ($candidate.Name -ne "AsTask" -or -not $candidate.IsGenericMethodDefinition) {
            continue
        }
        if ($candidate.GetGenericArguments().Count -ne 1) {
            continue
        }
        try {
            $parameters = $candidate.GetParameters()
        } catch {
            continue
        }
        if ($parameters.Count -ne 1) {
            continue
        }
        $methods += $candidate
    }

    if ($methods.Count -eq 0) {
        throw "System.WindowsRuntimeSystemExtensions.AsTask<T>(IAsyncOperation<T>) not found."
    }

    $last_error = $null
    foreach ($method in $methods) {
        try {
            $task = $method.MakeGenericMethod($ResultType).Invoke($null, @($AsyncOp))
            return $task.GetAwaiter().GetResult()
        } catch {
            $last_error = $_.Exception.Message
        }
    }

    throw "System.WindowsRuntimeSystemExtensions.AsTask<T> could not consume async operation: $last_error"
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
    $active_session_count = 0
    $session_statuses = @()

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
            try {
                $session_status = $session.SessionStatus
                $session_statuses += ([string]$session_status)
                if ($session_status -eq [Windows.Devices.Bluetooth.GenericAttributeProfile.GattSessionStatus]::Active) {
                    $active_session_count += 1
                }
            } catch {
                $session_statuses += "Unknown"
            }
        }
    }

    return [PSCustomObject]@{
        CommunicationStatus = $result.Status
        ServiceCount        = $service_count
        SessionCount        = $session_count
        ActiveSessionCount  = $active_session_count
        SessionStatuses     = $session_statuses
    }
}

$target_address = Get-TargetBluetoothAddress -PreferredName $DeviceName -PreferredAddress $BluetoothAddress
$target_address_hex = "{0:X12}" -f $target_address
$deadline = (Get-Date).AddSeconds($DurationSeconds)
$service_handles = @()
$sessions = @()
$ready = $false
$last_status = $null

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
        $connection_ready = $device.ConnectionStatus -eq [Windows.Devices.Bluetooth.BluetoothConnectionStatus]::Connected
        $session_ready = $result.ActiveSessionCount -gt 0
        $last_status = [PSCustomObject]@{
            ConnectionStatus = [string]$device.ConnectionStatus
            GattStatus       = [string]$result.CommunicationStatus
            Services         = $result.ServiceCount
            Sessions         = $result.SessionCount
            ActiveSessions   = $result.ActiveSessionCount
            SessionStatuses  = ($result.SessionStatuses -join ",")
        }
        Write-Host (
            "ensure_ble_hid_connection: status={0} gatt={1} services={2} sessions={3} active_sessions={4} session_statuses={5}" -f
            $device.ConnectionStatus,
            $result.CommunicationStatus,
            $result.ServiceCount,
            $result.SessionCount,
            $result.ActiveSessionCount,
            ($result.SessionStatuses -join ","))

        if ($connection_ready -and $session_ready) {
            $ready = $true
            if ($ExitOnReady) {
                break
            }
        } elseif ($result.SessionCount -gt 0) {
            Write-Warning (
                "ensure_ble_hid_connection: GATT services are cached but device is not connected/active yet; status={0} active_sessions={1}" -f
                $device.ConnectionStatus,
                $result.ActiveSessionCount
            )
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

if ($null -ne $last_status) {
    Write-Warning (
        "ensure_ble_hid_connection: unable to establish active maintain-connection session before timeout; last_status={0} gatt={1} services={2} sessions={3} active_sessions={4} session_statuses={5}" -f
        $last_status.ConnectionStatus,
        $last_status.GattStatus,
        $last_status.Services,
        $last_status.Sessions,
        $last_status.ActiveSessions,
        $last_status.SessionStatuses
    )
} else {
    Write-Warning "ensure_ble_hid_connection: unable to establish active maintain-connection session before timeout"
}
exit 1
