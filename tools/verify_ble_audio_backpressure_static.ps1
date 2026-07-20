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
$gap = "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c"
$capture = "ports/esp32/audio_capture/audio_capture_esp32.c"
$voiceRecording = "components/voice_recording_control/voice_recording_control.c"
$sdkDefaults = "sdkconfig.defaults"
$sdkDefaultsEsp32s3 = "sdkconfig.defaults.esp32s3"
$captureBle = "tools/capture_audio_ble_wav.py"
$matrix = "tools/verify_audio_ble_product_matrix.py"
$transportModel = "tools/verify_ble_audio_transport_model.py"
$captureIntegrity = "tools/verify_audio_capture_integrity_log.py"
$events = "components/diag_log/include/diag_log_events.h"
$voiceControl = "components/voice_recording_control/voice_recording_control.c"

Assert-Contains -RelativePath $header -Pattern "typedef\s+struct\s*\{(?s).*queue_depth.*audio_pool_in_use.*pressure_percent.*pause_recommended" -Description "backpressure snapshot API shape"
Assert-Contains -RelativePath $header -Pattern "ble_audio_stream_get_backpressure" -Description "public backpressure snapshot function"

Assert-NotContains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_BACKPRESSURE_(PAUSE|RESUME)_(QUEUE_DEPTH|POOL_IN_USE)" -Description "false low-water capture pause threshold"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_BACKPRESSURE_PAUSE_PERCENT\s+95U" -Description "true-capacity pause threshold"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_BACKPRESSURE_RESUME_PERCENT\s+70U" -Description "true-capacity resume threshold"
Assert-Contains -RelativePath $stream -Pattern "(?s)#else\s*#define\s+BLE_AUDIO_STREAM_NOTIFY_QUEUE_LENGTH\s+48" -Description "N4 BLE audio queue covers short link recovery windows"
Assert-Contains -RelativePath $stream -Pattern "ble_audio_stream_pressure_percent" -Description "combined queue/pool pressure calculation"
Assert-Contains -RelativePath $stream -Pattern "uxQueueMessagesWaiting\(s_export_queue\)" -Description "queue depth sampling"
Assert-Contains -RelativePath $stream -Pattern "s_audio_pool_global_high_water" -Description "pool high-water sampling"
Assert-Contains -RelativePath $stream -Pattern "snapshot->pressure_percent\s*>=\s*BLE_AUDIO_STREAM_BACKPRESSURE_PAUSE_PERCENT" -Description "backpressure keeps emergency pressure pause"
Assert-Contains -RelativePath $stream -Pattern "snapshot->pressure_percent\s*<=\s*BLE_AUDIO_STREAM_BACKPRESSURE_RESUME_PERCENT" -Description "backpressure keeps true-capacity resume"
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
Assert-Contains -RelativePath $stream -Pattern "\.flags\s*=\s*BLE_GATT_CHR_F_WRITE\s*\|\s*BLE_GATT_CHR_F_WRITE_NO_RSP" -Description "audio control supports no-response writes for stop/toggle under notify load"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_REPLAY_WINDOW_PACKETS\s+48" -Description "active-session BLE audio replay window"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_NOTIFY_RETRY_DELAY_MS\s+2" -Description "short retry yield for sustained audio notify pressure"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_NOTIFY_MIN_FREE_MSYS_BLOCKS\s+4" -Description "NimBLE msys preflight before 517-MTU audio notify"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_NOTIFY_MSYS_WAIT_MS\s+1" -Description "bounded 1ms msys wait cadence"
Assert-Contains -RelativePath $stream -Pattern "ble_audio_stream_wait_msys_blocks" -Description "NimBLE msys preflight helper"
Assert-Contains -RelativePath $stream -Pattern "os_msys_num_free\(\)" -Description "NimBLE msys free-block sampling"
Assert-Contains -RelativePath $stream -Pattern "msys_waits=" -Description "session summary exposes proactive msys waits"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_PCM_PRODUCTION_BYTES_PER_SECOND\s+32000U" -Description "BLE audio records PCM production rate"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_AUDIO_TARGET_BYTES_PER_SECOND\s+38400U" -Description "BLE audio target rate has transport drain headroom"
Assert-Contains -RelativePath $stream -Pattern "audio transport target must drain PCM faster than capture produces it" -Description "compile-time transport headroom contract"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_AUDIO_PACE_TICK_MS\s+10U" -Description "BLE audio media clock has stable 10ms tick"
Assert-Contains -RelativePath $stream -Pattern "s_audio_pace_debt_bytes" -Description "BLE audio retains fractional media-clock debt"
Assert-Contains -RelativePath $stream -Pattern "audio_pace_ticks=" -Description "session summary exposes media-clock pacing"
Assert-Contains -RelativePath $stream -Pattern "audio_pace_target_bytes_per_s=" -Description "session summary exposes paced drain target"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_LOSSLESS_RICE_FLAG\s+0x01u" -Description "lossless Rice packet flag"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_LOSSLESS_RICE_VERSION_V1\s+1u" -Description "lossless Rice V1 compatibility frame"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_LOSSLESS_RICE_VERSION_V2\s+2u" -Description "lossless Rice V2 adaptive frame"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_LOSSLESS_RICE_VERSION_V3\s+3u" -Description "lossless Rice V3 higher-order adaptive frame"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_LOSSLESS_RICE_PREDICTOR_FIRST_ORDER\s+1u" -Description "lossless Rice V2 first-order predictor"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_LOSSLESS_RICE_PREDICTOR_SECOND_ORDER\s+2u" -Description "lossless Rice V2 second-order predictor"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_LOSSLESS_RICE_PREDICTOR_FOURTH_ORDER\s+4u" -Description "lossless Rice V3 fourth-order predictor"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_LOSSLESS_RICE_V2_PARAMETER_K_SPAN\s+1u" -Description "lossless Rice V2 uses a bounded parameter search"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_LOSSLESS_RICE_HEADER_BYTES\s+6u" -Description "lossless Rice seed header"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_LOSSLESS_RICE_PREFERRED_PCM_BYTES\s+480U" -Description "lossless Rice expands raw PCM per single-PDU notification"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_LOSSLESS_RICE_FALLBACK_STEP_BYTES\s+32U" -Description "lossless Rice has bounded smaller-frame fallback"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_LOSSLESS_RICE_FINE_FALLBACK_STEP_BYTES\s+16U" -Description "lossless Rice probes larger packets before the existing fallback ladder"
Assert-Contains -RelativePath $stream -Pattern "ble_audio_stream_encode_lossless_rice" -Description "lossless Rice encoder"
Assert-Contains -RelativePath $stream -Pattern "ble_audio_stream_plan_session_audio_packet" -Description "lossless Rice packet planner"
Assert-Contains -RelativePath $stream -Pattern "TYPE:AUDIO:LOSSLESS_RICE:1" -Description "Type lossless capability negotiation"
Assert-Contains -RelativePath $stream -Pattern "TYPE:AUDIO:LOSSLESS_RICE:2" -Description "Type V2 lossless capability negotiation"
Assert-Contains -RelativePath $stream -Pattern "TYPE:AUDIO:LOSSLESS_RICE:3" -Description "Type V3 lossless capability negotiation"
Assert-Contains -RelativePath $stream -Pattern "s_type_lossless_rice_capable" -Description "per-link lossless capability state"
Assert-Contains -RelativePath $stream -Pattern "s_type_lossless_rice_version" -Description "per-link lossless capability version"
Assert-Contains -RelativePath $stream -Pattern "s_transport_lossless_rice_enabled" -Description "session freezes lossless capability before packet planning"
Assert-Contains -RelativePath $stream -Pattern "s_transport_lossless_rice_version" -Description "session freezes lossless capability version"
Assert-Contains -RelativePath $stream -Pattern "BLE_AUDIO_STREAM_AUDIO_JOB_COOPERATIVE_YIELD_BATCHES\s+5U" -Description "continuous audio export periodically yields to the scheduler"
Assert-Contains -RelativePath $stream -Pattern "consecutive_audio_jobs" -Description "continuous audio export tracks bounded cooperative yielding"
Assert-Contains -RelativePath $stream -Pattern "ble_audio_stream_is_type_lossless_rice_enabled" -Description "public lossless capability status query"
Assert-Contains -RelativePath $stream -Pattern "audio_wire_bytes=" -Description "session summary reports compressed wire bytes"
Assert-Contains -RelativePath $stream -Pattern "audio_lossless_packets=" -Description "session summary reports compressed packet count"
Assert-Contains -RelativePath $stream -Pattern "audio_raw_packets=" -Description "session summary reports raw fallback packet count"
Assert-Contains -RelativePath $stream -Pattern "single_pdu_value_max_bytes" -Description "BLE audio caps ATT values to one negotiated link-layer PDU"
Assert-Contains -RelativePath $stream -Pattern "fragments into three LL PDUs" -Description "BLE audio documents the sustained ATT fragmentation failure mode"
Assert-Contains -RelativePath $stream -Pattern "ble_hid_gap_get_audio_notification_value_max_bytes" -Description "BLE audio reads the active DLE-derived ATT value budget"
Assert-Contains -RelativePath $gap -Pattern "BLE_HID_GAP_ACTIVE_ITVL_MIN\s+6U" -Description "active audio uses the minimum 7.5ms connection interval"
Assert-Contains -RelativePath $gap -Pattern "BLE_HID_GAP_ACTIVE_ITVL_MAX\s+6U" -Description "active audio interval remains deterministic"
Assert-Contains -RelativePath $gap -Pattern "BLE_HID_GAP_ACTIVE_MIN_CE_LEN\s+8U" -Description "active audio requests at least two 251-octet PDUs per connection event"
Assert-Contains -RelativePath $gap -Pattern "BLE_HID_GAP_ACTIVE_MAX_CE_LEN\s+12U" -Description "active audio reserves the full 7.5ms connection event"
Assert-Contains -RelativePath $gap -Pattern "\.min_ce_len\s*=\s*active_audio\s*\?\s*BLE_HID_GAP_ACTIVE_MIN_CE_LEN" -Description "active connection update sends its minimum event budget"
Assert-Contains -RelativePath $gap -Pattern "\.max_ce_len\s*=\s*active_audio\s*\?\s*BLE_HID_GAP_ACTIVE_MAX_CE_LEN" -Description "active connection update sends its maximum event budget"
Assert-Contains -RelativePath $gap -Pattern "ble_hid_gap_get_audio_notification_value_max_bytes" -Description "GAP exports the DLE-derived ATT value capacity"
Assert-Contains -RelativePath $gap -Pattern "att_value_overhead\s*=\s*7U" -Description "DLE capacity deducts L2CAP and ATT headers"
Assert-Contains -RelativePath $stream -Pattern "BLE_GAP_EVENT_NOTIFY_TX reports only a submission attempt" -Description "NimBLE notify callback is not treated as radio completion"
Assert-Contains -RelativePath $stream -Pattern "#if\s+!MYNEWT_VAL\(BLE_GATT_NOTIFY\)" -Description "NimBLE notify support is a build-time requirement for mbuf ownership"
Assert-Contains -RelativePath $stream -Pattern "requires NimBLE notify support for enabled-path mbuf ownership" -Description "notify-disabled build guard explains enabled-path ownership contract"
Assert-NotContains -RelativePath $stream -Pattern "s_notify_credit_sem" -Description "submission-attempt completion credit semaphore"
Assert-NotContains -RelativePath $stream -Pattern "s_notify_tx_done_sem" -Description "BLE audio does not serialize each notification on a completion semaphore"
Assert-Contains -RelativePath $stream -Pattern "audio replay window armed: reason=%s" -Description "link-suspend replay arm logging"
Assert-Contains -RelativePath $stream -Pattern "audio replay window resend: session=" -Description "link-recovery replay resend logging"
Assert-Contains -RelativePath $stream -Pattern "audio replay window skip current packet" -Description "replay drain skips the triggering packet so it is not duplicated"
Assert-Contains -RelativePath $stream -Pattern "replay_retained_high_water" -Description "session summary replay retained high-water"
Assert-Contains -RelativePath $stream -Pattern "replay_stored=" -Description "session summary replay stored count"
Assert-Contains -RelativePath $stream -Pattern "replay_replaced=" -Description "session summary duplicate replay replacement count"
Assert-Contains -RelativePath $stream -Pattern "replay_removed=" -Description "session summary replay removal count"
Assert-Contains -RelativePath $stream -Pattern "replay_resent=" -Description "session summary replay resend count"
Assert-Contains -RelativePath $stream -Pattern "replay_resend_failed=" -Description "session summary replay failure count"
Assert-Contains -RelativePath $stream -Pattern "replay_skip_current=" -Description "session summary current packet skip count"
Assert-Contains -RelativePath $stream -Pattern "replay_pending=" -Description "session summary pending replay count"
Assert-Contains -RelativePath $stream -Pattern "DIAG_BAUD_REPLAY" -Description "BLE audio replay diagnostic event logging"
Assert-Contains -RelativePath $stream -Pattern "(?s)s_replay_pending\s*&&\s*s_replay_session_id\s*==\s*session_id.*?ble_audio_stream_replay_store_packet.*?skip_replay_current_packet\s*=\s*true" -Description "current audio packet retained before replay drain and marked for skip"
Assert-Contains -RelativePath $stream -Pattern "(?s)ble_audio_stream_reset_transport_session.*?ble_audio_stream_replay_clear_session" -Description "session reset clears retained replay window"
Assert-NotContains -RelativePath $stream -Pattern "(?s)ble_gatts_notify_custom.*?ble_audio_stream_replay_remove_packet\(session_id,\s*sequence_or_count\).*?ble_audio_stream_stats_packet_result\(session_id,\s*packet_type,\s*ESP_OK\)" -Description "successful notify keeps recent audio packets retained for disconnect replay"
Assert-Contains -RelativePath $stream -Pattern "ble_audio_stream_replay_pending_packets\(\s*session_id,\s*skip_replay_current_packet,\s*sequence_or_count\s*\)" -Description "replay drain receives current-packet skip context"
Assert-Contains -RelativePath $stream -Pattern "(?s)ble_audio_stream_send_packet.*?LISTENER_AUDIO_PACKET_TYPE_SESSION_STOP.*?ble_audio_stream_replay_pending_packets" -Description "stop waits for replayed tail audio before terminal control"
Assert-Contains -RelativePath $stream -Pattern "(?s)ble_audio_stream_send_packet.*?LISTENER_AUDIO_PACKET_TYPE_AUDIO_DATA.*?ble_audio_stream_replay_store_packet" -Description "in-flight audio data packets are retained for replay"
Assert-Contains -RelativePath $stream -Pattern "(?s)esp_err_t\s+ble_audio_stream_send_session_cancel.*?ble_audio_stream_transport_session_active\(\).*?s_transport_session_id\s*!=\s*session_id" -Description "cancel during link recovery remains session-owned"
Assert-Contains -RelativePath $captureBle -Pattern "duplicate_packet_sequences" -Description "BLE capture records duplicate packet_sequence values"
Assert-Contains -RelativePath $captureBle -Pattern "duplicate packet_sequence values detected" -Description "BLE capture fails duplicate sequence validation"
Assert-Contains -RelativePath $matrix -Pattern "duplicate_packet_count" -Description "product matrix preserves duplicate sequence diagnostics"
Assert-Contains -RelativePath $matrix -Pattern '(?s)warnings:\s+list\[str\]\s*=\s*\[\].*?warnings\.append\(f"continuous_round\{round_no\}:\{warning_text\}"\).*?elif warnings:\s*result\s*=\s*"warning"\s*reason\s*=\s*"continuous_background_rounds_warning"' -Description "A1 continuous background round warnings propagate to matrix fail-on-warning"
Assert-Contains -RelativePath $matrix -Pattern '"round_warnings":\s*warnings' -Description "A1 continuous warning details are preserved in matrix JSON"

