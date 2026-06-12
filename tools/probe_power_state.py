import argparse
import asyncio
import pathlib
import sys
import time
import uuid

TOOLS_DIR = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

import capture_audio_ble_wav as cap


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Capture same-session board/power telemetry for active or recording states."
    )
    parser.add_argument("--port", required=True)
    parser.add_argument("--mode", choices=["active", "recording"], required=True)
    parser.add_argument("--device-name", default="listener")
    parser.add_argument("--output-dir", default="tests/artifacts/power_states")
    parser.add_argument("--bluetooth-address", default=None)
    parser.add_argument("--stream-ready-timeout-seconds", type=float, default=12.0)
    parser.add_argument("--recording-start-timeout-seconds", type=float, default=25.0)
    parser.add_argument("--recording-stop-timeout-seconds", type=float, default=12.0)
    return parser.parse_args()


def resolve_address_hex(args: argparse.Namespace) -> str:
    explicit = "".join(
        ch for ch in str(args.bluetooth_address or "") if ch in "0123456789ABCDEFabcdef"
    ).upper()
    if explicit:
        return explicit
    detected = cap.get_paired_device_address_hex(args.device_name)
    if detected:
        return detected
    raise RuntimeError(
        f"probe_power_state: unable to resolve paired BLE device '{args.device_name}'"
    )


async def collect_for(
    serial_monitor: cap.SerialLogMonitor,
    duration_seconds: float,
    transcript_lines: list[str] | None = None,
) -> None:
    deadline = time.time() + duration_seconds
    while time.time() < deadline:
        for line in serial_monitor.poll_lines():
            if transcript_lines is not None:
                transcript_lines.append(line)
        await asyncio.sleep(0.02)


async def send_command_and_collect(
    ser,
    serial_monitor: cap.SerialLogMonitor,
    command: str,
    duration_seconds: float,
    transcript_lines: list[str],
) -> None:
    transcript_lines.append(f"> {command}")
    ser.write((command + "\n").encode("utf-8"))
    ser.flush()
    await collect_for(serial_monitor, duration_seconds, transcript_lines)


async def wait_for_stream_ready(
    serial_monitor: cap.SerialLogMonitor,
    timeout_seconds: float,
) -> None:
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        serial_monitor.poll_lines()
        latest = cap.latest_audio_transport_state_line(serial_monitor)
        if cap.line_indicates_stream_ready(latest):
            return
        await asyncio.sleep(0.05)
    raise RuntimeError(
        "probe_power_state: stream_ready not observed; recent logs:\n"
        + serial_monitor.recent_text()
    )


async def wait_for_recording_start(
    serial_monitor: cap.SerialLogMonitor,
    collector: cap.SessionCollector,
    timeout_seconds: float,
) -> None:
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        serial_monitor.poll_lines()
        if collector.has_started_session() or serial_monitor.find_lines("device_status state=recording"):
            return
        await asyncio.sleep(0.05)
    raise RuntimeError(
        "probe_power_state: recording state not observed; recent logs:\n"
        + serial_monitor.recent_text()
    )


async def wait_for_recording_stop(
    serial_monitor: cap.SerialLogMonitor,
    collector: cap.SessionCollector,
    timeout_seconds: float,
) -> None:
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        serial_monitor.poll_lines()
        if (
            collector.stop_received
            or collector.cancel_received
            or serial_monitor.find_lines("recording cancel source=")
            or serial_monitor.find_lines("record session canceled")
            or serial_monitor.find_lines(
                "device_status state=ready detail=recording_session_finished"
            )
        ):
            return
        await asyncio.sleep(0.05)


async def run_active(args: argparse.Namespace, output_dir: pathlib.Path) -> tuple[pathlib.Path, pathlib.Path]:
    transcript_path = output_dir / "active_transcript.txt"
    serial_log_path = output_dir / "active_serial.log"
    transcript_lines: list[str] = []
    with cap.open_serial_with_retry(args.port, 115200, timeout=0.05) as ser:
        ser.setDTR(False)
        ser.setRTS(False)
        ser.reset_input_buffer()
        serial_monitor = cap.SerialLogMonitor(ser)
        await send_command_and_collect(ser, serial_monitor, "~KEY:EC11:SINGLE", 1.5, transcript_lines)
        await send_command_and_collect(ser, serial_monitor, "~BOARD:STATUS", 1.8, transcript_lines)
        await send_command_and_collect(ser, serial_monitor, "~POWER:STATUS", 1.8, transcript_lines)
        serial_log_path.write_text(serial_monitor.full_text(), encoding="utf-8")
    transcript_path.write_text("\n".join(transcript_lines), encoding="utf-8")
    return transcript_path, serial_log_path


