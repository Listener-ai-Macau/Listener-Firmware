from __future__ import annotations

import pathlib
import re
from dataclasses import dataclass, field
from math import sin


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
STREAM = REPO_ROOT / "ports" / "esp32" / "ble_audio_stream" / "ble_audio_stream_esp32.c"
EVENTS = REPO_ROOT / "components" / "diag_log" / "include" / "diag_log_events.h"
GAP = REPO_ROOT / "ports" / "esp32" / "ble_hid_gap" / "ble_hid_gap_esp32.c"
PLATFORM_LOSSLESS_CODEC = (
    REPO_ROOT
    / "third_party"
    / "denzic-platform"
    / "audio"
    / "embedded"
    / "c"
    / "src"
    / "denzic_audio_lossless_v1.c"
)
PLATFORM_TRANSPORT_HEADER = (
    REPO_ROOT
    / "third_party"
    / "denzic-platform"
    / "audio"
    / "embedded"
    / "c"
    / "include"
    / "denzic_audio_transport_v1.h"
)


STATE_TOKENS = [
    "BLE_AUDIO_STREAM_TRANSPORT_STATE_DISCONNECTED",
    "BLE_AUDIO_STREAM_TRANSPORT_STATE_CONNECTED",
    "BLE_AUDIO_STREAM_TRANSPORT_STATE_MTU_READY",
    "BLE_AUDIO_STREAM_TRANSPORT_STATE_SUBSCRIBED",
    "BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAM_READY",
    "BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAMING",
    "BLE_AUDIO_STREAM_TRANSPORT_STATE_DRAINING",
    "BLE_AUDIO_STREAM_TRANSPORT_STATE_STOPPED",
    "BLE_AUDIO_STREAM_TRANSPORT_STATE_ERROR",
]

SOURCE_TOKENS = [
    "BLE audio transport invariants:",
    "s_connection_epoch",
    "s_pending_subscribe_epoch",
    "s_pending_mtu_epoch",
    "#if !MYNEWT_VAL(BLE_GATT_NOTIFY)",
    "BLE_AUDIO_STREAM_NOTIFY_RETRY_DELAY_MS 2",
    "BLE_AUDIO_STREAM_NOTIFY_MIN_FREE_MSYS_BLOCKS 4",
    "BLE_AUDIO_STREAM_NOTIFY_MSYS_WAIT_MS 1",
    "BLE_AUDIO_STREAM_PCM_PRODUCTION_BYTES_PER_SECOND 32000U",
    "BLE_AUDIO_STREAM_AUDIO_TARGET_BYTES_PER_SECOND 38400U",
    "BLE_AUDIO_STREAM_AUDIO_PACE_TICK_MS 10U",
    "BLE_AUDIO_STREAM_AUDIO_PACE_BYTES_PER_TICK",
    "denzic_audio_transport_v1_pacing_note_pcm_sent",
    "denzic_audio_transport_v1_replay_window_t *s_replay_window",
    "MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT",
    "audio replay window PSRAM allocation failed",
    "denzic_audio_transport_v1_replay_store(",
    "denzic_audio_transport_v1_replay_collect_pending",
    "denzic_audio_transport_v1_replay_mark_suspended",
    "denzic_audio_transport_v1_backpressure_decide",
    "BLE_AUDIO_STREAM_LOSSLESS_RICE_FLAG 0x01u",
    "BLE_AUDIO_STREAM_LOSSLESS_RICE_VERSION_V1 1u",
    "BLE_AUDIO_STREAM_LOSSLESS_RICE_VERSION_V2 2u",
    "BLE_AUDIO_STREAM_LOSSLESS_RICE_VERSION_V3 3u",
    "BLE_AUDIO_STREAM_LOSSLESS_RICE_HEADER_BYTES 6u",
    "BLE_AUDIO_STREAM_LOSSLESS_RICE_PREFERRED_PCM_BYTES 480U",
    "BLE_AUDIO_STREAM_LOSSLESS_RICE_FALLBACK_STEP_BYTES 32U",
    "BLE_AUDIO_STREAM_LOSSLESS_RICE_FINE_FALLBACK_STEP_BYTES 16U",
    "BLE_AUDIO_STREAM_LOSSLESS_RICE_FINE_FALLBACK_FLOOR_BYTES 400U",
    "s_transport_lossless_rice_version == BLE_AUDIO_STREAM_LOSSLESS_RICE_VERSION_V3",
    "payload_bytes == BLE_AUDIO_STREAM_LOSSLESS_RICE_PREFERRED_PCM_BYTES",
    "ble_audio_stream_encode_lossless_rice",
    "ble_audio_stream_plan_session_audio_packet",
    "s_transport_lossless_rice_enabled",
    "ble_audio_stream_is_type_lossless_rice_enabled",
    "TYPE:AUDIO:LOSSLESS_RICE:1",
    "TYPE:AUDIO:LOSSLESS_RICE:2",
    "TYPE:AUDIO:LOSSLESS_RICE:3",
    "BLE_AUDIO_STREAM_TYPE_RESTART_GRACE_MS 5000U",
    "type restart grace opened active connection",
    "type restart grace expired; restored low-power connection",
    "audio_wire_bytes=",
    "audio_wire_pct=",
    "((uint64_t)s_session_stats.audio_pcm_bytes_sent * 1000U)",
    "((uint64_t)s_session_stats.audio_wire_bytes_sent * 1000U)",
    "((uint64_t)s_session_stats.audio_packets_sent * 1000U)",
    "audio_lossless_packets=",
    "audio_raw_packets=",
    "single_pdu_value_max_bytes",
    "fragments into three LL PDUs",
    "BLE_GAP_EVENT_NOTIFY_TX reports only a submission attempt",
    "audio_pace_ticks=",
    "audio_pace_target_bytes_per_s=",
    "BLE_AUDIO_STREAM_AUDIO_JOB_COOPERATIVE_YIELD_BATCHES 5U",
    "consecutive_audio_jobs",
    "ble_audio_stream_wait_msys_blocks",
    "os_msys_num_free()",
    "msys_waits=",
    "s_stale_event_counts",
    "BLE_AUDIO_STREAM_LINK_RECOVERY_WAIT_MS 20000",
    "BLE_AUDIO_STREAM_REPLAY_WINDOW_PACKETS 48",
    "BLE_AUDIO_STREAM_BACKPRESSURE_PAUSE_PERCENT 95U",
    "BLE_AUDIO_STREAM_BACKPRESSURE_RESUME_PERCENT 70U",
    "ble_audio_stream_replay_store_packet",
    "ble_audio_stream_replay_remove_packet",
    "ble_audio_stream_replay_pending_packets",
    "replay_retained_high_water",
    "replay_stored=",
    "replay_replaced=",
    "replay_removed=",
    "replay_resent=",
    "replay_resend_failed=",
    "replay_skip_current=",
    "replay_pending=",
    "DIAG_BAUD_REPLAY",
    "audio session stop queued during link recovery",
]