Assert-Contains -RelativePath $capture -Pattern "AUDIO_CAPTURE_STREAM_BATCH_FRAMES\s+3" -Description "audio capture batches three PCM frames per BLE queue item"
Assert-Contains -RelativePath $capture -Pattern "audio_capture_note_transport_backpressure" -Description "audio capture records BLE pressure without stopping microphone reads"
Assert-Contains -RelativePath $capture -Pattern "ble_audio_stream_get_backpressure" -Description "audio capture reads BLE audio pressure"
Assert-Contains -RelativePath $capture -Pattern "DIAG_AUDIO_BACKPRESSURE" -Description "audio capture backpressure diagnostic event logging"
Assert-Contains -RelativePath $capture -Pattern "capture_backpressure_gap_ms" -Description "session capture-gap telemetry"
Assert-Contains -RelativePath $capture -Pattern "capture_backpressure_gap_ms=0" -Description "BLE pressure never removes microphone capture time"
Assert-NotContains -RelativePath $capture -Pattern "audio_capture_backpressure_should_pause" -Description "BLE pressure cannot pause microphone capture"
Assert-NotContains -RelativePath $capture -Pattern "AUDIO_CAPTURE_BACKPRESSURE_PAUSE_MS" -Description "BLE pressure has no microphone-read delay path"

