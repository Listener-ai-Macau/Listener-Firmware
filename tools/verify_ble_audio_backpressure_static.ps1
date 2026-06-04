[CmdletBinding()]
param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$errors = [System.Collections.Generic.List[string]]::new()

function Add-CheckError {
    param([Parameter(Mandatory = $true)][string]$Message)
    $script:errors.Add($Message)
}

function Read-RepoFile {
    param([Parameter(Mandatory = $true)][string]$RelativePath)

    $path = Join-Path $RepoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path)) {
        Add-CheckError "missing file: $RelativePath"
        return ""
    }
    return Get-Content -LiteralPath $path -Raw
}

function Assert-Contains {
    param(
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $text = Read-RepoFile -RelativePath $RelativePath
    if ($text -notmatch $Pattern) {
        Add-CheckError "$RelativePath missing $Description"
    }
}

function Assert-NotContains {
    param(
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $text = Read-RepoFile -RelativePath $RelativePath
    if ($text -match $Pattern) {
        Add-CheckError "$RelativePath contains forbidden $Description"
    }
}

function Assert-Order {
    param(
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][string]$First,
        [Parameter(Mandatory = $true)][string]$Second,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $text = Read-RepoFile -RelativePath $RelativePath
    $firstIndex = $text.IndexOf($First, [StringComparison]::Ordinal)
    $secondIndex = $text.IndexOf($Second, [StringComparison]::Ordinal)
    if ($firstIndex -lt 0 -or $secondIndex -lt 0 -or $firstIndex -gt $secondIndex) {
        Add-CheckError "$RelativePath does not preserve order for $Description"
    }
}

$header = "ports/esp32/ble_audio_stream/include/ble_audio_stream.h"
$stream = "ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c"
$capture = "ports/esp32/audio_capture/audio_capture_esp32.c"
$captureBle = "tools/capture_audio_ble_wav.py"
$matrix = "tools/verify_audio_ble_product_matrix.py"
$events = "components/diag_log/include/diag_log_events.h"

Assert-Contains -RelativePath $header -Pattern "typedef\s+struct\s*\{(?s).*queue_depth.*audio_pool_in_use.*pressure_percent.*pause_recommended" -Description "backpressure snapshot API shape"
Assert-Contains -RelativePath $header -Pattern "ble_audio_stream_get_backpressure" -Description "public backpressure snapshot function"

Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_BACKPRESSURE_PAUSE_PERCENT\s+95U" -Description "pause threshold"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_BACKPRESSURE_RESUME_PERCENT\s+70U" -Description "resume threshold"
Assert-Contains -RelativePath $stream -Pattern "(?s)#else\s*#define\s+BLE_AUDIO_STREAM_NOTIFY_QUEUE_LENGTH\s+48" -Description "N4 BLE audio queue covers short link recovery windows"
Assert-Contains -RelativePath $stream -Pattern "ble_audio_stream_pressure_percent" -Description "combined queue/pool pressure calculation"
Assert-Contains -RelativePath $stream -Pattern "uxQueueMessagesWaiting\(s_export_queue\)" -Description "queue depth sampling"
Assert-Contains -RelativePath $stream -Pattern "s_audio_pool_global_high_water" -Description "pool high-water sampling"
Assert-Contains -RelativePath $stream -Pattern "DIAG_BAUD_WATERMARK" -Description "BLE audio watermark diagnostic event logging"
Assert-Contains -RelativePath $stream -Pattern "DIAG_BAUD_BACKPRESSURE" -Description "BLE audio backpressure diagnostic event logging"
Assert-Contains -RelativePath $stream -Pattern "audio transport link suspended: reason=notify_disabled" -Description "notify-disabled link suspension instead of immediate session reset"
Assert-Contains -RelativePath $stream -Pattern "(?s)esp_err_t\s+ble_audio_stream_send_session_audio.*?s_transport_state\s*!=\s*BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAMING\s*\|\|\s*s_transport_session_id\s*!=\s*session_id" -Description "audio enqueue preserves active recovery window without link-ready precheck"
Assert-Contains -RelativePath $stream -Pattern "audio session stop queued during link recovery" -Description "stop during link recovery preserves queued audio before control intent"
Assert-NotContains -RelativePath $stream -Pattern "if\s*\(\s*active_session\s*&&\s*!link_ready\s*\)\s*\{\s*ble_audio_stream_purge_queued_session_jobs\(session_id,\s*true\)" -Description "stop during link recovery must not purge queued tail audio"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_REPLAY_WINDOW_PACKETS\s+48" -Description "active-session BLE audio replay window"
Assert-Contains -RelativePath $stream -Pattern "audio replay window armed: reason=%s" -Description "link-suspend replay arm logging"
Assert-Contains -RelativePath $stream -Pattern "audio replay window resend: session=" -Description "link-recovery replay resend logging"
Assert-Contains -RelativePath $stream -Pattern "audio replay window skip current packet" -Description "replay drain skips the triggering packet so it is not duplicated"
Assert-Contains -RelativePath $stream -Pattern "(?s)s_replay_pending\s*&&\s*s_replay_session_id\s*==\s*session_id.*?ble_audio_stream_replay_store_packet.*?skip_replay_current_packet\s*=\s*true" -Description "current audio packet retained before replay drain and marked for skip"
Assert-Contains -RelativePath $stream -Pattern "ble_audio_stream_replay_pending_packets\(\s*session_id,\s*skip_replay_current_packet,\s*sequence_or_count\s*\)" -Description "replay drain receives current-packet skip context"
Assert-Contains -RelativePath $stream -Pattern "(?s)ble_audio_stream_send_packet.*?LISTENER_AUDIO_PACKET_TYPE_SESSION_STOP.*?ble_audio_stream_replay_pending_packets" -Description "stop waits for replayed tail audio before terminal control"
Assert-Contains -RelativePath $stream -Pattern "(?s)ble_audio_stream_send_packet.*?LISTENER_AUDIO_PACKET_TYPE_AUDIO_DATA.*?ble_audio_stream_replay_store_packet" -Description "audio data packets are retained for replay"
Assert-Contains -RelativePath $stream -Pattern "(?s)esp_err_t\s+ble_audio_stream_send_session_cancel.*?ble_audio_stream_transport_session_active\(\).*?s_transport_session_id\s*!=\s*session_id" -Description "cancel during link recovery remains session-owned"
Assert-Contains -RelativePath $captureBle -Pattern "duplicate_packet_sequences" -Description "BLE capture records duplicate packet_sequence values"
Assert-Contains -RelativePath $captureBle -Pattern "duplicate packet_sequence values detected" -Description "BLE capture fails duplicate sequence validation"
Assert-Contains -RelativePath $matrix -Pattern "duplicate_packet_count" -Description "product matrix preserves duplicate sequence diagnostics"

Assert-Contains -RelativePath $capture -Pattern "audio_capture_backpressure_should_pause" -Description "audio capture backpressure gate"
Assert-Contains -RelativePath $capture -Pattern "ble_audio_stream_get_backpressure" -Description "audio capture reads BLE audio pressure"
Assert-Contains -RelativePath $capture -Pattern "DIAG_AUDIO_BACKPRESSURE" -Description "audio capture backpressure diagnostic event logging"
Assert-Contains -RelativePath $capture -Pattern "stop_or_cancel_requested" -Description "stop/cancel bypass for backpressure pause"
Assert-Order -RelativePath $capture -First "if (audio_capture_backpressure_should_pause())" -Second "esp_codec_dev_read" -Description "ES8311 read is gated before capture advances"
Assert-Order -RelativePath $capture -First "if (audio_capture_backpressure_should_pause())" -Second "i2s_channel_read" -Description "SPH0645 read is gated before capture advances"

Assert-Contains -RelativePath $events -Pattern "DIAG_AUDIO_BACKPRESSURE" -Description "audio backpressure event schema"
Assert-Contains -RelativePath $events -Pattern "DIAG_BAUD_WATERMARK" -Description "BLE audio watermark event schema"
Assert-Contains -RelativePath $events -Pattern "DIAG_BAUD_BACKPRESSURE" -Description "BLE audio backpressure event schema"

foreach ($relativePath in @(
    "tools/verify_ble_audio_backpressure_static.ps1"
)) {
    $fullPath = Join-Path $RepoRoot $relativePath
    $tokens = $null
    $parseErrors = $null
    [System.Management.Automation.Language.Parser]::ParseFile($fullPath, [ref]$tokens, [ref]$parseErrors) | Out-Null
    foreach ($parseError in @($parseErrors)) {
        Add-CheckError ("{0}:{1}: {2}" -f $relativePath, $parseError.Extent.StartLineNumber, $parseError.Message)
    }
}

if ($errors.Count -gt 0) {
    throw ("BLE audio backpressure static check failed:`n - " + ($errors -join "`n - "))
}

Write-Output "PASS: BLE audio backpressure static checks passed."
