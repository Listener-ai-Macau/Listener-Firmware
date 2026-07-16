[CmdletBinding()]
param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$scriptPath = Join-Path $RepoRoot "tools\ensure_ble_hid_connection.ps1"
if (-not (Test-Path -LiteralPath $scriptPath)) {
    throw "missing BLE HID connection selector: $scriptPath"
}

$source = Get-Content -LiteralPath $scriptPath -Raw
$failures = [System.Collections.Generic.List[string]]::new()

function Require-Fragment {
    param([string]$Fragment, [string]$Description)

    if ($source.IndexOf($Fragment, [StringComparison]::Ordinal) -lt 0) {
        $failures.Add("missing $Description")
    }
}

function Require-Order {
    param([string]$Earlier, [string]$Later, [string]$Description)

    $earlierIndex = $source.IndexOf($Earlier, [StringComparison]::Ordinal)
    $laterIndex = $source.IndexOf($Later, [StringComparison]::Ordinal)
    if ($earlierIndex -lt 0 -or $laterIndex -lt 0 -or $earlierIndex -ge $laterIndex) {
        $failures.Add("does not preserve order for $Description")
    }
}

# A random-identity recovery can leave same-name PnP rows that are stale or being
# removed. The selector must never let one of those rows win over an active link.
Require-Fragment '$_.Status -eq "OK"' "healthy PnP status filter"
Require-Fragment '$_.Present -eq $true' "present PnP device filter"
Require-Fragment '[Windows.Devices.Bluetooth.BluetoothLEDevice]::FromBluetoothAddressAsync($address)' "active BLE connection probe"
Require-Fragment '[string]$probe.ConnectionStatus -eq "Connected"' "active BLE connection preference"
Require-Fragment '$device = $candidates | Sort-Object InstanceId | Select-Object -First 1' "deterministic healthy fallback"
Require-Order '$candidates = @(Get-PnpDevice -Class Bluetooth |' '[Windows.Devices.Bluetooth.BluetoothLEDevice]::FromBluetoothAddressAsync($address)' "filtering before active-link probe"
Require-Order '[string]$probe.ConnectionStatus -eq "Connected"' '$device = $candidates | Sort-Object InstanceId | Select-Object -First 1' "active-link preference before fallback"

$tokens = $null
$parseErrors = $null
[System.Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$parseErrors) | Out-Null
foreach ($parseError in @($parseErrors)) {
    $failures.Add(("parse error at line {0}: {1}" -f $parseError.Extent.StartLineNumber, $parseError.Message))
}

if ($failures.Count -gt 0) {
    throw ("BLE HID connection selector static check failed:`n - " + ($failures -join "`n - "))
}

Write-Output "PASS: BLE HID connection selector static check passed."