PLATFORM_LOSSLESS_CODEC_TOKENS = [
    "denzic_audio_lossless_v1_encode",
    "denzic_audio_lossless_v1_decode",
    "DENZIC_AUDIO_LOSSLESS_V1_PREDICTOR_FIRST_ORDER",
    "DENZIC_AUDIO_LOSSLESS_V1_PREDICTOR_SECOND_ORDER",
    "DENZIC_AUDIO_LOSSLESS_V1_PREDICTOR_THIRD_ORDER",
    "DENZIC_AUDIO_LOSSLESS_V1_PREDICTOR_FOURTH_ORDER",
    "DENZIC_AUDIO_LOSSLESS_V1_MAX_K",
    "DENZIC_AUDIO_V1_LOSSLESS_RICE_V2_PARAMETER_K_SPAN",
    "DENZIC_AUDIO_V1_LOSSLESS_RICE_V3_PREDICTOR_SAMPLE_STRIDE",
    "DENZIC_AUDIO_V1_LOSSLESS_RICE_V3_MAX_PREDICTOR",
    "best_predictor << 4U",
]

GAP_SOURCE_TOKENS = [
    "s_active_connection_required",
    "s_ec11_fast_recording_armed",
    "ble_hid_gap_set_ec11_fast_recording_enabled",
    "low-power idle connection retained active: e11r fast recording is armed",
    "BLE_HID_GAP_ACTIVE_PROMOTION_RETRY_MS 50U",
    "BLE_HID_GAP_ACTIVE_PROMOTION_TIMEOUT_MS",
    "BLE_HID_GAP_AUDIO_DATA_LEN_OCTETS 251U",
    "BLE_HID_GAP_AUDIO_DATA_LEN_TIME_US 1590U",
    "BLE_HID_GAP_AUDIO_DATA_LEN_2M_PACKET_TIME_US 965U",
    "ble_hid_gap_data_length_ready_for_phy",
    "BLE_HID_GAP_ACTIVE_ITVL_MAX 6U",
    "BLE_HID_GAP_ACTIVE_FALLBACK_ITVL_MAX 12U",
    "BLE_HID_GAP_ACTIVE_MIN_CE_LEN 12U",
    "BLE_HID_GAP_ACTIVE_MAX_CE_LEN 24U",
    "ble_hid_gap_get_audio_notification_value_max_bytes",
    "ble_hid_gap_request_audio_data_length",
    "BLE_GAP_EVENT_DATA_LEN_CHG",
    "BLE_HS_EALREADY",
    "active connection promotion awaiting GAP completion event",
    "ble_hid_gap_conn_param_retry_delay_ticks",
    "ble_hid_gap_set_active_connection_required(false)",
    "ble_hid_gap_confirm_conn_param_update",
    "active connection promotion scheduled",
    "active connection promotion confirmed",
    "active connection promotion timed out",
]


def fail(message: str) -> None:
    raise SystemExit(f"FAIL: {message}")


def require(source: str, token: str, path: pathlib.Path) -> None:
    if token not in source:
        fail(f"{path.relative_to(REPO_ROOT)} missing token: {token}")


def require_regex(source: str, pattern: str, description: str, path: pathlib.Path) -> None:
    if not re.search(pattern, source, re.S):
        fail(f"{path.relative_to(REPO_ROOT)} missing {description}")