Assert-Contains -RelativePath $voiceRecording -Pattern "toggle_start_pending_transfer" -Description "rapid toggle start intent is queued while previous session transfers"
Assert-Contains -RelativePath $voiceRecording -Pattern "audio_session_transferring" -Description "pending start records transferring reason"
Assert-Contains -RelativePath $voiceRecording -Pattern "recording_waiting_for_previous_session" -Description "pending start exposes previous-session wait status"
Assert-Contains -RelativePath $voiceRecording -Pattern "(?s)if\s*\(\s*s_state\s*==\s*VOICE_RECORDING_STATE_TRANSFERRING\s*\)\s*\{\s*return;\s*\}\s*if\s*\(\s*s_state\s*!=\s*VOICE_RECORDING_STATE_IDLE\s*\)" -Description "pending start survives the transferring drain window"

Assert-Contains -RelativePath $events -Pattern "DIAG_AUDIO_BACKPRESSURE" -Description "audio backpressure event schema"
Assert-Contains -RelativePath $events -Pattern "DIAG_BAUD_WATERMARK" -Description "BLE audio watermark event schema"
Assert-Contains -RelativePath $events -Pattern "DIAG_BAUD_BACKPRESSURE" -Description "BLE audio backpressure event schema"
Assert-Contains -RelativePath $events -Pattern "DIAG_BAUD_REPLAY" -Description "BLE audio replay event schema"

