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

$manifest = Get-Content -Raw $ManifestPath | ConvertFrom-Json
$manifest_dir = Split-Path -Parent (Resolve-Path $ManifestPath)

# schema_version
if (-not $manifest.schema_version) {
    $errors += "Missing schema_version"
} elseif ($manifest.schema_version -ne 2) {
    $errors += "Unexpected schema_version: $($manifest.schema_version) (expected 2)"
}

# firmware
if (-not $manifest.firmware) {
    $errors += "Missing firmware section"
} else {
    if ([string]::IsNullOrWhiteSpace($manifest.firmware.version)) {
        $errors += "Missing firmware.version"
    }
    if ([string]::IsNullOrWhiteSpace($manifest.firmware.file)) {
        $errors += "Missing firmware.file"
    }
    if ([string]::IsNullOrWhiteSpace($manifest.firmware.sha256)) {
        $errors += "Missing firmware.sha256"
    }
    if ($null -eq $manifest.firmware.size_bytes -or $manifest.firmware.size_bytes -le 0) {
        $errors += "Missing or invalid firmware.size_bytes"
    }

    # Verify SHA256 against actual file
    $bin_path = Join-Path $manifest_dir $manifest.firmware.file
    if (Test-Path $bin_path) {
        $actual_hash = (Get-FileHash -LiteralPath $bin_path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual_hash -ne $manifest.firmware.sha256.ToLowerInvariant()) {
            $errors += "SHA256 mismatch: manifest=$($manifest.firmware.sha256) actual=$actual_hash"
        }
        $actual_size = (Get-Item -LiteralPath $bin_path).Length
        if ($actual_size -ne $manifest.firmware.size_bytes) {
            $errors += "Size mismatch: manifest=$($manifest.firmware.size_bytes) actual=$actual_size"
        }
    } else {
        $errors += "Firmware file not found: $bin_path"
    }
}

# requirements
if (-not $manifest.requirements) {
    $errors += "Missing requirements section"
} else {
    if ([string]::IsNullOrWhiteSpace($manifest.requirements.hardware_revision)) {
        $errors += "Missing requirements.hardware_revision"
    }
    if ($null -eq $manifest.requirements.protocol_version) {
        $errors += "Missing requirements.protocol_version"
    }
    if ([string]::IsNullOrWhiteSpace($manifest.requirements.min_desktop_version)) {
        $errors += "Missing requirements.min_desktop_version"
    }
}

# channel
$valid_channels = @("stable", "beta", "internal-test")
if (-not $manifest.channel) {
    $errors += "Missing channel"
} elseif ($manifest.channel -notin $valid_channels) {
    $errors += "Invalid channel: $($manifest.channel) (expected one of: $($valid_channels -join ', '))"
}

# rollback
if (-not $manifest.rollback -or $manifest.rollback.supported -ne $true) {
    $errors += "Missing or unsupported rollback section"
}

# Result
if ($errors.Count -gt 0) {
    Write-Host "FAIL: $($errors.Count) error(s):"
    $errors | ForEach-Object { Write-Host "  - $_" }
    exit 1
}

Write-Host "PASS: manifest valid - version=$($manifest.firmware.version) channel=$($manifest.channel) file=$($manifest.firmware.file) sha256=$($manifest.firmware.sha256)"
exit 0
