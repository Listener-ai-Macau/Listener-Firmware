[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$DeviceName = "listener",
    [string]$BluetoothAddress = "",
    [Guid]$ServiceUuid = "710af845-6d9f-6583-0c4d-9e5b3bc3092a",
    [Guid]$ControlUuid = "710af845-6d9f-6583-0c4d-9e5b3bc3092b",
    [Guid]$DataUuid = "710af845-6d9f-6583-0c4d-9e5b3bc3092c",
    [Guid]$ReadinessUuid = "710af845-6d9f-6583-0c4d-9e5b3bc3091c",
    [Guid]$CapabilitiesUuid = "710af845-6d9f-6583-0c4d-9e5b3bc3091d",
    [Guid]$DeviceInformationUuid = "0000180a-0000-1000-8000-00805f9b34fb",
    [Guid]$DisModelNumberUuid = "00002a24-0000-1000-8000-00805f9b34fb",
    [Guid]$DisFirmwareRevisionUuid = "00002a26-0000-1000-8000-00805f9b34fb",
    [Guid]$DisHardwareRevisionUuid = "00002a27-0000-1000-8000-00805f9b34fb",
    [int]$TimeoutSeconds = 20
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Devices.Bluetooth.BluetoothLEDevice, Windows.Devices.Bluetooth, ContentType = WindowsRuntime]
$null = [Windows.Devices.Bluetooth.BluetoothCacheMode, Windows.Devices.Bluetooth, ContentType = WindowsRuntime]
$null = [Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceService, Windows.Devices.Bluetooth, ContentType = WindowsRuntime]
$null = [Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceServicesResult, Windows.Devices.Bluetooth, ContentType = WindowsRuntime]
$null = [Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristicsResult, Windows.Devices.Bluetooth, ContentType = WindowsRuntime]
$null = [Windows.Devices.Bluetooth.GenericAttributeProfile.GattReadResult, Windows.Devices.Bluetooth, ContentType = WindowsRuntime]
$null = [Windows.Devices.Enumeration.DeviceInformation, Windows.Devices.Enumeration, ContentType = WindowsRuntime]
$null = [Windows.Security.Cryptography.CryptographicBuffer, Windows.Security.Cryptography, ContentType = WindowsRuntime]
$null = [Windows.Security.Cryptography.BinaryStringEncoding, Windows.Security.Cryptography, ContentType = WindowsRuntime]
$null = [Windows.Storage.Streams.DataReader, Windows.Storage.Streams, ContentType = WindowsRuntime]

