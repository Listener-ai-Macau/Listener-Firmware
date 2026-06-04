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
$voiceRecording = "components/voice_recording_control/voice_recording_control.c"
$captureBle = "tools/capture_audio_ble_wav.py"
$matrix = "tools/verify_audio_ble_product_matrix.py"
$events = "components/diag_log/include/diag_log_events.h"
$voiceControl = "components/voice_recording_control/voice_recording_control.c"

Assert-Contains -RelativePath $header -Pattern "typedef\s+struct\s*\{(?s).*queue_depth.*audio_pool_in_use.*pressure_percent.*pause_recommended" -Description "backpressure snapshot API shape"
Assert-Contains -RelativePath $header -Pattern "ble_audio_stream_get_backpressure" -Description "public backpressure snapshot function"

Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_BACKPRESSURE_PAUSE_PERCENT\s+95U" -Description "pause threshold"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_BACKPRESSURE_RESUME_PERCENT\s+70U" -Description "resume threshold"
Assert-Contains -RelativePath $stream -Pattern "(?s)#else\s*#define\s+BLE_AUDIO_STREAM_NOTIFY_QUEUE_LENGTH\s+48" -Description "N4 BLE audio queue covers short link recovery windows"
Assert-Contains -RelativePath $stream -Pattern "ble_audio_stream_pressure_percent" -Description "combined queue/pool pressure calculation"
Assert-Contains -RelativePath $stream -Pattern "uxQueueMessagesWaiting\(s_export_queue\)" -Description "queue depth sampling"
Assert-Contains -RelativePath $stream -Pattern "s_audio_pool_global_high_water" -Description "pool high-water sampling"
Assert-Contains -RelativePath $stream -Pattern "snapshot->pressure_percent\s*>=\s*BLE_AUDIO_STREAM_BACKPRESSURE_PAUSE_PERCENT" -Description "backpressure pause is pressure-threshold based"
Assert-NotContains -RelativePath $stream -Pattern "!snapshot->link_ready\s*\|\|" -Description "link-down-only capture pause trigger"
Assert-Contains -RelativePath $stream -Pattern "s_transport_audio_payload_bytes" -Description "session-scoped audio payload snapshot"
Assert-Contains -RelativePath $stream -Pattern "s_transport_audio_payload_bytes\s*=\s*payload_bytes" -Description "session start freezes audio payload size"
Assert-Contains -RelativePath $stream -Pattern "s_transport_audio_payload_bytes\s*=\s*0" -Description "session reset clears frozen audio payload"
Assert-Contains -RelativePath $stream -Pattern "(?s)ble_audio_stream_count_audio_packets.*?ble_audio_stream_session_audio_payload_bytes" -Description "packet counting uses session payload snapshot"
Assert-Contains -RelativePath $stream -Pattern "(?s)ble_audio_stream_send_session_audio_internal.*?ble_audio_stream_session_audio_payload_bytes" -Description "packet sending uses session payload snapshot"
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
Assert-Contains -RelativePath $stream -Pattern "ble_audio_stream_replay_remove_packet" -Description "confirmed audio packets are removed from the replay window"
Assert-Contains -RelativePath $stream -Pattern "(?s)ble_gatts_notify_custom.*?ble_audio_stream_replay_remove_packet\(session_id,\s*sequence_or_count\).*?ble_audio_stream_stats_packet_result\(session_id,\s*packet_type,\s*ESP_OK\)" -Description "successful notify removes current packet before success accounting"
Assert-Contains -RelativePath $stream -Pattern "ble_audio_stream_replay_pending_packets\(\s*session_id,\s*skip_replay_current_packet,\s*sequence_or_count\s*\)" -Description "replay drain receives current-packet skip context"
Assert-Contains -RelativePath $stream -Pattern "(?s)ble_audio_stream_send_packet.*?LISTENER_AUDIO_PACKET_TYPE_SESSION_STOP.*?ble_audio_stream_replay_pending_packets" -Description "stop waits for replayed tail audio before terminal control"
Assert-Contains -RelativePath $stream -Pattern "(?s)ble_audio_stream_send_packet.*?LISTENER_AUDIO_PACKET_TYPE_AUDIO_DATA.*?ble_audio_stream_replay_store_packet" -Description "in-flight audio data packets are retained for replay"
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

