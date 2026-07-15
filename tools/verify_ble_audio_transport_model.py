from __future__ import annotations

import pathlib
import re
from dataclasses import dataclass, field


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
STREAM = REPO_ROOT / "ports" / "esp32" / "ble_audio_stream" / "ble_audio_stream_esp32.c"
EVENTS = REPO_ROOT / "components" / "diag_log" / "include" / "diag_log_events.h"


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
    "s_notify_tx_wait_epoch",
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

    for token in STATE_TOKENS:
        require(stream, token, STREAM)
    for token in SOURCE_TOKENS:
        require(stream, token, STREAM)
    require(events, "DIAG_BAUD_REPLAY", EVENTS)

    require_regex(
        stream,
        r"ble_audio_stream_on_gap_disconnect\(.*?ble_audio_stream_replay_mark_link_suspended",
        "disconnect replay suspension",
        STREAM,
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
        r"ble_audio_stream_on_gap_notify_tx\(.*?s_notify_tx_wait_epoch",
        "notify_tx epoch guard",
        STREAM,
    )
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
]


def main() -> int:
    static_source_checks()
    for case in CASES:
        case()
    print(
        "PASS: BLE audio transport model covers connection epoch, notify readiness, "
        "session ownership, replay, backpressure, stale GATT events, and retry timeout "
        f"across {len(CASES)} extreme cases."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