Assert-Contains -RelativePath $transportModel -Pattern "case_disconnect_during_streaming" -Description "transport model disconnect during streaming case"
Assert-Contains -RelativePath $transportModel -Pattern "case_notify_disabled_during_streaming" -Description "transport model notify disabled case"
Assert-Contains -RelativePath $transportModel -Pattern "case_reconnect_before_stop" -Description "transport model reconnect before stop case"
Assert-Contains -RelativePath $transportModel -Pattern "case_stop_during_recovery" -Description "transport model stop during recovery case"
Assert-Contains -RelativePath $transportModel -Pattern "case_cancel_while_tail_packets_drain" -Description "transport model cancel while tail drains case"
Assert-Contains -RelativePath $transportModel -Pattern "case_duplicate_replay_prevention" -Description "transport model duplicate replay prevention case"
Assert-Contains -RelativePath $transportModel -Pattern "case_capacity_pressure_pause_resume_hysteresis" -Description "transport model true-capacity pressure hysteresis case"
Assert-Contains -RelativePath $captureIntegrity -Pattern "capture_backpressure_gap_ms" -Description "physical capture-integrity parser checks capture gaps"
Assert-Contains -RelativePath $captureIntegrity -Pattern "elapsed_over_pcm_ms" -Description "physical capture-integrity parser checks missing PCM time"
Assert-Contains -RelativePath $captureIntegrity -Pattern "audio_sent.*expected_packet_count" -Description "physical capture-integrity parser checks transport continuity"
Assert-Contains -RelativePath $transportModel -Pattern "case_pool_exhaustion" -Description "transport model pool exhaustion case"
Assert-Contains -RelativePath $transportModel -Pattern "case_stale_gatt_event_after_epoch_advance" -Description "transport model stale GATT event case"
Assert-Contains -RelativePath $transportModel -Pattern "case_bounded_retry_timeout" -Description "transport model bounded retry timeout case"
Assert-Contains -RelativePath $transportModel -Pattern "case_media_clock_drains_faster_than_pcm_production" -Description "transport model PCM-drain-headroom pacing case"
Assert-Contains -RelativePath $transportModel -Pattern "case_single_pdu_budget_beats_pcm_production" -Description "transport model verifies DLE and connection-event budget exceed PCM production"
Assert-Contains -RelativePath $transportModel -Pattern "case_lossless_predictive_rice_round_trip_and_wire_budget" -Description "transport model verifies lossless speech wire budget"
Assert-Contains -RelativePath $transportModel -Pattern "case_lossless_predictive_rice_preserves_extremes_and_falls_back_for_noise" -Description "transport model preserves PCM extremes and raw noise fallback"
Assert-Contains -RelativePath $transportModel -Pattern "case_lossless_adaptive_packet_plan_reduces_notification_demand" -Description "transport model proves adaptive lossless packets reduce Windows notification demand"
Assert-Contains -RelativePath $transportModel -Pattern "case_lossless_v2_first_order_reduces_noisy_voice_notification_rate" -Description "transport model proves V2 lowers noisy-voice notification demand"
Assert-Contains -RelativePath $transportModel -Pattern "case_lossless_v3_higher_order_reduces_smooth_voice_notification_rate" -Description "transport model proves V3 lowers smooth-voice notification demand"
Assert-Contains -RelativePath $transportModel -Pattern "RICE_V2_PARAMETER_K_SPAN" -Description "transport model mirrors bounded V2 parameter search"