Assert-Contains -RelativePath $voiceRecording -Pattern "toggle_start_pending_transfer" -Description "rapid toggle start intent is queued while previous session transfers"
Assert-Contains -RelativePath $voiceRecording -Pattern "audio_session_transferring" -Description "pending start records transferring reason"
Assert-Contains -RelativePath $voiceRecording -Pattern "recording_waiting_for_previous_session" -Description "pending start exposes previous-session wait status"
Assert-Contains -RelativePath $voiceRecording -Pattern "(?s)if\s*\(\s*s_state\s*==\s*VOICE_RECORDING_STATE_TRANSFERRING\s*\)\s*\{\s*return;\s*\}\s*if\s*\(\s*s_state\s*!=\s*VOICE_RECORDING_STATE_IDLE\s*\)" -Description "pending start survives the transferring drain window"

Assert-Contains -RelativePath $events -Pattern "DIAG_AUDIO_BACKPRESSURE" -Description "audio backpressure event schema"
Assert-Contains -RelativePath $events -Pattern "DIAG_BAUD_WATERMARK" -Description "BLE audio watermark event schema"
Assert-Contains -RelativePath $events -Pattern "DIAG_BAUD_BACKPRESSURE" -Description "BLE audio backpressure event schema"

Assert-Contains -RelativePath $voiceControl -Pattern "voice_recording_control_source_is_user_start_intent" -Description "user start intent source classifier"
Assert-Contains -RelativePath $voiceControl -Pattern "voice_recording_control_source_is_host_control" -Description "host cleanup/control source classifier"
Assert-Contains -RelativePath $voiceControl -Pattern "host_toggle_transport_not_ready_no_pending" -Description "host transport-not-ready toggle negative pending-start case"
Assert-Contains -RelativePath $voiceControl -Pattern "host_cleanup_toggle_cleared_pending_start" -Description "host cleanup toggle clears stale pending start"
Assert-Contains -RelativePath $voiceControl -Pattern "host_cleanup_toggle_ignored_user_pending" -Description "host cleanup toggle preserves real user pending start"
Assert-Contains -RelativePath $voiceControl -Pattern "host_cleanup_toggle_ignored_after_abort" -Description "late host cleanup toggle after aborted session guard"
Assert-Contains -RelativePath $voiceControl -Pattern "toggle_start_pending_transfer" -Description "real user next-start pending transfer case"
Assert-Contains -RelativePath $voiceControl -Pattern 'strcmp\(action, "STOP"\) == 0 \|\| strcmp\(action, "CLEANUP"\) == 0' -Description "explicit stop/cleanup control commands"
Assert-Contains -RelativePath $voiceControl -Pattern '(?s)if \(host_control\) \{.*host_cleanup_toggle_cleared_pending_start.*return;.*power_manager_record_activity\("voice_recording_pending_start"\)' -Description "host cleanup toggle exits before pending-start ignore path"
Assert-Contains -RelativePath $voiceControl -Pattern '(?s)if \(ret == ESP_ERR_INVALID_STATE && !audio_capture_session_is_active\(\)\) \{.*if \(user_start_intent\) \{.*voice_recording_control_schedule_pending_start\(\s*source,\s*"audio_transport_not_ready",\s*"recording_waiting_for_ble_audio"\s*\).*host_toggle_transport_not_ready_no_pending' -Description "only user idle start failures create pending start"
Assert-Contains -RelativePath $voiceControl -Pattern '(?s)s_state == VOICE_RECORDING_STATE_TRANSFERRING.*if \(user_start_intent\) \{.*toggle_start_pending_transfer.*voice_recording_control_schedule_pending_start\(\s*source,\s*"audio_session_transferring",\s*"recording_waiting_for_previous_session"\s*\).*voice_recording_control_stop\(source\)' -Description "transfer toggle distinguishes user next-start from host cleanup stop"
Assert-NotContains -RelativePath $voiceControl -Pattern '(?s)\n\s*if \(s_state == VOICE_RECORDING_STATE_TRANSFERRING\) \{\s*power_manager_record_activity\("voice_recording_pending_transfer_start"\)' -Description "unconditional transferring pending-start before idle handling"
Assert-Contains -RelativePath $voiceControl -Pattern '(?s)static void voice_recording_control_poll_pending_start\(void\).*if \(s_state == VOICE_RECORDING_STATE_TRANSFERRING\) \{.*return;.*if \(s_state != VOICE_RECORDING_STATE_IDLE\) \{.*voice_recording_control_reset_pending_start\(\)' -Description "pending start survives transfer drain but resets outside idle/transfer"

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
