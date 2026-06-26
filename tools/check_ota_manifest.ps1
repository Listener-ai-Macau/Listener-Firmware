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
$esp_app_version_max_chars = 31

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

# schema_version
if (-not (Test-JsonProperty -Object $manifest -Name "schema_version")) {
    $errors += "Missing schema_version"
} elseif ($manifest.schema_version -ne 2) {
    $errors += "Unexpected schema_version: $($manifest.schema_version) (expected 2)"
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
    Require-String "firmware.project" | Out-Null
    $firmware_version = Require-String "firmware.version"
    Require-String "firmware.git_commit" | Out-Null
    Require-Bool "firmware.git_dirty" | Out-Null
    Require-String "firmware.target" | Out-Null
    $firmware_file = Require-String "firmware.file"
    $firmware_sha256 = Require-String "firmware.sha256"
    $firmware_size = Require-PositiveInteger "firmware.size_bytes"

    if ($firmware_sha256 -and $firmware_sha256 -cnotmatch '^[0-9a-f]{64}$') {
        $errors += "Invalid firmware.sha256: expected lowercase 64-character hex digest"
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
    Require-String "requirements.hardware_revision" | Out-Null
    Require-PositiveInteger "requirements.protocol_version" | Out-Null
    Require-String "requirements.min_desktop_version" | Out-Null
}

# channel
$valid_channels = @("stable", "development")
$channel = Require-String "channel"
if ($channel -and $channel -notin $valid_channels) {
    $errors += "Invalid channel: $channel (expected one of: $($valid_channels -join ', '))"
}

# ble_identity
if (Require-Object "ble_identity") {
    Require-String "ble_identity.name" | Out-Null
    Require-String "ble_identity.appearance" | Out-Null
    Require-String "ble_identity.hid_service_uuid" | Out-Null
    Require-String "ble_identity.audio_service_uuid" | Out-Null
    Require-String "ble_identity.audio_notify_uuid" | Out-Null
    Require-String "ble_identity.readiness_uuid" | Out-Null
    Require-String "ble_identity.capabilities_uuid" | Out-Null
    if (Require-Object "ble_identity.dis") {
        Require-String "ble_identity.dis.manufacturer" | Out-Null
        Require-String "ble_identity.dis.model" | Out-Null
        Require-String "ble_identity.dis.hardware_revision" | Out-Null
        Require-String "ble_identity.dis.firmware_revision" | Out-Null
        Require-String "ble_identity.dis.software_revision_protocol" | Out-Null
    }
}

# rollback
if (Require-Object "rollback") {
    $rollback_supported = Require-Bool "rollback.supported"
    if ($null -ne $rollback_supported -and $rollback_supported -ne $true) {
        $errors += "Unsupported rollback section: rollback.supported must be true"
    }
    $rollback_method = Require-String "rollback.method"
    if ($rollback_method -and $rollback_method -ne "esp_idf_bootloader_rollback") {
        $errors += "Invalid rollback.method: $rollback_method"
    }
    Require-String "rollback.instructions" | Out-Null
}

# recovery
if (Require-Object "recovery") {
    Require-String "recovery.factory_reflash" | Out-Null
    Require-String "recovery.serial_commands" | Out-Null
}

# Result
if ($errors.Count -gt 0) {
    Write-Host "FAIL: $($errors.Count) error(s):"
    $errors | ForEach-Object { Write-Host "  - $_" }
    exit 1
}

Write-Host "PASS: manifest valid - version=$($manifest.firmware.version) channel=$($manifest.channel) file=$($manifest.firmware.file) sha256=$($manifest.firmware.sha256)"
exit 0