def static_source_checks() -> None:
    stream = STREAM.read_text(encoding="utf-8")
    events = EVENTS.read_text(encoding="utf-8")
    gap = GAP.read_text(encoding="utf-8")
    platform_codec = PLATFORM_LOSSLESS_CODEC.read_text(encoding="utf-8")
    platform_transport_header = PLATFORM_TRANSPORT_HEADER.read_text(encoding="utf-8")

    for token in STATE_TOKENS:
        require(stream, token, STREAM)
    for token in SOURCE_TOKENS:
        require(stream, token, STREAM)
    for token in GAP_SOURCE_TOKENS:
        require(gap, token, GAP)
    for token in PLATFORM_LOSSLESS_CODEC_TOKENS:
        require(platform_codec, token, PLATFORM_LOSSLESS_CODEC)
    require(events, "DIAG_BAUD_REPLAY", EVENTS)

    require_regex(
        stream,
        r"ble_audio_stream_begin_type_restart_grace\(.*?"
        r"ble_hid_gap_request_active_connection\(.*?"
        r"xTaskCreate\(.*?"
        r"ble_audio_stream_type_restart_grace_task",
        "Type restart grace requests an active link before scheduling its bounded rollback",
        STREAM,
    )
    require_regex(
        stream,
        r"ble_audio_stream_type_restart_grace_task\(.*?"
        r"BLE_AUDIO_STREAM_TYPE_RESTART_GRACE_MS.*?"
        r"ble_audio_stream_is_type_link_ready\(.*?"
        r"ble_hid_gap_request_low_power_connection",
        "Type restart grace retains a restored Type link or returns to low power",
        STREAM,
    )
    require_regex(
        stream,
        r"strcmp\(command, \"TYPE:BYE\"\) == 0.*?"
        r"ble_audio_stream_sync_power_manager_for_type_link\(false, command\).*?"
        r"strcmp\(command, \"TYPE:BYE\"\) == 0.*?"
        r"ble_audio_stream_begin_type_restart_grace\(\)",
        "only Type shutdown opens the bounded restart grace after normal link cleanup",
        STREAM,
    )
    require_regex(
        stream,
        r"ble_audio_stream_on_gap_disconnect\(.*?ble_audio_stream_replay_mark_link_suspended",
        "disconnect replay suspension",
        STREAM,
    )
    require_regex(
        gap,
        r"BLE_GAP_EVENT_CONN_UPDATE:.*?ble_hid_gap_confirm_conn_param_update.*?"
        r"ble_hid_gap_active_connection_required\(\).*?"
        r"ble_hid_gap_schedule_active_connection_with_delay",
        "completed low-power update re-promotes an active recording link",
        GAP,
    )
    require_regex(
        gap,
        r"ble_hid_gap_active_connection_request_task\(.*?while \(ble_hid_gap_active_connection_required\(\)\).*?"
        r"ble_hid_gap_conn_param_request_is_pending\(\).*?"
        r"awaiting_gap_completion\s*=\s*true.*?"
        r"ble_hid_gap_conn_param_retry_delay_ticks.*?"
        r"BLE_HID_GAP_ACTIVE_PROMOTION_RETRY_MS.*?"
        r"active connection promotion timed out",
        "event-owned active connection promotion retry",
        GAP,
    )
    require_regex(
        gap,
        r"rc == 0 \|\| rc == BLE_HS_EALREADY.*?"
        r"s_conn_param_request_pending_until_tick\s*=\s*portMAX_DELAY.*?"
        r"BLE_GAP_EVENT_CONN_UPDATE:.*?ble_hid_gap_confirm_conn_param_update",
        "NimBLE-owned pending update waits for the GAP completion event",
        GAP,
    )
    require_regex(
        gap,
        r"ble_hid_gap_request_low_power_connection\(.*?"
        r"ble_hid_gap_ec11_fast_recording_armed\(\).*?"
        r"ble_hid_gap_request_active_connection\(\).*?"
        r"ble_hid_gap_set_active_connection_required\(false\)",
        "armed e11r retains the active connection before low-power can apply",
        GAP,
    )
    require_regex(
        gap,
        r"ble_hid_gap_set_ec11_fast_recording_enabled\(.*?"
        r"s_ec11_fast_recording_armed\s*=\s*enabled.*?"
        r"if \(enabled\).*?ble_hid_gap_request_active_connection\(.*?"
        r"power_manager_get_state\(\)\s*==\s*POWER_MANAGER_STATE_CONNECTED_IDLE.*?"
        r"ble_hid_gap_request_low_power_connection",
        "e11r setting arms the active link and only disarms to low power from idle",
        GAP,
    )
    require_regex(
        gap,
        r"ble_hid_gap_request_active_connection\(.*?"
        r"ble_hid_gap_set_active_connection_required\(true\).*?"
        r"ble_hid_gap_request_active_connection_once\(\).*?"
        r"!ble_hid_gap_active_connection_applied\(\).*?"
        r"ble_hid_gap_schedule_active_connection_with_delay",
        "recording active request retains intent and schedules promotion",
        GAP,
    )
    require_regex(
        gap,
        r"ble_hid_gap_request_active_connection_once\(.*?"
        r"ble_hid_gap_active_connection_applied\(\).*?"
        r"ble_hid_gap_request_preferred_2m_phy\(\"active audio\"\)",
        "active audio requests 2M PHY",
        GAP,
    )
    require_regex(
        gap,
        r"const bool active_audio = mode == BLE_HID_CONN_PARAM_MODE_ACTIVE;.*?"
        r"\.min_ce_len = active_audio \? BLE_HID_GAP_ACTIVE_MIN_CE_LEN : 0,.*?"
        r"\.max_ce_len = active_audio \? BLE_HID_GAP_ACTIVE_MAX_CE_LEN : 0,",
        "active audio requests a full connection-event budget",
        GAP,
    )
    require_regex(
        gap,
        r"uint16_t ble_hid_gap_get_audio_notification_value_max_bytes\(void\).*?"
        r"const uint16_t att_value_overhead = 7U;.*?"
        r"audio_data_length_max_tx_octets - att_value_overhead",
        "DLE-derived single-PDU ATT value limit",
        GAP,
    )
    require_regex(
        gap,
        r"case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:.*?"
        r"event->phy_updated.status == 0.*?"
        r"ble_hid_gap_request_audio_data_length\(\"after 2M PHY\"\)",
        "active audio requests 251-octet data length only after 2M PHY completes",
        GAP,
    )
    require_regex(
        stream,
        r"ble_audio_stream_on_gap_subscribe\(.*?reason=notify_disabled.*?"
        r"ble_audio_stream_replay_mark_link_suspended",
        "notify-disabled replay suspension",
        STREAM,
    )
    require_regex(
        stream,
        r"#if !MYNEWT_VAL\(BLE_GATT_NOTIFY\).*?"
        r"requires NimBLE notify support for enabled-path mbuf ownership",
        "notify-disabled build guard for enabled-path mbuf ownership",
        STREAM,
    )
    require_regex(
        stream,
        r"ble_audio_stream_task\(.*?consecutive_audio_jobs.*?"
        r"BLE_AUDIO_STREAM_AUDIO_JOB_COOPERATIVE_YIELD_BATCHES.*?"
        r"vTaskDelay\(1\).*?watchdog_platform_feed_current_task",
        "continuous audio batches periodically yield to the idle task without disabling the watchdog",
        STREAM,
    )
    require_regex(
        stream,
        r"uint16_t ble_audio_stream_count_audio_packets\(.*?"
        r"s_transport_lossless_rice_version == BLE_AUDIO_STREAM_LOSSLESS_RICE_VERSION_V3.*?"
        r"payload_bytes == BLE_AUDIO_STREAM_LOSSLESS_RICE_PREFERRED_PCM_BYTES.*?"
        r"return \(uint16_t\)\(\(\(uint32_t\)pcm_bytes \+.*?"
        r"BLE_AUDIO_STREAM_LOSSLESS_RICE_PREFERRED_PCM_BYTES",
        "V3 full-DLE packet counting bypasses duplicate capture-side encoding",
        STREAM,
    )
    require_regex(
        stream,
        r"ble_audio_stream_notify_success_delay\(.*?"
        r"denzic_audio_transport_v1_pacing_note_pcm_sent\(\s*&s_audio_pacing,\s*packet_pcm_bytes\).*?"
        r"vTaskDelay",
        "PCM media-clock pacing",
        STREAM,
    )
    require_regex(
        stream,
        r"_Static_assert\(BLE_AUDIO_STREAM_AUDIO_PACE_BYTES_PER_TICK\s*==\s*"
        r"DENZIC_AUDIO_TRANSPORT_V1_PACING_BYTES_PER_TICK",
        "media-clock pacing quantum pinned to the platform contract",
        STREAM,
    )
    require_regex(
        stream,
        r"ble_audio_stream_packet_payload_bytes\(void\).*?"
        r"single_pdu_value_max_bytes = BLE_AUDIO_STREAM_PACKET_DEFAULT_BYTES;.*?"
        r"ble_hid_gap_get_audio_notification_value_max_bytes\(\).*?"
        r"packet_value_max_bytes > single_pdu_value_max_bytes",
        "audio payload cap prevents ATT fragmentation from consuming connection events",
        STREAM,
    )
    require_regex(
        stream,
        r"TYPE:AUDIO:LOSSLESS_RICE:3.*?"
        r"ble_audio_stream_set_type_lossless_rice_capable\(\s*true,\s*"
        r"BLE_AUDIO_STREAM_LOSSLESS_RICE_VERSION_V3",
        "Type-negotiated lossless Rice V3 capability",
        STREAM,
    )
    require_regex(
        stream,
        r"TYPE:AUDIO:LOSSLESS_RICE:2.*?"
        r"ble_audio_stream_set_type_lossless_rice_capable\(\s*true,\s*"
        r"BLE_AUDIO_STREAM_LOSSLESS_RICE_VERSION_V2",
        "Type-negotiated lossless Rice V2 capability",
        STREAM,
    )
    require_regex(
        stream,
        r"TYPE:AUDIO:LOSSLESS_RICE:1.*?"
        r"ble_audio_stream_set_type_lossless_rice_capable\(\s*true,\s*"
        r"BLE_AUDIO_STREAM_LOSSLESS_RICE_VERSION_V1",
        "Type-negotiated lossless Rice V1 compatibility capability",
        STREAM,
    )
    require_regex(
        stream,
        r"ble_audio_stream_on_gap_connect\(.*?"
        r"s_type_lossless_rice_capable\s*=\s*false",
        "lossless capability resets on a new link epoch",
        STREAM,
    )
    require_regex(
        stream,
        r"ble_audio_stream_on_gap_disconnect\(.*?"
        r"s_type_lossless_rice_capable\s*=\s*false",
        "lossless capability resets after disconnect",
        STREAM,
    )
    require_regex(
        stream,
        r"ble_audio_stream_send_session_start\(.*?"
        r"s_transport_lossless_rice_version\s*=\s*"
        r"ble_audio_stream_get_type_lossless_rice_version\(\).*?"
        r"s_transport_lossless_rice_enabled\s*=\s*"
        r"s_transport_lossless_rice_version\s*!=\s*0",
        "session freezes Type-negotiated lossless version before packet planning",
        STREAM,
    )
    require_regex(
        platform_codec,
        r"denzic_audio_lossless_v1_encode\(.*?"
        r"version == DENZIC_AUDIO_LOSSLESS_V1_VERSION_V3.*?"
        r"DENZIC_AUDIO_V1_LOSSLESS_RICE_V3_MAX_PREDICTOR.*?"
        r"best_predictor << 4U",
        "platform codec selects a predictor within the negotiated version",
        PLATFORM_LOSSLESS_CODEC,
    )
    require_regex(
        stream,
        r"ble_audio_stream_encode_lossless_rice\(.*?"
        r"denzic_audio_lossless_v1_encode\(.*?"
        r"ble_audio_stream_plan_session_audio_packet\(.*?"
        r"ble_audio_stream_session_lossless_rice_enabled\(\).*?"
        r"BLE_AUDIO_STREAM_LOSSLESS_RICE_PREFERRED_PCM_BYTES.*?"
        r"ble_audio_stream_encode_lossless_rice\(.*?"
        r"s_transport_lossless_rice_version.*?"
        r"BLE_AUDIO_STREAM_LOSSLESS_RICE_FLAG",
        "versioned lossless packet plan delegates encoding to the platform codec",
        STREAM,
    )
    require_regex(
        stream,
        r"ble_audio_stream_count_audio_packets\(.*?"
        r"ble_audio_stream_plan_session_audio_packet",
        "producer packet count uses the exact adaptive packet plan",
        STREAM,
    )
    require_regex(
        stream,
        r"ble_audio_stream_send_session_audio_internal\(.*?"
        r"ble_audio_stream_plan_session_audio_packet.*?"
        r"packet_count\s*!=\s*expected_packet_count",
        "export task verifies its adaptive packet plan against the queued sequence count",
        STREAM,
    )
    require_regex(
        platform_transport_header,
        r"typedef struct \{.*?packet_pcm_bytes;\s*uint8_t flags;.*?"
        r"payload\[DENZIC_AUDIO_TRANSPORT_V1_REPLAY_PAYLOAD_BYTES\]",
        "platform replay packet preserves PCM byte count and flags",
        PLATFORM_TRANSPORT_HEADER,
    )
    require_regex(
        stream,
        r"ble_audio_stream_replay_store_packet\(.*?uint8_t flags\).*?"
        r"denzic_audio_transport_v1_replay_store\(.*?"
        r"packets\[i\]->flags",
        "replay preserves the negotiated lossless frame flag",
        STREAM,
    )
    require_regex(
        stream,
        r"ble_gatts_notify_custom\(link\.conn_handle, s_notify_attr_handle, om\).*?"
        r"NimBLE consumes om on every enabled notify path.*?"
        r"BLE_GAP_EVENT_NOTIFY_TX.*?only a synchronous submission-attempt event.*?"
        r"if \(rc == BLE_HS_ENOMEM\).*?ble_audio_stream_notify_retry_delay\(\).*?continue;",
        "NimBLE notify failure ownership comment and ENOMEM retry path",
        STREAM,
    )
    if "s_notify_credit_sem" in stream:
        fail("notify submission attempts must not manufacture completion credits")
    require_regex(
        stream,
        r"ble_audio_stream_send_packet\(.*?LISTENER_AUDIO_PACKET_TYPE_SESSION_STOP.*?"
        r"ble_audio_stream_replay_pending_packets",
        "stop drains replay before terminal notify",
        STREAM,
    )
    require_regex(
        stream,
        r"ble_audio_stream_get_backpressure\(.*?"
        r"denzic_audio_transport_v1_backpressure_decide\(\s*"
        r"ble_audio_stream_get_backpressure_active\(\),\s*"
        r"snapshot->transport_session_active,\s*snapshot->pressure_percent\s*\)",
        "true-capacity backpressure hysteresis delegates to the shared platform core",
        STREAM,
    )
    require_regex(
        stream,
        r"_Static_assert\(BLE_AUDIO_STREAM_BACKPRESSURE_PAUSE_PERCENT\s*==\s*"
        r"DENZIC_AUDIO_TRANSPORT_V1_BACKPRESSURE_PAUSE_PERCENT.*?"
        r"_Static_assert\(BLE_AUDIO_STREAM_BACKPRESSURE_RESUME_PERCENT\s*==\s*"
        r"DENZIC_AUDIO_TRANSPORT_V1_BACKPRESSURE_RESUME_PERCENT",
        "backpressure thresholds pinned to the platform contract",
        STREAM,
    )