function Invoke-WinRtAsync {
    param(
        [Parameter(Mandatory = $true)][object]$AsyncOp,
        [Parameter(Mandatory = $true)][Type]$ResultType,
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
        Where-Object { $_.FriendlyName -eq $PreferredName -and $_.InstanceId -match "DEV_([0-9A-Fa-f]{12})" } |
        Select-Object -First 1
    if ($null -eq $device) {
        throw "Unable to find Bluetooth device named '$PreferredName' with a DEV_ address."
    }
    return (Convert-HexAddressToUInt64 -HexAddress $Matches[1])
}

function Get-CollectionItems {
    param([object]$Collection)

    if ($null -eq $Collection) {
        return @()
    }
    if ($Collection -is [System.Array]) {
        return @($Collection)
    }
    try {
        $size = [int]$Collection.Size()
        $items = @()
        for ($i = 0; $i -lt $size; $i++) {
            $items += $Collection.GetAt($i)
        }
        return $items
    } catch {
        return @($Collection)
    }
}

function Convert-BufferToBytes {
    param([object]$Buffer)

    if ($null -eq $Buffer) {
        return [byte[]]@()
    }
    $length = 0
    try {
        $length = [int]$Buffer.Length
    } catch {
    }
    if ($length -le 0) {
        return [byte[]]@()
    }

    try {
        return [System.Runtime.InteropServices.WindowsRuntime.WindowsRuntimeBufferExtensions]::ToArray(
            $Buffer,
            [uint32]0,
            $length)
    } catch {
    }

    try {
        [byte[]]$bytes = New-Object byte[] $length
        [System.Runtime.InteropServices.WindowsRuntime.WindowsRuntimeBufferExtensions]::CopyTo($Buffer, $bytes)
        return $bytes
    } catch {
    }

    try {
        [byte[]]$bytes = $null
        [Windows.Security.Cryptography.CryptographicBuffer]::CopyToByteArray($Buffer, [ref]$bytes)
        if ($null -ne $bytes) {
            return $bytes
        }
    } catch {
    }

    try {
        $reader = [Windows.Storage.Streams.DataReader]::FromBuffer($Buffer)
        [byte[]]$bytes = New-Object byte[] ([int]$reader.UnconsumedBufferLength)
        $reader.ReadBytes($bytes)
        $reader.Dispose()
        return $bytes
    } catch {
    }

    return [byte[]]@()
}

function Convert-BufferToUtf8FromBuffer {
    param([object]$Buffer)

    if ($null -eq $Buffer) {
        return ""
    }
    try {
        return [Windows.Security.Cryptography.CryptographicBuffer]::ConvertBinaryToString(
            [Windows.Security.Cryptography.BinaryStringEncoding]::Utf8,
            $Buffer)
    } catch {
        return ""
    }
}

function Convert-BufferToUtf8 {
    param([byte[]]$Bytes)

    if ($null -eq $Bytes -or $Bytes.Length -le 0) {
        return ""
    }
    return [System.Text.Encoding]::UTF8.GetString($Bytes)
}

function Read-CharacteristicProbe {
    param(
        [Parameter(Mandatory = $true)][object]$Service,
        [Parameter(Mandatory = $true)][Guid]$Uuid,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $results = @()
    foreach ($mode in @(
        [Windows.Devices.Bluetooth.BluetoothCacheMode]::Uncached,
        [Windows.Devices.Bluetooth.BluetoothCacheMode]::Cached
    )) {
        $entry = [ordered]@{
            label = $Label
            uuid = $Uuid.ToString()
            cache_mode = $mode.ToString()
            characteristic_status = $null
            characteristic_count = 0
            properties = $null
            read_status = $null
            protocol_error = $null
            value_length = 0
            value_utf8 = ""
            error = $null
        }
        try {
            $charResult = Invoke-WinRtAsync `
                -AsyncOp ($Service.GetCharacteristicsForUuidAsync($Uuid, $mode)) `
                -ResultType ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristicsResult]) `
                -TimeoutMs ($TimeoutSeconds * 1000)
            $entry.characteristic_status = $charResult.Status.ToString()
            $chars = @(Get-CollectionItems -Collection $charResult.Characteristics)
            $entry.characteristic_count = $chars.Count
            if ($entry.characteristic_status -eq "Success" -and $chars.Count -gt 0) {
                $char = $chars[0]
                $entry.properties = $char.CharacteristicProperties.ToString()
                $read = Invoke-WinRtAsync `
                    -AsyncOp ($char.ReadValueAsync($mode)) `
                    -ResultType ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattReadResult]) `
                    -TimeoutMs ($TimeoutSeconds * 1000)
                $entry.read_status = $read.Status.ToString()
                if ($read.ProtocolError) {
                    $entry.protocol_error = [int]$read.ProtocolError
                }
                if ($read.Value) {
                    [byte[]]$bytes = Convert-BufferToBytes -Buffer $read.Value
                    $entry.value_length = $bytes.Length
                    $entry.value_utf8 = Convert-BufferToUtf8 -Bytes $bytes
                    if ([string]::IsNullOrWhiteSpace($entry.value_utf8)) {
                        $entry.value_utf8 = Convert-BufferToUtf8FromBuffer -Buffer $read.Value
                        if (-not [string]::IsNullOrWhiteSpace($entry.value_utf8)) {
                            $entry.value_length = [Text.Encoding]::UTF8.GetByteCount($entry.value_utf8)
                        }
                    }
                }
            }
        } catch {
            $entry.error = $_.Exception.Message
        }
        $results += [pscustomobject]$entry
    }
    return $results
}