foreach ($sdkPath in @($sdkDefaults, $sdkDefaultsEsp32s3)) {
    Assert-Contains -RelativePath $sdkPath -Pattern "CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT=64" -Description "audio-sized NimBLE MSYS1 block count"
    Assert-Contains -RelativePath $sdkPath -Pattern "CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT=96" -Description "audio-sized NimBLE MSYS2 block count"
    Assert-Contains -RelativePath $sdkPath -Pattern "CONFIG_BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT=24" -Description "default NimBLE ACL buffer count for sustained audio"
    Assert-Contains -RelativePath $sdkPath -Pattern "CONFIG_BT_CTRL_CE_LENGTH_TYPE_CE=y" -Description "controller honors active-audio connection-event budget"
}

Assert-Contains -RelativePath $voiceControl -Pattern "voice_recording_control_source_is_user_start_intent" -Description "user start intent source classifier"
Assert-Contains -RelativePath $voiceControl -Pattern "voice_recording_control_source_is_host_control" -Description "host cleanup/control source classifier"
Assert-Contains -RelativePath $voiceControl -Pattern "host_toggle_transport_not_ready_no_pending" -Description "host transport-not-ready toggle negative pending-start case"
Assert-Contains -RelativePath $voiceControl -Pattern "host_cleanup_toggle_cleared_pending_start" -Description "host cleanup toggle clears stale pending start"
Assert-Contains -RelativePath $voiceControl -Pattern "host_cleanup_toggle_ignored_user_pending" -Description "host cleanup toggle preserves real user pending start"
Assert-Contains -RelativePath $voiceControl -Pattern "host_cleanup_toggle_ignored_after_abort" -Description "late host cleanup toggle after aborted session guard"
Assert-Contains -RelativePath $voiceControl -Pattern "toggle_start_pending_transfer" -Description "real user next-start pending transfer case"
Assert-Contains -RelativePath $voiceControl -Pattern 'strcmp\(action, "STOP"\) == 0 \|\| strcmp\(action, "CLEANUP"\) == 0' -Description "explicit stop/cleanup control commands"
Assert-Contains -RelativePath $voiceControl -Pattern '(?s)VOICE_RECORDING_CONTROL_FSM_ARTIFACT.*host_cleanup_clears_host_pending_start.*VOICE_RECORDING_EFFECT_CLEAR_PENDING_START.*host_cleanup_preserves_user_pending_start.*host_cleanup_toggle_ignored_user_pending' -Description "host cleanup toggle pending-start decisions are modeled explicitly"
Assert-Contains -RelativePath $voiceControl -Pattern '(?s)VOICE_RECORDING_EVENT_START_REJECTED_TRANSPORT_NOT_READY.*VOICE_RECORDING_SOURCE_USER_START_INTENT.*VOICE_RECORDING_EFFECT_SCHEDULE_PENDING_START.*recording_waiting_for_ble_audio.*VOICE_RECORDING_SOURCE_HOST_CONTROL.*VOICE_RECORDING_EFFECT_IGNORE.*host_toggle_transport_not_ready_no_pending' -Description "only user idle start failures create pending start"
Assert-Contains -RelativePath $voiceControl -Pattern '(?s)rapid_next_start_while_transferring.*VOICE_RECORDING_STATE_TRANSFERRING.*VOICE_RECORDING_EVENT_TOGGLE.*VOICE_RECORDING_SOURCE_USER_START_INTENT.*VOICE_RECORDING_EFFECT_SCHEDULE_PENDING_START.*toggle_start_pending_transfer' -Description "transfer toggle distinguishes user next-start from host cleanup stop"
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
