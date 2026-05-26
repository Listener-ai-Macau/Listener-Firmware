[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$DeviceName = "listener",
    [string]$BluetoothAddress = "",
    [Guid]$ServiceUuid = "710af845-6d9f-6583-0c4d-9e5b3bc3092a",
    [Guid]$ControlUuid = "710af845-6d9f-6583-0c4d-9e5b3bc3092b",
    [Guid]$DataUuid = "710af845-6d9f-6583-0c4d-9e5b3bc3092c",
    [int]$TimeoutSeconds = 20,
    [switch]$SkipCachedSessionPrime = $false
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Devices.Bluetooth.BluetoothLEDevice, Windows.Devices.Bluetooth, ContentType = WindowsRuntime]
$null = [Windows.Devices.Bluetooth.BluetoothCacheMode, Windows.Devices.Bluetooth, ContentType = WindowsRuntime]
$null = [Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceServicesResult, Windows.Devices.Bluetooth, ContentType = WindowsRuntime]
$null = [Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristicsResult, Windows.Devices.Bluetooth, ContentType = WindowsRuntime]

function Invoke-WinRtAsync {
    param(
        [Parameter(Mandatory = $true)]
        [object]$AsyncOp,
        [Parameter(Mandatory = $true)]
        [Type]$ResultType,
        [int]$TimeoutMs = 20000
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
    if (-not $task.Wait($TimeoutMs)) {
        throw "WinRT async operation timed out after $TimeoutMs ms."
    }
    return $task.GetAwaiter().GetResult()
}

function Convert-HexAddressToUInt64 {
    param([Parameter(Mandatory = $true)][string]$HexAddress)

    $normalized = ($HexAddress -replace "[^0-9A-Fa-f]", "").ToUpperInvariant()
    if ($normalized.Length -ne 12) {
        throw "Bluetooth address must contain exactly 12 hex digits. Current value: $HexAddress"
    }
    return [UInt64]::Parse($normalized, [System.Globalization.NumberStyles]::HexNumber)
}

function Get-TargetBluetoothAddress {
    param([string]$PreferredName, [string]$PreferredAddress)

    if (-not [string]::IsNullOrWhiteSpace($PreferredAddress)) {
        return (Convert-HexAddressToUInt64 -HexAddress $PreferredAddress)
    }

    $device = Get-PnpDevice -Class Bluetooth |
        Where-Object { $_.FriendlyName -eq $PreferredName } |
        Select-Object -First 1
    if ($null -eq $device -or $device.InstanceId -notmatch "DEV_([0-9A-Fa-f]{12})") {
        throw "Unable to find Bluetooth device named '$PreferredName' with a DEV_ address."
    }
    return (Convert-HexAddressToUInt64 -HexAddress $Matches[1])
}

function Get-FirstCharacteristicByUuid {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Service,
        [Parameter(Mandatory = $true)]
        [Guid]$Uuid
    )

    $result = Invoke-WinRtAsync `
        -AsyncOp ($Service.GetCharacteristicsForUuidAsync($Uuid, [Windows.Devices.Bluetooth.BluetoothCacheMode]::Uncached)) `
        -ResultType ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristicsResult])

    if ($result.Status -ne "Success" -or $null -eq $result.Characteristics -or $result.Characteristics.Count -lt 1) {
        throw "Characteristic $Uuid discovery failed: $($result.Status)"
    }
    return $result.Characteristics[0]
}

$address = Get-TargetBluetoothAddress -PreferredName $DeviceName -PreferredAddress $BluetoothAddress
$device = Invoke-WinRtAsync `
    -AsyncOp ([Windows.Devices.Bluetooth.BluetoothLEDevice]::FromBluetoothAddressAsync($address)) `
    -ResultType ([Windows.Devices.Bluetooth.BluetoothLEDevice]) `
    -TimeoutMs ($TimeoutSeconds * 1000)
if ($null -eq $device) {
    throw "BluetoothLEDevice.FromBluetoothAddressAsync returned null."
}

$heldServices = @()
$heldSessions = @()
if (-not $SkipCachedSessionPrime.IsPresent) {
    $cachedServices = Invoke-WinRtAsync `
        -AsyncOp ($device.GetGattServicesAsync([Windows.Devices.Bluetooth.BluetoothCacheMode]::Cached)) `
        -ResultType ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceServicesResult]) `
        -TimeoutMs ($TimeoutSeconds * 1000)
    if ($cachedServices.Status -eq "Success" -and $null -ne $cachedServices.Services) {
        foreach ($cachedService in $cachedServices.Services) {
            $heldServices += $cachedService
            if ($null -ne $cachedService.Session) {
                $cachedService.Session.MaintainConnection = $true
                $heldSessions += $cachedService.Session
            }
        }
    }

    $connectDeadline = (Get-Date).AddSeconds([Math]::Min($TimeoutSeconds, 10))
    while ($device.ConnectionStatus.ToString() -ne "Connected" -and (Get-Date) -lt $connectDeadline) {
        Start-Sleep -Milliseconds 250
    }
}

$serviceResult = Invoke-WinRtAsync `
    -AsyncOp ($device.GetGattServicesForUuidAsync($ServiceUuid, [Windows.Devices.Bluetooth.BluetoothCacheMode]::Uncached)) `
    -ResultType ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceServicesResult]) `
    -TimeoutMs ($TimeoutSeconds * 1000)
if ($serviceResult.Status -ne "Success" -or $null -eq $serviceResult.Services -or $serviceResult.Services.Count -lt 1) {
    throw "OTA service $ServiceUuid discovery failed: $($serviceResult.Status)"
}

$service = $serviceResult.Services[0]
$control = Get-FirstCharacteristicByUuid -Service $service -Uuid $ControlUuid
$data = Get-FirstCharacteristicByUuid -Service $service -Uuid $DataUuid

$controlProps = $control.CharacteristicProperties.ToString()
$dataProps = $data.CharacteristicProperties.ToString()
if ($controlProps -notmatch "Write") {
    throw "Control characteristic lacks Write property: $controlProps"
}
if ($dataProps -notmatch "Write") {
    throw "Data characteristic lacks Write property: $dataProps"
}
if ($dataProps -notmatch "WriteWithoutResponse") {
    throw "Data characteristic lacks WriteWithoutResponse property: $dataProps"
}

[PSCustomObject]@{
    status = "PASS"
    device_name = $device.Name
    bluetooth_address = ("{0:X12}" -f $address)
    connection_status = $device.ConnectionStatus.ToString()
    primed_cached_sessions = $heldSessions.Count
    service_uuid = $ServiceUuid.ToString()
    control_uuid = $ControlUuid.ToString()
    control_properties = $controlProps
    data_uuid = $DataUuid.ToString()
    data_properties = $dataProps
} | ConvertTo-Json -Depth 3