@dataclass
class TransportModel:
    state: str = "disconnected"
    epoch: int = 0
    conn_handle: int | None = None
    mtu_ready: bool = False
    notify_enabled: bool = False
    active_session: int | None = None
    replay_window: dict[int, str] = field(default_factory=dict)
    replay_pending: bool = False
    replay_resent: list[int] = field(default_factory=list)
    terminal: str | None = None
    stale: dict[str, int] = field(
        default_factory=lambda: {"subscribe": 0, "mtu": 0, "notify_tx": 0, "disconnect": 0}
    )
    queue_depth: int = 0
    pool_in_use: int = 0
    pool_high_water: int = 0
    backpressure_active: bool = False
    retries: int = 0
    last_error: str | None = None
    queue_jobs_purged: int = 0

    def link_ready(self) -> bool:
        return self.conn_handle is not None and self.mtu_ready and self.notify_enabled

    def refresh_idle_state(self) -> None:
        if self.active_session is not None:
            return
        if self.conn_handle is None:
            self.state = "disconnected"
        elif not self.mtu_ready:
            self.state = "connected"
        elif not self.notify_enabled:
            self.state = "mtu_ready"
        else:
            self.state = "stream_ready"

    def connect(self, conn_handle: int) -> None:
        self.epoch += 1
        self.conn_handle = conn_handle
        self.mtu_ready = False
        self.notify_enabled = False
        self.refresh_idle_state()

    def disconnect(self, conn_handle: int) -> None:
        if conn_handle != self.conn_handle:
            self.stale["disconnect"] += 1
            return
        self.epoch += 1
        if self.active_session is not None and self.replay_window:
            self.replay_pending = True
        self.conn_handle = None
        self.mtu_ready = False
        self.notify_enabled = False
        self.refresh_idle_state()

    def mtu(self, conn_handle: int) -> None:
        if conn_handle != self.conn_handle:
            self.stale["mtu"] += 1
            return
        self.mtu_ready = True
        self.refresh_idle_state()

    def subscribe(self, conn_handle: int, enabled: bool) -> None:
        if conn_handle != self.conn_handle:
            self.stale["subscribe"] += 1
            return
        if self.active_session is not None and not enabled and self.replay_window:
            self.replay_pending = True
        self.notify_enabled = enabled
        self.refresh_idle_state()

    def notify_tx(self, conn_handle: int, epoch: int, waiting: bool = True) -> None:
        if conn_handle != self.conn_handle or epoch != self.epoch or not waiting or not self.link_ready():
            self.stale["notify_tx"] += 1

    def start(self, session_id: int) -> None:
        if self.state != "stream_ready" or not self.link_ready():
            self.last_error = "transport_not_ready"
            raise AssertionError("start requires stream_ready link")
        self.active_session = session_id
        self.state = "streaming"
        self.terminal = None

    def retain_audio(self, sequence: int) -> None:
        if self.state != "streaming" or self.active_session is None:
            raise AssertionError("audio requires active streaming session")
        replaced = sequence in self.replay_window
        self.replay_window[sequence] = "retained"
        if not replaced and len(self.replay_window) > 48:
            self.replay_window.pop(sorted(self.replay_window)[0])

    def notify_success(self, sequence: int) -> None:
        self.replay_window.pop(sequence, None)

    def drain_replay(self, skip_current: int | None = None) -> None:
        if not self.replay_pending:
            return
        for sequence in sorted(self.replay_window):
            if skip_current is not None and sequence == skip_current:
                continue
            self.replay_resent.append(sequence)
        self.replay_pending = False

    def stop(self) -> None:
        if self.active_session is None:
            self.last_error = "stale_stop"
            return
        self.state = "draining"
        if self.link_ready():
            self.drain_replay()
            self.terminal = "stop"
            self.active_session = None
            self.state = "stopped"

    def complete_pending_stop_after_recovery(self) -> None:
        if self.state != "draining" or self.active_session is None:
            raise AssertionError("pending stop requires draining active session")
        if not self.link_ready():
            raise AssertionError("pending stop requires recovered link")
        self.drain_replay()
        self.terminal = "stop"
        self.active_session = None
        self.state = "stopped"

    def cancel(self) -> None:
        if self.active_session is None:
            self.last_error = "stale_cancel"
            return
        self.queue_jobs_purged += self.queue_depth
        self.queue_depth = 0
        self.replay_window.clear()
        self.replay_pending = False
        self.terminal = "cancel"
        self.active_session = None
        self.state = "stopped" if self.link_ready() else "error"

    def pressure_update(self, queue_depth: int, pool_in_use: int) -> None:
        self.queue_depth = queue_depth
        self.pool_in_use = pool_in_use
        self.pool_high_water = max(self.pool_high_water, pool_in_use)
        pressure = max(queue_depth * 100 // 48, pool_in_use * 100 // 52)
        if self.active_session is None:
            self.backpressure_active = False
        elif pressure >= 95:
            self.backpressure_active = True
        elif pressure <= 70:
            self.backpressure_active = False

    def exhaust_pool(self) -> None:
        self.pool_in_use = 52
        self.pool_high_water = 52
        self.last_error = "pool_exhausted"

    def bounded_retry_timeout(self, limit: int = 80) -> None:
        self.retries = limit
        self.last_error = "notify_timeout"
        self.state = "error"


@dataclass
class ConnectionParameterModel:
    mode: str = "active"
    pending_mode: str | None = None
    active_required: bool = False
    fast_recording_armed: bool = False
    host_update_in_progress: bool = False
    promotion_retries: int = 0

    def request_low_power(self) -> None:
        if self.fast_recording_armed:
            self.request_active()
            return
        self.active_required = False
        self.pending_mode = "low_power"

    def set_fast_recording_armed(self, enabled: bool) -> None:
        self.fast_recording_armed = enabled
        if enabled:
            self.request_active()

    def request_active(self) -> None:
        self.active_required = True
        if self.host_update_in_progress:
            return
        if self.pending_mode is None:
            self.pending_mode = "active"

    def complete_host_update(self) -> None:
        self.host_update_in_progress = False
        self.complete_pending_update()

    def complete_pending_update(self) -> None:
        if self.pending_mode is None:
            raise AssertionError("completion requires a pending parameter update")
        self.mode = self.pending_mode
        self.pending_mode = None
        if self.active_required and self.mode != "active":
            self.promotion_retries += 1
            self.pending_mode = "active"


def ready_model() -> TransportModel:
    model = TransportModel()
    model.connect(1)
    model.mtu(1)
    model.subscribe(1, True)
    if model.state != "stream_ready":
        raise AssertionError("model did not reach stream_ready")
    return model


def case_disconnect_during_streaming() -> None:
    model = ready_model()
    model.start(100)
    model.retain_audio(1)
    model.disconnect(1)
    assert model.active_session == 100
    assert model.replay_pending
    model.connect(2)
    model.mtu(2)
    model.subscribe(2, True)
    model.drain_replay()
    assert model.replay_resent == [1]
    model.stop()
    assert model.terminal == "stop"


def case_notify_disabled_during_streaming() -> None:
    model = ready_model()
    model.start(101)
    model.retain_audio(7)
    model.subscribe(1, False)
    assert model.active_session == 101
    assert model.replay_pending
    model.subscribe(1, True)
    model.drain_replay()
    assert model.replay_resent == [7]


def case_reconnect_before_stop() -> None:
    model = ready_model()
    model.start(102)
    model.retain_audio(2)
    model.disconnect(1)
    model.connect(2)
    model.mtu(2)
    model.subscribe(2, True)
    model.stop()
    assert model.replay_resent == [2]
    assert model.terminal == "stop"


def case_stop_during_recovery() -> None:
    model = ready_model()
    model.start(103)
    model.retain_audio(3)
    model.disconnect(1)
    model.stop()
    assert model.state == "draining"
    assert model.active_session == 103
    model.connect(2)
    model.mtu(2)
    model.subscribe(2, True)
    model.complete_pending_stop_after_recovery()
    assert model.replay_resent == [3]
    assert model.terminal == "stop"


def case_cancel_while_tail_packets_drain() -> None:
    model = ready_model()
    model.start(104)
    model.queue_depth = 3
    model.retain_audio(4)
    model.retain_audio(5)
    model.cancel()
    assert model.terminal == "cancel"
    assert model.queue_jobs_purged == 3
    assert not model.replay_window


def case_duplicate_replay_prevention() -> None:
    model = ready_model()
    model.start(105)
    model.retain_audio(6)
    model.retain_audio(6)
    model.disconnect(1)
    model.connect(2)
    model.mtu(2)
    model.subscribe(2, True)
    model.drain_replay()
    assert model.replay_resent == [6]


def case_capacity_pressure_pause_resume_hysteresis() -> None:
    model = ready_model()
    model.start(106)
    model.pressure_update(queue_depth=16, pool_in_use=4)
    assert not model.backpressure_active
    model.pressure_update(queue_depth=46, pool_in_use=4)
    assert model.backpressure_active
    model.pressure_update(queue_depth=30, pool_in_use=4)
    assert not model.backpressure_active


def case_pool_exhaustion() -> None:
    model = ready_model()
    model.start(107)
    model.exhaust_pool()
    assert model.pool_high_water == 52
    assert model.last_error == "pool_exhausted"


def case_stale_gatt_event_after_epoch_advance() -> None:
    model = ready_model()
    model.disconnect(1)
    model.connect(2)
    model.subscribe(1, True)
    model.mtu(1)
    model.notify_tx(1, 1)
    model.disconnect(1)
    assert model.stale == {"subscribe": 1, "mtu": 1, "notify_tx": 1, "disconnect": 1}


def case_bounded_retry_timeout() -> None:
    model = ready_model()
    model.start(108)
    model.bounded_retry_timeout()
    assert model.retries == 80
    assert model.state == "error"
    assert model.last_error == "notify_timeout"


def case_media_clock_drains_faster_than_pcm_production() -> None:
    pcm_bytes_per_second = 32000
    target_bytes_per_second = 38400
    pace_tick_ms = 10
    pace_bytes_per_tick = target_bytes_per_second * pace_tick_ms // 1000
    packet_pcm_bytes = 480
    packet_count = pcm_bytes_per_second * 60 // packet_pcm_bytes
    debt_bytes = 0
    pace_ticks = 0

    for _ in range(packet_count):
        debt_bytes += packet_pcm_bytes
        pace_ticks += debt_bytes // pace_bytes_per_tick
        debt_bytes %= pace_bytes_per_tick

    assert debt_bytes == 0
    assert target_bytes_per_second > pcm_bytes_per_second
    assert pace_ticks * pace_bytes_per_tick == packet_count * packet_pcm_bytes
    assert pace_ticks == 5000


def case_long_session_rate_metrics_do_not_wrap() -> None:
    pcm_bytes = 9_198_720
    wire_bytes = 3_769_368
    packets = 21_970
    elapsed_ms = 287_660

    assert pcm_bytes * 1000 // elapsed_ms == 31_977
    assert wire_bytes * 1000 // elapsed_ms == 13_103
    assert packets * 1000 // elapsed_ms == 76
    assert ((pcm_bytes * 1000) & 0xFFFFFFFF) // elapsed_ms == 2_116


def case_single_pdu_budget_beats_pcm_production() -> None:
    dle_tx_octets = 251
    l2cap_and_att_headers = 7
    protocol_header = 20
    active_interval_seconds = 6 * 1.25 / 1000
    active_min_ce_len_us = 8 * 625
    active_max_ce_len_us = 12 * 625
    max_pdu_airtime_us = 1590

    single_pdu_value = dle_tx_octets - l2cap_and_att_headers
    pcm_payload = single_pdu_value - protocol_header
    assert single_pdu_value == 244
    assert pcm_payload == 224
    assert active_min_ce_len_us >= 2 * max_pdu_airtime_us
    assert active_max_ce_len_us == int(active_interval_seconds * 1_000_000)
    assert 2 * pcm_payload / active_interval_seconds > 32000


RICE_VERSION_V1 = 1
RICE_VERSION_V2 = 2
RICE_VERSION_V3 = 3
RICE_HEADER_BYTES = 6
RICE_MAX_K = 15
RICE_MAX_ZIGZAG_RESIDUAL = 1_048_560
RICE_PREDICTOR_FIRST_ORDER = 1
RICE_PREDICTOR_SECOND_ORDER = 2
RICE_PREDICTOR_THIRD_ORDER = 3
RICE_PREDICTOR_FOURTH_ORDER = 4
RICE_V2_PARAMETER_K_SPAN = 1
LOSSLESS_RICE_PREFERRED_PCM_BYTES = 480
LOSSLESS_RICE_FALLBACK_STEP_BYTES = 32
LOSSLESS_RICE_FINE_FALLBACK_STEP_BYTES = 16
LOSSLESS_RICE_FINE_FALLBACK_FLOOR_BYTES = 400
SINGLE_PDU_PCM_PAYLOAD_BYTES = 224


def rice_zigzag(residual: int) -> int:
    return residual * 2 if residual >= 0 else -residual * 2 - 1


def rice_residual(samples: list[int], sample_offset: int, predictor: int) -> int:
    if predictor == RICE_PREDICTOR_FIRST_ORDER:
        return samples[sample_offset] - samples[sample_offset - 1]
    if predictor == RICE_PREDICTOR_SECOND_ORDER:
        return samples[sample_offset] - 2 * samples[sample_offset - 1] + samples[sample_offset - 2]
    if predictor == RICE_PREDICTOR_THIRD_ORDER:
        return (
            samples[sample_offset]
            - 3 * samples[sample_offset - 1]
            + 3 * samples[sample_offset - 2]
            - samples[sample_offset - 3]
        )
    assert predictor == RICE_PREDICTOR_FOURTH_ORDER
    return (
        samples[sample_offset]
        - 4 * samples[sample_offset - 1]
        + 6 * samples[sample_offset - 2]
        - 4 * samples[sample_offset - 3]
        + samples[sample_offset - 4]
    )


def rice_header_bytes(version: int, predictor: int) -> int:
    return 2 + predictor * 2 if version == RICE_VERSION_V3 else RICE_HEADER_BYTES


def rice_encode(pcm: bytes, version: int = RICE_VERSION_V1) -> bytes:
    assert len(pcm) >= 6 and len(pcm) % 2 == 0
    assert version in (RICE_VERSION_V1, RICE_VERSION_V2, RICE_VERSION_V3)
    samples = [int.from_bytes(pcm[offset : offset + 2], "little", signed=True) for offset in range(0, len(pcm), 2)]
    predictors = (
        (RICE_PREDICTOR_SECOND_ORDER,)
        if version == RICE_VERSION_V1
        else (RICE_PREDICTOR_FIRST_ORDER, RICE_PREDICTOR_SECOND_ORDER)
        if version == RICE_VERSION_V2
        else (
            RICE_PREDICTOR_FIRST_ORDER,
            RICE_PREDICTOR_SECOND_ORDER,
            RICE_PREDICTOR_THIRD_ORDER,
            RICE_PREDICTOR_FOURTH_ORDER,
        )
    )
    best_predictor = RICE_PREDICTOR_SECOND_ORDER
    best_total: int | None = None
    best_score: int | None = None
    predictor_sample_stride = 16 if version == RICE_VERSION_V3 else 1
    for predictor in predictors:
        first_residual_sample = predictor if version == RICE_VERSION_V3 else RICE_PREDICTOR_SECOND_ORDER
        if len(samples) <= first_residual_sample:
            continue
        total = sum(
            rice_zigzag(rice_residual(samples, sample_offset, predictor))
            for sample_offset in range(first_residual_sample, len(samples), predictor_sample_stride)
        )
        score = total + rice_header_bytes(version, predictor) * 8
        if best_score is None or score < best_score:
            best_score = score
            best_total = total
            best_predictor = predictor

    assert best_total is not None
    first_residual_sample = (
        best_predictor if version == RICE_VERSION_V3 else RICE_PREDICTOR_SECOND_ORDER
    )
    if version == RICE_VERSION_V3:
        best_total = sum(
            rice_zigzag(rice_residual(samples, sample_offset, best_predictor))
            for sample_offset in range(first_residual_sample, len(samples))
        )
    residual_count = len(samples) - first_residual_sample
    if version == RICE_VERSION_V1:
        rice_ks = range(RICE_MAX_K + 1)
    else:
        average_zigzag = (best_total + residual_count - 1) // residual_count
        estimated_k = 0
        while average_zigzag > 1 and estimated_k < RICE_MAX_K:
            average_zigzag >>= 1
            estimated_k += 1
        rice_ks = (
            range(estimated_k, estimated_k + 1)
            if version == RICE_VERSION_V3
            else range(
                max(0, estimated_k - RICE_V2_PARAMETER_K_SPAN),
                min(RICE_MAX_K, estimated_k + RICE_V2_PARAMETER_K_SPAN) + 1,
            )
        )

    best_k = 0
    best_bits: int | None = None
    for rice_k in rice_ks:
        bits = rice_header_bytes(version, best_predictor) * 8
        for sample_offset in range(first_residual_sample, len(samples)):
            bits += (rice_zigzag(rice_residual(samples, sample_offset, best_predictor)) >> rice_k) + 1 + rice_k
        if best_bits is None or bits < best_bits:
            best_bits = bits
            best_k = rice_k

    assert best_bits is not None
    output = bytearray((best_bits + 7) // 8)
    output[0] = version
    output[1] = (best_predictor << 4) | best_k if version != RICE_VERSION_V1 else best_k
    seed_count = best_predictor if version == RICE_VERSION_V3 else RICE_PREDICTOR_SECOND_ORDER
    for seed_index in range(seed_count):
        offset = 2 + seed_index * 2
        output[offset : offset + 2] = samples[seed_index].to_bytes(2, "little", signed=True)
    bit_offset = rice_header_bytes(version, best_predictor) * 8
    for sample_offset in range(first_residual_sample, len(samples)):
        residual = rice_residual(samples, sample_offset, best_predictor)
        zigzag = rice_zigzag(residual)
        bit_offset += zigzag >> best_k
        output[bit_offset // 8] |= 1 << (bit_offset % 8)
        bit_offset += 1
        for bit_index in range(best_k):
            if zigzag & (1 << bit_index):
                output[bit_offset // 8] |= 1 << (bit_offset % 8)
            bit_offset += 1
    return bytes(output)


def rice_decode(payload: bytes, expected_pcm_bytes: int) -> bytes:
    assert expected_pcm_bytes >= 4 and expected_pcm_bytes % 2 == 0
    assert len(payload) >= 2
    if payload[0] == RICE_VERSION_V1:
        predictor = RICE_PREDICTOR_SECOND_ORDER
        rice_k = payload[1]
        seed_count = 2
    elif payload[0] == RICE_VERSION_V2:
        predictor = payload[1] >> 4
        rice_k = payload[1] & RICE_MAX_K
        seed_count = 2
    else:
        assert payload[0] == RICE_VERSION_V3
        predictor = payload[1] >> 4
        rice_k = payload[1] & RICE_MAX_K
        seed_count = predictor
    assert predictor in (
        RICE_PREDICTOR_FIRST_ORDER,
        RICE_PREDICTOR_SECOND_ORDER,
        RICE_PREDICTOR_THIRD_ORDER,
        RICE_PREDICTOR_FOURTH_ORDER,
    )
    assert payload[0] == RICE_VERSION_V3 or predictor <= RICE_PREDICTOR_SECOND_ORDER
    assert rice_k <= RICE_MAX_K
    assert len(payload) >= rice_header_bytes(payload[0], predictor)
    assert expected_pcm_bytes // 2 >= seed_count
    samples = [
        int.from_bytes(payload[2 + sample_index * 2 : 4 + sample_index * 2], "little", signed=True)
        for sample_index in range(seed_count)
    ]
    bit_offset = rice_header_bytes(payload[0], predictor) * 8
    for _ in range(seed_count, expected_pcm_bytes // 2):
        quotient = 0
        while True:
            assert bit_offset < len(payload) * 8
            bit = payload[bit_offset // 8] & (1 << (bit_offset % 8))
            bit_offset += 1
            if bit:
                break
            quotient += 1
            assert quotient <= RICE_MAX_ZIGZAG_RESIDUAL >> rice_k
        remainder = 0
        for bit_index in range(rice_k):
            assert bit_offset < len(payload) * 8
            if payload[bit_offset // 8] & (1 << (bit_offset % 8)):
                remainder |= 1 << bit_index
            bit_offset += 1
        zigzag = (quotient << rice_k) | remainder
        assert zigzag <= RICE_MAX_ZIGZAG_RESIDUAL
        residual = (zigzag >> 1) ^ -(zigzag & 1)
        if predictor == RICE_PREDICTOR_FIRST_ORDER:
            sample = samples[-1] + residual
        elif predictor == RICE_PREDICTOR_SECOND_ORDER:
            sample = 2 * samples[-1] - samples[-2] + residual
        elif predictor == RICE_PREDICTOR_THIRD_ORDER:
            sample = 3 * samples[-1] - 3 * samples[-2] + samples[-3] + residual
        else:
            sample = 4 * samples[-1] - 6 * samples[-2] + 4 * samples[-3] - samples[-4] + residual
        assert -(2**15) <= sample < 2**15
        samples.append(sample)
    return b"".join(sample.to_bytes(2, "little", signed=True) for sample in samples)


def plan_lossless_rice_packet(
    pcm: bytes,
    payload_bytes: int = SINGLE_PDU_PCM_PAYLOAD_BYTES,
    rice_version: int = RICE_VERSION_V1,
) -> tuple[int, bytes, bool]:
    assert pcm and payload_bytes > 0
    raw_packet_bytes = min(len(pcm), payload_bytes)
    if len(pcm) < RICE_HEADER_BYTES or len(pcm) % 2:
        return raw_packet_bytes, pcm[:raw_packet_bytes], False

    candidate = min(len(pcm), LOSSLESS_RICE_PREFERRED_PCM_BYTES) & ~1
    while candidate >= raw_packet_bytes and candidate >= RICE_HEADER_BYTES:
        compressed = rice_encode(pcm[:candidate], rice_version)
        if len(compressed) < candidate and len(compressed) <= payload_bytes:
            return candidate, compressed, True
        if candidate == raw_packet_bytes:
            break
        fallback_step = (
            LOSSLESS_RICE_FINE_FALLBACK_STEP_BYTES
            if candidate > LOSSLESS_RICE_FINE_FALLBACK_FLOOR_BYTES
            else LOSSLESS_RICE_FALLBACK_STEP_BYTES
        )
        candidate = (
            raw_packet_bytes
            if candidate - raw_packet_bytes <= fallback_step
            else candidate - fallback_step
        )
    return raw_packet_bytes, pcm[:raw_packet_bytes], False


def packetize_lossless_rice(
    pcm: bytes,
    rice_version: int = RICE_VERSION_V1,
) -> list[tuple[bytes, int, bytes, bool]]:
    packets: list[tuple[bytes, int, bytes, bool]] = []
    offset = 0
    while offset < len(pcm):
        packet_pcm_bytes, wire_payload, compressed = plan_lossless_rice_packet(
            pcm[offset:],
            rice_version=rice_version,
        )
        assert packet_pcm_bytes > 0
        packets.append((pcm[offset : offset + packet_pcm_bytes], packet_pcm_bytes, wire_payload, compressed))
        offset += packet_pcm_bytes
    return packets


def case_lossless_predictive_rice_round_trip_and_wire_budget() -> None:
    speech_like = [
        int(5000 * sin(index * 0.14) + 900 * sin(index * 0.47))
        for index in range(112)
    ]
    raw = b"".join(sample.to_bytes(2, "little", signed=True) for sample in speech_like)
    compressed = rice_encode(raw, RICE_VERSION_V2)
    assert rice_decode(compressed, len(raw)) == raw
    assert len(compressed) < len(raw)
    # The observed Windows link sustained about 26 kB/s. At the firmware's
    # 38.4 kB/s raw media clock, this representative speech packet must fit
    # beneath that wire budget rather than recreate the old queue growth.
    assert len(compressed) * 38_400 <= len(raw) * 26_000


def case_lossless_predictive_rice_preserves_extremes_and_falls_back_for_noise() -> None:
    extremes = [32767, -32768, 32767, -32768, 32767]
    raw_extremes = b"".join(sample.to_bytes(2, "little", signed=True) for sample in extremes)
    assert rice_decode(rice_encode(raw_extremes, RICE_VERSION_V2), len(raw_extremes)) == raw_extremes

    state = 0xC0FFEE
    noisy_samples: list[int] = []
    for _ in range(112):
        state = (state * 1_103_515_245 + 12_345) & 0xFFFFFFFF
        noisy_samples.append((state >> 16) - 32768)
    raw_noise = b"".join(sample.to_bytes(2, "little", signed=True) for sample in noisy_samples)
    noisy_encoded = rice_encode(raw_noise, RICE_VERSION_V2)
    assert rice_decode(noisy_encoded, len(raw_noise)) == raw_noise
    assert len(noisy_encoded) >= len(raw_noise)


def case_lossless_adaptive_packet_plan_reduces_notification_demand() -> None:
    speech_like = [
        int(5000 * sin(index * 0.14) + 900 * sin(index * 0.47))
        for index in range(960)
    ]
    raw_speech = b"".join(sample.to_bytes(2, "little", signed=True) for sample in speech_like)
    packets = packetize_lossless_rice(raw_speech, RICE_VERSION_V2)
    legacy_packet_count = (len(raw_speech) + SINGLE_PDU_PCM_PAYLOAD_BYTES - 1) // SINGLE_PDU_PCM_PAYLOAD_BYTES

    assert len(packets) < legacy_packet_count
    assert len(packets) <= 6
    assert all(len(wire_payload) <= SINGLE_PDU_PCM_PAYLOAD_BYTES for _, _, wire_payload, _ in packets)
    assert all(compressed for _, _, _, compressed in packets)
    steady_packet_pcm_bytes = min(packet_pcm_bytes for _, packet_pcm_bytes, _, _ in packets[:-1])
    assert steady_packet_pcm_bytes >= 368
    reconstructed = b"".join(
        rice_decode(wire_payload, packet_pcm_bytes) if compressed else raw_packet
        for raw_packet, packet_pcm_bytes, wire_payload, compressed in packets
    )
    assert reconstructed == raw_speech
    assert (32_000 + steady_packet_pcm_bytes - 1) // steady_packet_pcm_bytes <= 87
    assert 87 < 120

    state = 0xC0FFEE
    noise_samples: list[int] = []
    for _ in range(960):
        state = (state * 1_103_515_245 + 12_345) & 0xFFFFFFFF
        noise_samples.append((state >> 16) - 32768)
    raw_noise = b"".join(sample.to_bytes(2, "little", signed=True) for sample in noise_samples)
    noise_packets = packetize_lossless_rice(raw_noise, RICE_VERSION_V2)
    assert any(not compressed for _, _, _, compressed in noise_packets)
    assert all(
        packet_pcm_bytes <= SINGLE_PDU_PCM_PAYLOAD_BYTES
        for _, packet_pcm_bytes, _, compressed in noise_packets
        if not compressed
    )


def case_lossless_v2_first_order_reduces_noisy_voice_notification_rate() -> None:
    state = 1
    sample = 0
    random_walk: list[int] = []
    for _ in range(1_920):
        state = (state * 1_103_515_245 + 12_345) & 0xFFFFFFFF
        sample = max(-32_768, min(32_767, sample + ((state >> 24) - 128)))
        random_walk.append(sample)
    raw = b"".join(sample.to_bytes(2, "little", signed=True) for sample in random_walk)

    v1_packets = packetize_lossless_rice(raw, RICE_VERSION_V1)
    v2_packets = packetize_lossless_rice(raw, RICE_VERSION_V2)
    assert len(v2_packets) < len(v1_packets)
    assert all(
        rice_decode(wire_payload, packet_pcm_bytes) == raw_packet
        for raw_packet, packet_pcm_bytes, wire_payload, compressed in v2_packets
        if compressed
    )
    assert min(packet_pcm_bytes for _, packet_pcm_bytes, _, _ in v2_packets[:-1]) >= 400


def case_lossless_v3_higher_order_reduces_smooth_voice_notification_rate() -> None:
    samples = [
        max(-32_768, min(32_767, (index - 48) ** 3 // 8))
        for index in range(96)
    ]
    raw = b"".join(sample.to_bytes(2, "little", signed=True) for sample in samples)
    v2 = rice_encode(raw, RICE_VERSION_V2)
    v3 = rice_encode(raw, RICE_VERSION_V3)

    assert len(v3) < len(v2)
    assert rice_decode(v3, len(raw)) == raw


def case_recording_promotion_wins_over_pending_low_power() -> None:
    model = ConnectionParameterModel()
    model.request_low_power()
    model.request_active()
    model.complete_pending_update()
    assert model.mode == "low_power"
    assert model.active_required
    assert model.promotion_retries == 1
    assert model.pending_mode == "active"
    model.complete_pending_update()
    assert model.mode == "active"
    assert model.active_required
    assert model.pending_mode is None


def case_e11r_armed_before_connected_idle_prevents_slow_link() -> None:
    model = ConnectionParameterModel(mode="active")
    model.set_fast_recording_armed(True)
    assert model.active_required
    assert model.pending_mode == "active"
    model.complete_pending_update()
    assert model.mode == "active"
    model.request_low_power()
    assert model.mode == "active"
    assert model.pending_mode == "active"
    model.set_fast_recording_armed(False)
    model.request_low_power()
    assert not model.active_required
    assert model.pending_mode == "low_power"


def case_e11r_repromotes_after_host_downgrade_completion() -> None:
    model = ConnectionParameterModel(mode="active")
    model.set_fast_recording_armed(True)
    model.complete_pending_update()
    assert model.mode == "active"
    assert model.active_required

    # Windows owns the next update and completes it at low power. The firmware
    # must wait for that real completion, then serialize a promotion back to active.
    model.host_update_in_progress = True
    model.pending_mode = "low_power"
    model.complete_host_update()
    assert model.mode == "low_power"
    assert model.active_required
    assert model.promotion_retries == 1
    assert model.pending_mode == "active"

    model.complete_pending_update()
    assert model.mode == "active"
    assert model.pending_mode is None


def case_active_request_waits_for_existing_host_transaction() -> None:
    model = ConnectionParameterModel(mode="low_power", pending_mode="low_power")
    model.host_update_in_progress = True
    model.request_active()
    assert model.active_required
    assert model.pending_mode == "low_power"
    model.complete_host_update()
    assert model.mode == "low_power"
    assert model.pending_mode == "active"
    model.complete_pending_update()
    assert model.mode == "active"


CASES = [
    case_disconnect_during_streaming,
    case_notify_disabled_during_streaming,
    case_reconnect_before_stop,
    case_stop_during_recovery,
    case_cancel_while_tail_packets_drain,
    case_duplicate_replay_prevention,
    case_capacity_pressure_pause_resume_hysteresis,
    case_pool_exhaustion,
    case_stale_gatt_event_after_epoch_advance,
    case_bounded_retry_timeout,
    case_media_clock_drains_faster_than_pcm_production,
    case_long_session_rate_metrics_do_not_wrap,
    case_single_pdu_budget_beats_pcm_production,
    case_lossless_predictive_rice_round_trip_and_wire_budget,
    case_lossless_predictive_rice_preserves_extremes_and_falls_back_for_noise,
    case_lossless_adaptive_packet_plan_reduces_notification_demand,
    case_lossless_v2_first_order_reduces_noisy_voice_notification_rate,
    case_lossless_v3_higher_order_reduces_smooth_voice_notification_rate,
    case_recording_promotion_wins_over_pending_low_power,
    case_e11r_armed_before_connected_idle_prevents_slow_link,
    case_e11r_repromotes_after_host_downgrade_completion,
    case_active_request_waits_for_existing_host_transaction,
]


def main() -> int:
    static_source_checks()
    for case in CASES:
        case()
    print(
        "PASS: BLE audio transport model covers connection epoch, notify readiness, "
        "session ownership, replay, backpressure, stale GATT events, retry timeout, "
        "PCM media-clock pacing, single-PDU DLE sizing, lossless predictive Rice, and "
        "event-owned recording connection promotion and pre-armed e11r low-power exclusion "
        f"across {len(CASES)} extreme cases."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