function Read-DeviceCharacteristicProbe {
    param(
        [Parameter(Mandatory = $true)][object]$Device,
        [Parameter(Mandatory = $true)][Guid]$ServiceUuid,
        [Parameter(Mandatory = $true)][Guid]$CharacteristicUuid,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $results = @()
    foreach ($mode in @(
        [Windows.Devices.Bluetooth.BluetoothCacheMode]::Uncached,
        [Windows.Devices.Bluetooth.BluetoothCacheMode]::Cached
    )) {
        $entry = [ordered]@{
            label = $Label
            service_uuid = $ServiceUuid.ToString()
            uuid = $CharacteristicUuid.ToString()
            cache_mode = $mode.ToString()
            service_status = $null
            service_count = 0
            characteristic_status = $null
            characteristic_count = 0
            properties = $null
            read_status = $null
            protocol_error = $null
            value_length = 0
            value_utf8 = ""
            error = $null
        }
        try {
            $serviceResult = Invoke-WinRtAsync `
                -AsyncOp ($Device.GetGattServicesForUuidAsync($ServiceUuid, $mode)) `
                -ResultType ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceServicesResult]) `
                -TimeoutMs ($TimeoutSeconds * 1000)
            $entry.service_status = $serviceResult.Status.ToString()
            $services = @(Get-CollectionItems -Collection $serviceResult.Services)
            $entry.service_count = $services.Count
            if ($entry.service_status -eq "Success" -and $services.Count -gt 0) {
                $charResult = Invoke-WinRtAsync `
                    -AsyncOp ($services[0].GetCharacteristicsForUuidAsync($CharacteristicUuid, $mode)) `
                    -ResultType ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristicsResult]) `
                    -TimeoutMs ($TimeoutSeconds * 1000)
                $entry.characteristic_status = $charResult.Status.ToString()
                $chars = @(Get-CollectionItems -Collection $charResult.Characteristics)
                $entry.characteristic_count = $chars.Count
                if ($entry.characteristic_status -eq "Success" -and $chars.Count -gt 0) {
                    $char = $chars[0]
                    $entry.properties = $char.CharacteristicProperties.ToString()
                    $read = Invoke-WinRtAsync `
                        -AsyncOp ($char.ReadValueAsync($mode)) `
                        -ResultType ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattReadResult]) `
                        -TimeoutMs ($TimeoutSeconds * 1000)
                    $entry.read_status = $read.Status.ToString()
                    if ($read.ProtocolError) {
                        $entry.protocol_error = [int]$read.ProtocolError
                    }
                    if ($read.Value) {
                        [byte[]]$bytes = Convert-BufferToBytes -Buffer $read.Value
                        $entry.value_length = $bytes.Length
                        $entry.value_utf8 = Convert-BufferToUtf8 -Bytes $bytes
                        if ([string]::IsNullOrWhiteSpace($entry.value_utf8)) {
                            $entry.value_utf8 = Convert-BufferToUtf8FromBuffer -Buffer $read.Value
                            if (-not [string]::IsNullOrWhiteSpace($entry.value_utf8)) {
                                $entry.value_length = [Text.Encoding]::UTF8.GetByteCount($entry.value_utf8)
                            }
                        }
                    }
                }
            }
        } catch {
            $entry.error = $_.Exception.Message
        }
        $results += [pscustomobject]$entry
    }
    return $results
}

$address = Get-TargetBluetoothAddress -PreferredName $DeviceName -PreferredAddress $BluetoothAddress
$selector = [Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceService]::GetDeviceSelectorFromUuid($ServiceUuid)
$serviceInfos = Invoke-WinRtAsync `
    -AsyncOp ([Windows.Devices.Enumeration.DeviceInformation]::FindAllAsync($selector)) `
    -ResultType ([Windows.Devices.Enumeration.DeviceInformationCollection]) `
    -TimeoutMs ($TimeoutSeconds * 1000)

$infoItems = @(Get-CollectionItems -Collection $serviceInfos)
$matchingInfo = $infoItems |
    Where-Object {
        $_.Name -eq $DeviceName -and
        (($_.Id -replace "[^0-9A-Fa-f]", "").ToUpperInvariant()).Contains(("{0:X12}" -f $address))
    } |
    Select-Object -First 1
if ($null -eq $matchingInfo) {
    $matchingInfo = $infoItems | Where-Object { $_.Name -eq $DeviceName } | Select-Object -First 1
}

$probe = [ordered]@{
    status = "FAIL"
    device_name = $DeviceName
    bluetooth_address = "{0:X12}" -f $address
    pnp = @(Get-PnpDevice -Class Bluetooth |
        Where-Object { $_.FriendlyName -eq $DeviceName -or $_.InstanceId -like "*$("{0:X12}" -f $address)*" } |
        ForEach-Object {
            [ordered]@{
                status = $_.Status
                friendly_name = $_.FriendlyName
                instance_id = $_.InstanceId
            }
        })
    service_info_count = $infoItems.Count
    selected_service_id = $null
    device_connection_status = $null
    characteristics = @()
    dis = @()
}

if ($null -eq $matchingInfo) {
    $probe.error = "OTA service not found by AQS selector."
    $probe | ConvertTo-Json -Depth 8
    exit 1
}

$probe.selected_service_id = $matchingInfo.Id
$service = Invoke-WinRtAsync `
    -AsyncOp ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceService]::FromIdAsync($matchingInfo.Id)) `
    -ResultType ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceService]) `
    -TimeoutMs ($TimeoutSeconds * 1000)
$device = $service.DeviceId.ToString()
$bleDevice = Invoke-WinRtAsync `
    -AsyncOp ([Windows.Devices.Bluetooth.BluetoothLEDevice]::FromIdAsync($device)) `
    -ResultType ([Windows.Devices.Bluetooth.BluetoothLEDevice]) `
    -TimeoutMs ($TimeoutSeconds * 1000)