async def run_recording(
    args: argparse.Namespace,
    output_dir: pathlib.Path,
) -> tuple[pathlib.Path, pathlib.Path]:
    transcript_path = output_dir / "recording_transcript.txt"
    serial_log_path = output_dir / "recording_serial.log"
    stage_path = output_dir / "recording_stage.txt"
    transcript_lines: list[str] = []
    address_hex = resolve_address_hex(args)
    collector = cap.SessionCollector()
    serial_monitor: cap.SerialLogMonitor | None = None

    def set_stage(value: str) -> None:
        stage_path.write_text(value + "\n", encoding="utf-8")
        print(f"recording_probe_stage={value}", flush=True)

    def on_value_changed(_sender, event_args) -> None:
        reader = cap.DataReader.from_buffer(event_args.characteristic_value)
        payload = bytearray(reader.unconsumed_buffer_length)
        reader.read_bytes(payload)
        reader.close()
        collector.handle_notification(bytes(payload))

    requester = service = characteristic = token = gatt_session = None
    with cap.open_serial_with_retry(args.port, 115200, timeout=0.05) as ser:
        ser.setDTR(False)
        ser.setRTS(False)
        ser.reset_input_buffer()
        serial_monitor = cap.SerialLogMonitor(ser)
        try:
            set_stage("enable_notify")
            requester, service, characteristic, token, gatt_session = await cap.enable_notify_with_rebuild(
                address_hex=address_hex,
                device_name=args.device_name,
                service_uuid=uuid.UUID(cap.SERVICE_UUID),
                notify_uuid=uuid.UUID(cap.NOTIFY_UUID),
                on_value_changed=on_value_changed,
            )
            set_stage("wait_stream_ready")
            await wait_for_stream_ready(serial_monitor, args.stream_ready_timeout_seconds)
            set_stage("start_recording")
            cap.send_toggle(ser)
            await wait_for_recording_start(
                serial_monitor, collector, args.recording_start_timeout_seconds
            )
            await asyncio.sleep(0.7)
            set_stage("capture_status")
            await send_command_and_collect(
                ser, serial_monitor, "~BOARD:STATUS", 1.8, transcript_lines
            )
            await send_command_and_collect(
                ser, serial_monitor, "~POWER:STATUS", 1.8, transcript_lines
            )
            set_stage("cancel_recording")
            cap.send_cancel(ser)
            await wait_for_recording_stop(
                serial_monitor, collector, args.recording_stop_timeout_seconds
            )
            set_stage("write_logs")
            serial_log_path.write_text(serial_monitor.full_text(), encoding="utf-8")
        finally:
            if serial_monitor is not None:
                serial_log_path.write_text(serial_monitor.full_text(), encoding="utf-8")
            if characteristic is not None:
                await cap.disable_notify_best_effort(characteristic)
            cap.close_ble_objects(
                characteristic=characteristic,
                token=token,
                service=service,
                gatt_session=gatt_session,
                requester=requester,
            )
    transcript_path.write_text("\n".join(transcript_lines), encoding="utf-8")
    return transcript_path, serial_log_path


async def main_async() -> None:
    args = parse_args()
    cap.configure_utf8_stdio()
    output_dir = pathlib.Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    if args.mode == "active":
        transcript_path, serial_log_path = await run_active(args, output_dir)
    else:
        transcript_path, serial_log_path = await run_recording(args, output_dir)
    print(f"mode={args.mode}")
    print(f"transcript_path={transcript_path}")
    print(f"serial_log_path={serial_log_path}")


def main() -> None:
    asyncio.run(main_async())


if __name__ == "__main__":
    main()
