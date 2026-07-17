from __future__ import annotations

import pathlib
import re
from dataclasses import dataclass, field


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
STREAM = REPO_ROOT / "ports" / "esp32" / "ble_audio_stream" / "ble_audio_stream_esp32.c"
EVENTS = REPO_ROOT / "components" / "diag_log" / "include" / "diag_log_events.h"
GAP = REPO_ROOT / "ports" / "esp32" / "ble_hid_gap" / "ble_hid_gap_esp32.c"


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
    "s_notify_tx_inflight_epoch",
    "s_notify_tx_inflight",
    "ble_audio_stream_note_notify_tx_queued",
    "ble_audio_stream_complete_notify_tx",
    "#if !MYNEWT_VAL(BLE_GATT_NOTIFY)",
    "BLE_AUDIO_STREAM_NOTIFY_RETRY_DELAY_MS 2",
    "BLE_AUDIO_STREAM_NOTIFY_MIN_FREE_MSYS_BLOCKS 4",
    "BLE_AUDIO_STREAM_NOTIFY_MSYS_WAIT_MS 1",
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

GAP_SOURCE_TOKENS = [
    "s_active_connection_required",
    "s_ec11_fast_recording_armed",
    "ble_hid_gap_set_ec11_fast_recording_enabled",
    "low-power idle connection retained active: e11r fast recording is armed",
    "BLE_HID_GAP_ACTIVE_PROMOTION_RETRY_MS 50U",
    "BLE_HID_GAP_ACTIVE_PROMOTION_TIMEOUT_MS",
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

    for token in STATE_TOKENS:
        require(stream, token, STREAM)
    for token in SOURCE_TOKENS:
        require(stream, token, STREAM)
    for token in GAP_SOURCE_TOKENS:
        require(gap, token, GAP)
    require(events, "DIAG_BAUD_REPLAY", EVENTS)

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
        stream,
        r"ble_audio_stream_on_gap_subscribe\(.*?reason=notify_disabled.*?"
        r"ble_audio_stream_replay_mark_link_suspended",
        "notify-disabled replay suspension",
        STREAM,
    )
    require_regex(
        stream,
        r"ble_audio_stream_on_gap_notify_tx\(.*?ble_audio_stream_complete_notify_tx\(link\.connection_epoch\)",
        "notify_tx in-flight epoch guard",
        STREAM,
    )
    if "ble_audio_stream_cancel_notify_tx_queued" in stream:
        fail("notify failure path must not manually cancel GAP notify_tx credit ownership")
    require_regex(
        stream,
        r"#if !MYNEWT_VAL\(BLE_GATT_NOTIFY\).*?"
        r"requires NimBLE notify support for enabled-path mbuf ownership and notify_tx attempt pacing",
        "notify-disabled build guard for enabled-path mbuf ownership",
        STREAM,
    )
    require_regex(
        stream,
        r"ble_gatts_notify_custom\(link\.conn_handle, s_notify_attr_handle, om\).*?"
        r"BLE_GATT_NOTIFY is a build requirement above.*?"
        r"NimBLE's enabled.*?notify path consumes om regardless of the outcome.*?"
        r"BLE_GAP_EVENT_NOTIFY_TX attempt event.*?"
        r"if \(rc == BLE_HS_ENOMEM\).*?ble_audio_stream_notify_retry_delay\(\).*?continue;",
        "NimBLE notify failure ownership comment and ENOMEM retry path",
        STREAM,
    )
    if re.search(r"if \(rc == BLE_HS_ENOMEM\)[\s\S]{0,900}xSemaphoreGive\(s_notify_credit_sem\)", stream):
        fail("ENOMEM notify failure must not return notify credit outside BLE_GAP_EVENT_NOTIFY_TX")
    if re.search(r"ESP_LOGW\(\s*TAG,\s*\"notify failed:[\s\S]{0,700}xSemaphoreGive\(s_notify_credit_sem\)", stream):
        fail("generic notify failure must not return notify credit outside BLE_GAP_EVENT_NOTIFY_TX")
    require_regex(
        stream,
        r"ble_audio_stream_send_packet\(.*?LISTENER_AUDIO_PACKET_TYPE_SESSION_STOP.*?"
        r"ble_audio_stream_replay_pending_packets",
        "stop drains replay before terminal notify",
        STREAM,
    )
    require_regex(
        stream,
        r"ble_audio_stream_get_backpressure\(.*?pressure_percent\s*>=\s*"
        r"BLE_AUDIO_STREAM_BACKPRESSURE_PAUSE_PERCENT.*?pressure_percent\s*<=\s*"
        r"BLE_AUDIO_STREAM_BACKPRESSURE_RESUME_PERCENT",
        "true-capacity backpressure hysteresis",
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
    notify_window_depth: int = 3
    notify_credits: int = 3
    notify_inflight: int = 0
    manual_failure_credit_returns: int = 0

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
            return
        if self.notify_inflight <= 0:
            self.stale["notify_tx"] += 1
            return
        self.notify_inflight -= 1
        self.notify_credits = min(self.notify_credits + 1, self.notify_window_depth)

    def take_notify_credit(self) -> None:
        if self.notify_credits <= 0:
            raise AssertionError("notify credit unavailable")
        self.notify_credits -= 1
        self.notify_inflight += 1

    def immediate_notify_failure(self) -> None:
        if self.conn_handle is None:
            raise AssertionError("notify failure requires a connection")
        self.take_notify_credit()
        self.notify_tx(self.conn_handle, self.epoch)
        # The enabled ble_gatts_notify_custom path emits a notify_tx attempt
        # event on failure, so the firmware branch must not return a second
        # credit.
        assert self.manual_failure_credit_returns == 0

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


def case_notify_failure_event_owns_credit() -> None:
    model = ready_model()
    model.start(109)
    model.immediate_notify_failure()
    assert model.notify_credits == model.notify_window_depth
    assert model.notify_inflight == 0
    assert model.manual_failure_credit_returns == 0


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
    case_notify_failure_event_owns_credit,
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
        "notify-failure credit ownership, and "
        "event-owned recording connection promotion and pre-armed e11r low-power exclusion "
        f"across {len(CASES)} extreme cases."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