if ($bleDevice) {
    $probe.device_connection_status = $bleDevice.ConnectionStatus.ToString()
}

$probe.characteristics = @(
    Read-CharacteristicProbe -Service $service -Uuid $ControlUuid -Label "control_readiness_fallback"
    Read-CharacteristicProbe -Service $service -Uuid $DataUuid -Label "data_capabilities_fallback"
    Read-CharacteristicProbe -Service $service -Uuid $ReadinessUuid -Label "readiness"
    Read-CharacteristicProbe -Service $service -Uuid $CapabilitiesUuid -Label "capabilities"
)
$probe.dis = @(
    Read-DeviceCharacteristicProbe -Device $bleDevice -ServiceUuid $DeviceInformationUuid -CharacteristicUuid $DisModelNumberUuid -Label "dis_model"
    Read-DeviceCharacteristicProbe -Device $bleDevice -ServiceUuid $DeviceInformationUuid -CharacteristicUuid $DisHardwareRevisionUuid -Label "dis_hardware"
    Read-DeviceCharacteristicProbe -Device $bleDevice -ServiceUuid $DeviceInformationUuid -CharacteristicUuid $DisFirmwareRevisionUuid -Label "dis_firmware"
)

$hasIdentity = @($probe.characteristics | Where-Object {
    $_.read_status -eq "Success" -and
    ($_.value_utf8 -like "*fw_version=*" -or $_.value_utf8 -like "*firmware_ota_v1*")
}).Count -gt 0
$hasDisIdentity = @($probe.dis | Where-Object {
    $_.read_status -eq "Success" -and
    -not [string]::IsNullOrWhiteSpace($_.value_utf8)
}).Count -gt 0
$probe.status = if ($hasIdentity -or $hasDisIdentity) { "PASS" } else { "FAIL" }

$probe | ConvertTo-Json -Depth 8
if ($probe.status -eq "PASS") { exit 0 }
exit 1
