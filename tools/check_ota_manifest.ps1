param(
    [Parameter(Mandatory = $true)]
    [string]$ManifestPath
)

$ErrorActionPreference = "Stop"
$errors = @()

if (-not (Test-Path $ManifestPath)) {
    Write-Error "Manifest not found: $ManifestPath"
    exit 1
}

$manifest_json = Get-Content -Raw $ManifestPath
$convertFromJsonCommand = Get-Command ConvertFrom-Json
if ($convertFromJsonCommand.Parameters.ContainsKey("DateKind")) {
    $manifest = $manifest_json | ConvertFrom-Json -DateKind String
} else {
    $manifest = $manifest_json | ConvertFrom-Json
}
$manifest_dir = Split-Path -Parent (Resolve-Path $ManifestPath)
$project_root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$manifest_schema_path = Join-Path $project_root "third_party\denzic-platform\ota\protocol\ota_manifest_v2.json"
if (-not (Test-Path -LiteralPath $manifest_schema_path)) {
    Write-Error "Platform OTA manifest schema not found: $manifest_schema_path"
    exit 1
}
$manifest_schema = Get-Content -LiteralPath $manifest_schema_path -Raw | ConvertFrom-Json
$expected_schema_version = [int]$manifest_schema.schema_version
$esp_app_version_max_chars = [int]$manifest_schema.firmware_version_max_chars
$sha256_hex_chars = [int]$manifest_schema.sha256_hex_chars
$valid_channels = @($manifest_schema.channels)
$valid_rollback_methods = @($manifest_schema.rollback_methods)

function Test-JsonProperty {
    param(
        [Parameter(Mandatory = $true)]
        [AllowNull()]
        $Object,
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    return $null -ne $Object -and $Object.PSObject.Properties.Name -contains $Name
}

function Get-JsonValue {
    param(
        [Parameter(Mandatory = $true)]
        [AllowNull()]
        $Object,
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $current = $Object
    foreach ($part in ($Path -split '\.')) {
        if (-not (Test-JsonProperty -Object $current -Name $part)) {
            return [PSCustomObject]@{
                Exists = $false
                Value = $null
            }
        }
        $current = $current.$part
    }

    return [PSCustomObject]@{
        Exists = $true
        Value = $current
    }
}

function Add-MissingOrInvalid {
    param([Parameter(Mandatory = $true)][string]$Message)
    $script:errors += $Message
}

function Require-Object {
    param([Parameter(Mandatory = $true)][string]$Path)

    $result = Get-JsonValue -Object $manifest -Path $Path
    if (-not $result.Exists -or $null -eq $result.Value) {
        Add-MissingOrInvalid "Missing $Path section"
        return $false
    }
    if ($result.Value -isnot [pscustomobject]) {
        Add-MissingOrInvalid "Invalid $Path section"
        return $false
    }
    return $true
}

function Require-String {
    param([Parameter(Mandatory = $true)][string]$Path)

    $result = Get-JsonValue -Object $manifest -Path $Path
    if (-not $result.Exists -or [string]::IsNullOrWhiteSpace([string]$result.Value)) {
        Add-MissingOrInvalid "Missing $Path"
        return $null
    }
    return [string]$result.Value
}

function Require-Bool {
    param([Parameter(Mandatory = $true)][string]$Path)

    $result = Get-JsonValue -Object $manifest -Path $Path
    if (-not $result.Exists -or $result.Value -isnot [bool]) {
        Add-MissingOrInvalid "Missing or invalid $Path"
        return $null
    }
    return [bool]$result.Value
}

function Require-PositiveInteger {
    param([Parameter(Mandatory = $true)][string]$Path)

    $result = Get-JsonValue -Object $manifest -Path $Path
    $parsed = 0L
    if (-not $result.Exists -or -not [Int64]::TryParse([string]$result.Value, [ref]$parsed) -or $parsed -le 0) {
        Add-MissingOrInvalid "Missing or invalid $Path"
        return $null
    }
    return $parsed
}

# Value getters mirror the Require-* validators without reporting errors; the
# schema-driven Test-RequiredFields pass has already recorded any failure.
function Get-StringOrNull {
    param([Parameter(Mandatory = $true)][string]$Path)

    $result = Get-JsonValue -Object $manifest -Path $Path
    if (-not $result.Exists -or [string]::IsNullOrWhiteSpace([string]$result.Value)) {
        return $null
    }
    return [string]$result.Value
}

function Get-BoolOrNull {
    param([Parameter(Mandatory = $true)][string]$Path)

    $result = Get-JsonValue -Object $manifest -Path $Path
    if (-not $result.Exists -or $result.Value -isnot [bool]) {
        return $null
    }
    return [bool]$result.Value
}

function Get-PositiveIntegerOrNull {
    param([Parameter(Mandatory = $true)][string]$Path)

    $result = Get-JsonValue -Object $manifest -Path $Path
    $parsed = 0L
    if (-not $result.Exists -or -not [Int64]::TryParse([string]$result.Value, [ref]$parsed) -or $parsed -le 0) {
        return $null
    }
    return $parsed
}

# Required-field lists come from the platform manifest schema; section keys may
# contain dots (e.g. ble_identity.dis), so look them up as literal property names.
function Test-RequiredFields {
    param([Parameter(Mandatory = $true)][string]$Section)

    $property = $manifest_schema.required_fields.PSObject.Properties[$Section]
    if ($null -eq $property) {
        return
    }
    $spec = $property.Value
    if ($null -ne $spec.string) {
        foreach ($name in @($spec.string)) {
            Require-String "$Section.$name" | Out-Null
        }
    }
    if ($null -ne $spec.bool) {
        foreach ($name in @($spec.bool)) {
            Require-Bool "$Section.$name" | Out-Null
        }
    }
    if ($null -ne $spec.positive_integer) {
        foreach ($name in @($spec.positive_integer)) {
            Require-PositiveInteger "$Section.$name" | Out-Null
        }
    }
}

# schema_version
if (-not (Test-JsonProperty -Object $manifest -Name "schema_version")) {
    $errors += "Missing schema_version"
} elseif ($manifest.schema_version -ne $expected_schema_version) {
    $errors += "Unexpected schema_version: $($manifest.schema_version) (expected $expected_schema_version)"
}

if (-not (Test-JsonProperty -Object $manifest -Name "created_at_utc") -or $null -eq $manifest.created_at_utc) {
    $errors += "Missing created_at_utc"
} elseif ($manifest.created_at_utc -is [datetime]) {
    if ($manifest.created_at_utc.Kind -eq [DateTimeKind]::Unspecified) {
        $errors += "Invalid created_at_utc: expected timezone-aware UTC timestamp"
    }
} else {
    $created_at_text = [string]$manifest.created_at_utc
    if ([string]::IsNullOrWhiteSpace($created_at_text)) {
        $errors += "Missing created_at_utc"
    } elseif (-not $created_at_text.EndsWith("Z")) {
        $errors += "Invalid created_at_utc: expected UTC timestamp ending in Z"
    }
    $created_at = [DateTimeOffset]::MinValue
    if (-not [DateTimeOffset]::TryParse($created_at_text, [ref]$created_at)) {
        $errors += "Invalid created_at_utc: expected ISO 8601 UTC timestamp"
    }
}

# firmware
if (Require-Object "firmware") {
    Test-RequiredFields "firmware"
    $firmware_version = Get-StringOrNull "firmware.version"
    $firmware_file = Get-StringOrNull "firmware.file"
    $firmware_sha256 = Get-StringOrNull "firmware.sha256"
    $firmware_size = Get-PositiveIntegerOrNull "firmware.size_bytes"

    if ($firmware_sha256 -and $firmware_sha256 -cnotmatch "^[0-9a-f]{$sha256_hex_chars}$") {
        $errors += "Invalid firmware.sha256: expected lowercase $sha256_hex_chars-character hex digest"
    }
    if ($firmware_version -and $firmware_version.Length -gt $esp_app_version_max_chars) {
        $errors += "Invalid firmware.version: expected <= $esp_app_version_max_chars characters for ESP app descriptor / BLE OTA control"
    }

    # Verify SHA256 against actual file
    if ($firmware_file) {
        if ([System.IO.Path]::IsPathRooted($firmware_file) -or $firmware_file -like "*..*") {
            $errors += "Invalid firmware.file: must be a package-local filename"
        } else {
            $bin_path = Join-Path $manifest_dir $firmware_file
            if (Test-Path $bin_path) {
                $actual_hash = (Get-FileHash -LiteralPath $bin_path -Algorithm SHA256).Hash.ToLowerInvariant()
                if ($firmware_sha256 -and $actual_hash -ne $firmware_sha256.ToLowerInvariant()) {
                    $errors += "SHA256 mismatch: manifest=$firmware_sha256 actual=$actual_hash"
                }
                $actual_size = (Get-Item -LiteralPath $bin_path).Length
                if ($firmware_size -and $actual_size -ne $firmware_size) {
                    $errors += "Size mismatch: manifest=$firmware_size actual=$actual_size"
                }
            } else {
                $errors += "Firmware file not found: $bin_path"
            }
        }
    }
}

# requirements
if (Require-Object "requirements") {
    Test-RequiredFields "requirements"
}

# channel
$channel = Require-String "channel"
if ($channel -and $channel -notin $valid_channels) {
    $errors += "Invalid channel: $channel (expected one of: $($valid_channels -join ', '))"
}

# ble_identity
if (Require-Object "ble_identity") {
    Test-RequiredFields "ble_identity"
    if (Require-Object "ble_identity.dis") {
        Test-RequiredFields "ble_identity.dis"
    }
}

# rollback
if (Require-Object "rollback") {
    Test-RequiredFields "rollback"
    $rollback_supported = Get-BoolOrNull "rollback.supported"
    if ($null -ne $rollback_supported -and $rollback_supported -ne $true) {
        $errors += "Unsupported rollback section: rollback.supported must be true"
    }
    $rollback_method = Get-StringOrNull "rollback.method"
    if ($rollback_method -and $rollback_method -notin $valid_rollback_methods) {
        $errors += "Invalid rollback.method: $rollback_method"
    }
}

# recovery
if (Require-Object "recovery") {
    Test-RequiredFields "recovery"
}

# Result
if ($errors.Count -gt 0) {
    Write-Host "FAIL: $($errors.Count) error(s):"
    $errors | ForEach-Object { Write-Host "  - $_" }
    exit 1
}

Write-Host "PASS: manifest valid - version=$($manifest.firmware.version) channel=$($manifest.channel) file=$($manifest.firmware.file) sha256=$($manifest.firmware.sha256)"
exit 0
