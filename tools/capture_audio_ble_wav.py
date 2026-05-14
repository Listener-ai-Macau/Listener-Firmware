import argparse
import asyncio
import pathlib
import subprocess
import struct
import time
import uuid
import wave
from collections import deque

import serial
from serial import Serial
from winrt.windows.devices.bluetooth import BluetoothLEDevice
from winrt.windows.devices.bluetooth.genericattributeprofile import (
    GattClientCharacteristicConfigurationDescriptorValue as GattCccdValue,
)
from winrt.windows.storage.streams import DataReader

SERVICE_UUID = "710af845-6d9f-6583-0c4d-9e5b3bc3091a"
NOTIFY_UUID = "710af845-6d9f-6583-0c4d-9e5b3bc3091b"
HEADER_LEN = 20
MAGIC = b"VKA1"
PACKET_TYPE_SESSION_START = 1
PACKET_TYPE_AUDIO_CHUNK = 2
PACKET_TYPE_SESSION_STOP = 3
PCM_SAMPLE_RATE = 16000
PCM_CHANNELS = 1
PCM_WIDTH_BYTES = 2
DEFAULT_OUTPUT_DIR = pathlib.Path("tests")
RECOVER_BLE_SCRIPT = pathlib.Path(__file__).with_name("recover_ble_hid_host.ps1")
READY_MARKERS = (
    "voice recording control ready: key1 toggle start/stop",
    "USB SERIAL INPUT READY",
)
AUDIO_NOTIFY_READY_MARKER = "audio notify subscription changed"
AUDIO_NOTIFY_ENABLED_MARKER = "notify=1"
AUDIO_UPLOAD_BEGIN_MARKER = "audio session upload begin"
AUDIO_UPLOAD_END_MARKER = "audio session upload end"
AUDIO_UPLOAD_SKIPPED_MARKER = "audio session upload skipped"
STREAM_SESSION_START_MARKER = "stream session start queued"
STREAM_SESSION_CHUNK_MARKER = "stream session chunk queued"
STREAM_SESSION_STOP_MARKER = "stream session stop queued"
BLE_NOTIFY_ENABLE_RETRY_COUNT = 3


class SerialLogMonitor:
    def __init__(self, ser: Serial) -> None:
        self._ser = ser
        self._buffer = bytearray()
        self._recent_lines = deque(maxlen=400)

    def poll_lines(self) -> list[str]:
        lines: list[str] = []
        bytes_waiting = self._ser.in_waiting
        if bytes_waiting <= 0:
            return lines

        data = self._ser.read(bytes_waiting)
        if not data:
            return lines

        self._buffer.extend(data)
        while b"\n" in self._buffer:
            raw_line, _, self._buffer = self._buffer.partition(b"\n")
            line = raw_line.decode("utf-8", errors="ignore").strip()
            if line:
                self._recent_lines.append(line)
                lines.append(line)
        return lines

    def recent_text(self) -> str:
        if not self._recent_lines:
            return "<no serial logs captured>"
        return "\n".join(self._recent_lines)

    def contains(self, marker: str) -> bool:
        return any(marker in line for line in self._recent_lines)

    def find_lines(self, marker: str) -> list[str]:
        return [line for line in self._recent_lines if marker in line]

    async def wait_for_markers(self, markers: tuple[str, ...], timeout_seconds: int) -> None:
        deadline = time.time() + timeout_seconds
        found = set()
        while time.time() < deadline:
            for line in self.poll_lines():
                for marker in markers:
                    if marker in line:
                        found.add(marker)
            if len(found) == len(markers):
                return
            await asyncio.sleep(0.05)
        raise RuntimeError(
            "capture_audio_ble_wav: timed out waiting for serial markers "
            f"{markers}; recent logs:\n{self.recent_text()}"
        )

    async def wait_for_predicate(self, predicate, timeout_seconds: int, description: str) -> None:
        deadline = time.time() + timeout_seconds
        while time.time() < deadline:
            for line in self.poll_lines():
                if predicate(line):
                    return
            await asyncio.sleep(0.05)
        raise RuntimeError(
            f"capture_audio_ble_wav: timed out waiting for {description}; recent logs:\n{self.recent_text()}"
        )


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--device-name", default="Listener Keyboard")
    parser.add_argument("--capture-seconds", type=int, default=4)
    parser.add_argument("--output-dir", default=str(DEFAULT_OUTPUT_DIR))
    parser.add_argument("--serial-log-path", default=None)
    parser.add_argument("--timeout-seconds", type=int, default=60)
    parser.add_argument("--boot-timeout-seconds", type=int, default=15)
    parser.add_argument("--notify-ready-timeout-seconds", type=int, default=20)
    parser.add_argument("--trigger-mode", choices=["serial-toggle", "physical-key"], default="serial-toggle")
    parser.add_argument("--max-sessions", type=int, default=1)
    return parser.parse_args()


def get_paired_device_address(device_name: str) -> str | None:
    ps = (
        "Get-PnpDevice -Class Bluetooth | "
        f"Where-Object {{ $_.FriendlyName -eq '{device_name}' }} | "
        "Select-Object -First 1 -ExpandProperty InstanceId"
    )
    try:
        result = subprocess.run(
            ["powershell", "-NoProfile", "-Command", ps],
            capture_output=True,
            text=True,
            check=True,
        )
    except subprocess.CalledProcessError:
        return None

    instance_id = result.stdout.strip()
    marker = "DEV_"
    if marker not in instance_id:
        return None
    suffix = instance_id.split(marker, 1)[1]
    hex12 = "".join(ch for ch in suffix if ch in "0123456789ABCDEFabcdef")[:12]
    if len(hex12) != 12:
        return None
    return ":".join(hex12[i:i + 2] for i in range(0, 12, 2)).upper()


def get_paired_device_address_hex(device_name: str) -> str | None:
    address = get_paired_device_address(device_name)
    if address is None:
        return None
    return address.replace(":", "").upper()


def recover_host_ble(device_name: str, address_hex: str) -> None:
    if not RECOVER_BLE_SCRIPT.exists():
        return
    subprocess.run(
        [
            "powershell",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(RECOVER_BLE_SCRIPT),
            "-DeviceName",
            device_name,
            "-BluetoothAddress",
            address_hex,
        ],
        check=True,
        capture_output=True,
        text=True,
    )


def send_toggle(ser: Serial) -> None:
    ser.write(b"~VREC:TOGGLE\n")
    ser.flush()


def parse_header(packet: bytes):
    if len(packet) < HEADER_LEN:
        raise RuntimeError("capture_audio_ble_wav: packet too short")
    magic = packet[0:4]
    if magic != MAGIC:
        raise RuntimeError(f"capture_audio_ble_wav: invalid magic {magic!r}")
    packet_type = packet[4]
    header_len = struct.unpack_from("<H", packet, 6)[0]
    session_id = struct.unpack_from("<I", packet, 8)[0]
    chunk_index = struct.unpack_from("<H", packet, 12)[0]
    fragment_index = packet[14]
    fragment_count = packet[15]
    payload_len = struct.unpack_from("<H", packet, 16)[0]
    chunk_pcm_bytes = struct.unpack_from("<H", packet, 18)[0]
    payload = packet[header_len:header_len + payload_len]
    return {
        "packet_type": packet_type,
        "session_id": session_id,
        "chunk_index": chunk_index,
        "fragment_index": fragment_index,
        "fragment_count": fragment_count,
        "payload_len": payload_len,
        "chunk_pcm_bytes": chunk_pcm_bytes,
        "payload": payload,
    }


def write_wav(output_path: pathlib.Path, pcm_bytes: bytes) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(output_path), "wb") as wav_file:
        wav_file.setnchannels(PCM_CHANNELS)
        wav_file.setsampwidth(PCM_WIDTH_BYTES)
        wav_file.setframerate(PCM_SAMPLE_RATE)
        wav_file.writeframes(pcm_bytes)


def pcm_duration_seconds(pcm_bytes_len: int) -> float:
    bytes_per_second = PCM_SAMPLE_RATE * PCM_CHANNELS * PCM_WIDTH_BYTES
    if bytes_per_second <= 0:
        return 0.0
    return float(pcm_bytes_len) / float(bytes_per_second)


class SessionCollector:
    def __init__(self) -> None:
        self.session_id = None
        self.stop_received = False
        self.expected_chunk_count = None
        self.chunk_fragments = {}
        self.chunk_sizes = {}
        self.complete_chunks = {}
        self.last_packet_time = 0.0

    def reset(self) -> None:
        self.session_id = None
        self.stop_received = False
        self.expected_chunk_count = None
        self.chunk_fragments = {}
        self.chunk_sizes = {}
        self.complete_chunks = {}
        self.last_packet_time = 0.0

    def handle_notification(self, packet: bytes) -> None:
        header = parse_header(packet)
        packet_type = header["packet_type"]
        self.last_packet_time = time.time()
        if packet_type == PACKET_TYPE_SESSION_START:
            if self.session_id != header["session_id"]:
                self.reset()
                self.session_id = header["session_id"]
            return
        if self.session_id is None or header["session_id"] != self.session_id:
            return
        if packet_type == PACKET_TYPE_AUDIO_CHUNK:
            chunk_index = header["chunk_index"]
            if chunk_index not in self.chunk_fragments:
                self.chunk_fragments[chunk_index] = [None] * header["fragment_count"]
                self.chunk_sizes[chunk_index] = header["chunk_pcm_bytes"]
            self.chunk_fragments[chunk_index][header["fragment_index"]] = header["payload"]
            if all(part is not None for part in self.chunk_fragments[chunk_index]):
                joined = b"".join(self.chunk_fragments[chunk_index])
                self.complete_chunks[chunk_index] = joined[:self.chunk_sizes[chunk_index]]
        elif packet_type == PACKET_TYPE_SESSION_STOP:
            self.stop_received = True
            self.expected_chunk_count = header["chunk_index"]

    def has_completed_session(self) -> bool:
        if (
            self.session_id is None
            or not self.stop_received
            or self.expected_chunk_count is None
            or self.expected_chunk_count == 0
        ):
            return False
        return (time.time() - self.last_packet_time) >= 0.2

    def missing_chunk_indices(self) -> list[int]:
        if self.expected_chunk_count is None:
            return []
        return [
            index
            for index in range(self.expected_chunk_count)
            if index not in self.complete_chunks
        ]

    def is_chunk_complete(self) -> bool:
        if self.expected_chunk_count is None:
            return False
        return len(self.missing_chunk_indices()) == 0

    def chunk_integrity_summary(self) -> str:
        expected = (
            str(self.expected_chunk_count)
            if self.expected_chunk_count is not None
            else "unknown"
        )
        missing = self.missing_chunk_indices()
        missing_preview = ",".join(str(index) for index in missing[:16])
        if len(missing) > 16:
            missing_preview += ",..."
        if not missing_preview:
            missing_preview = "<none>"
        return (
            f"expected_chunk_count={expected} "
            f"received_chunk_count={len(self.complete_chunks)} "
            f"missing_chunk_count={len(missing)} "
            f"missing_chunk_indices={missing_preview}"
        )

    def ordered_pcm(self) -> bytes:
        return b"".join(self.complete_chunks[index] for index in sorted(self.complete_chunks))


async def run_ble_capture(args, ser: Serial, serial_monitor: SerialLogMonitor):
    address = get_paired_device_address(args.device_name)
    address_hex = get_paired_device_address_hex(args.device_name)
    if address is None or address_hex is None:
        raise RuntimeError(f"capture_audio_ble_wav: unable to resolve paired BLE device '{args.device_name}'")

    recover_host_ble(args.device_name, address_hex)

    collector = SessionCollector()
    completed_sessions = 0
    session_summaries = []

    def handle_notification(_sender, data: bytearray):
        collector.handle_notification(bytes(data))

    service_uuid = uuid.UUID(SERVICE_UUID)
    notify_uuid = uuid.UUID(NOTIFY_UUID)

    requester = None
    service = None
    characteristic = None
    token = None
    last_notify_error = None

    def on_value_changed(sender, args):
        reader = DataReader.from_buffer(args.characteristic_value)
        payload = bytearray(reader.unconsumed_buffer_length)
        reader.read_bytes(payload)
        handle_notification(sender, payload)
        reader.close()

    for attempt in range(1, BLE_NOTIFY_ENABLE_RETRY_COUNT + 1):
        if requester is not None:
            try:
                if characteristic is not None and token is not None:
                    characteristic.remove_value_changed(token)
            except Exception:
                pass
            if service is not None:
                service.close()
            requester.close()
            requester = None
            service = None
            characteristic = None
            token = None

        requester = await BluetoothLEDevice.from_bluetooth_address_async(int(address_hex, 16))
        if requester is None:
            last_notify_error = RuntimeError(
                f"capture_audio_ble_wav: unable to open BluetoothLEDevice for '{args.device_name}'")
            await asyncio.sleep(1.0)
            continue

        services_result = await requester.get_gatt_services_for_uuid_async(service_uuid)
        if services_result.status != 0 or len(services_result.services) == 0:
            last_notify_error = RuntimeError(
                f"capture_audio_ble_wav: target service unavailable status={services_result.status} count={len(services_result.services)}"
            )
            recover_host_ble(args.device_name, address_hex)
            await asyncio.sleep(1.0)
            continue

        service = services_result.services[0]
        chars_result = await service.get_characteristics_for_uuid_async(notify_uuid)
        if chars_result.status != 0 or len(chars_result.characteristics) == 0:
            last_notify_error = RuntimeError(
                f"capture_audio_ble_wav: target characteristic unavailable status={chars_result.status} count={len(chars_result.characteristics)}"
            )
            recover_host_ble(args.device_name, address_hex)
            await asyncio.sleep(1.0)
            continue

        characteristic = chars_result.characteristics[0]
        token = characteristic.add_value_changed(on_value_changed)
        try:
            cccd_status = await characteristic.write_client_characteristic_configuration_descriptor_async(
                GattCccdValue.NOTIFY)
            if cccd_status == 0:
                break
            last_notify_error = RuntimeError(
                f"capture_audio_ble_wav: notify enable failed status={cccd_status} attempt={attempt}"
            )
        except OSError as exc:
            last_notify_error = RuntimeError(
                f"capture_audio_ble_wav: notify enable raised {exc!r} attempt={attempt}"
            )

        recover_host_ble(args.device_name, address_hex)
        await asyncio.sleep(1.0)
    else:
        if requester is not None:
            try:
                if characteristic is not None and token is not None:
                    characteristic.remove_value_changed(token)
            except Exception:
                pass
            if service is not None:
                service.close()
            requester.close()
        raise last_notify_error if last_notify_error is not None else RuntimeError(
            "capture_audio_ble_wav: notify enable failed without detailed error")

    try:
        await serial_monitor.wait_for_predicate(
            lambda line: AUDIO_NOTIFY_READY_MARKER in line and AUDIO_NOTIFY_ENABLED_MARKER in line,
            timeout_seconds=args.notify_ready_timeout_seconds,
            description="audio notify enabled marker",
        )

        if args.trigger_mode == "physical-key":
            print("ready_for_key=1", flush=True)
            print("trigger_mode=physical-key", flush=True)
            print("instruction=press KEY1 once to start recording, then press KEY1 again to stop", flush=True)

        target_sessions = args.max_sessions if args.max_sessions > 0 else None
        while target_sessions is None or completed_sessions < target_sessions:
            collector.reset()

            if args.trigger_mode == "serial-toggle":
                send_toggle(ser)
                capture_deadline = time.time() + args.capture_seconds
                while time.time() < capture_deadline:
                    serial_monitor.poll_lines()
                    await asyncio.sleep(0.05)
                send_toggle(ser)

            deadline = time.time() + args.timeout_seconds
            while time.time() < deadline:
                serial_monitor.poll_lines()
                if collector.has_completed_session():
                    break
                if AUDIO_UPLOAD_SKIPPED_MARKER in serial_monitor.recent_text():
                    break
                await asyncio.sleep(0.1)

            if collector.session_id is None:
                if target_sessions is None and args.trigger_mode == "physical-key":
                    continue
                device_state = (
                    "record_start_lines=\n"
                    + "\n".join(serial_monitor.find_lines("recording start"))
                    + "\nrecord_stop_lines=\n"
                    + "\n".join(serial_monitor.find_lines("recording stop"))
                    + "\nqueue_lines=\n"
                    + "\n".join(serial_monitor.find_lines("ble audio upload queued"))
                    + "\nupload_begin_lines=\n"
                    + "\n".join(serial_monitor.find_lines(AUDIO_UPLOAD_BEGIN_MARKER))
                    + "\nupload_end_lines=\n"
                    + "\n".join(serial_monitor.find_lines(AUDIO_UPLOAD_END_MARKER))
                )
                raise RuntimeError(
                    "capture_audio_ble_wav: did not receive session_start; recent serial logs:\n"
                    f"{device_state}\n\nrecent serial logs:\n{serial_monitor.recent_text()}"
                )
            if not collector.stop_received:
                raise RuntimeError(
                    "capture_audio_ble_wav: did not receive session_stop; recent serial logs:\n"
                    f"{serial_monitor.recent_text()}"
                )
            if collector.expected_chunk_count is None:
                raise RuntimeError(
                    "capture_audio_ble_wav: session_stop did not provide expected chunk count; recent serial logs:\n"
                    f"{serial_monitor.recent_text()}"
                )
            if not collector.complete_chunks:
                raise RuntimeError(
                    "capture_audio_ble_wav: did not receive any audio chunks; recent serial logs:\n"
                    f"{serial_monitor.recent_text()}"
                )
            if not collector.is_chunk_complete():
                raise RuntimeError(
                    "capture_audio_ble_wav: session chunk integrity check failed; "
                    f"{collector.chunk_integrity_summary()}\nrecent serial logs:\n{serial_monitor.recent_text()}"
                )

            ordered_pcm = collector.ordered_pcm()
            output_dir = pathlib.Path(args.output_dir)
            output_path = output_dir / "capture_ble_latest_16k_mono.wav"
            write_wav(output_path, ordered_pcm)
            completed_sessions += 1
            duration_seconds = pcm_duration_seconds(len(ordered_pcm))

            session_summary = {
                "wav_path": str(output_path),
                "serial_log_path": str(args.serial_log_path) if args.serial_log_path else "",
                "session_id": collector.session_id,
                "chunk_count": len(collector.complete_chunks),
                "expected_chunk_count": collector.expected_chunk_count,
                "missing_chunk_count": len(collector.missing_chunk_indices()),
                "pcm_bytes": len(ordered_pcm),
                "duration_seconds": duration_seconds,
                "serial_stream_start_count": len(serial_monitor.find_lines(STREAM_SESSION_START_MARKER)),
                "serial_stream_chunk_count": len(serial_monitor.find_lines(STREAM_SESSION_CHUNK_MARKER)),
                "serial_stream_stop_count": len(serial_monitor.find_lines(STREAM_SESSION_STOP_MARKER)),
                "completed_session_count": completed_sessions,
            }
            session_summaries.append(session_summary)

            if args.serial_log_path:
                serial_log_path = pathlib.Path(args.serial_log_path)
                serial_log_path.parent.mkdir(parents=True, exist_ok=True)
                serial_log_path.write_text(serial_monitor.recent_text(), encoding="utf-8")

            print(f"wav_path={output_path}", flush=True)
            print(f"session_id={collector.session_id}", flush=True)
            print(f"chunk_count={len(collector.complete_chunks)}", flush=True)
            print(f"expected_chunk_count={collector.expected_chunk_count}", flush=True)
            print(f"missing_chunk_count={len(collector.missing_chunk_indices())}", flush=True)
            print(f"pcm_bytes={len(ordered_pcm)}", flush=True)
            print(f"duration_seconds={duration_seconds:.3f}", flush=True)
            print(
                f"serial_stream_start_count={len(serial_monitor.find_lines(STREAM_SESSION_START_MARKER))}",
                flush=True,
            )
            print(
                f"serial_stream_chunk_count={len(serial_monitor.find_lines(STREAM_SESSION_CHUNK_MARKER))}",
                flush=True,
            )
            print(
                f"serial_stream_stop_count={len(serial_monitor.find_lines(STREAM_SESSION_STOP_MARKER))}",
                flush=True,
            )
            print(f"completed_session_count={completed_sessions}", flush=True)

            if target_sessions is None and args.trigger_mode == "physical-key":
                print("ready_for_key=1", flush=True)
                continue
    finally:
        if args.serial_log_path:
            serial_log_path = pathlib.Path(args.serial_log_path)
            serial_log_path.parent.mkdir(parents=True, exist_ok=True)
            serial_log_path.write_text(serial_monitor.recent_text(), encoding="utf-8")
        try:
            await characteristic.write_client_characteristic_configuration_descriptor_async(GattCccdValue.NONE)
        except Exception:
            pass
        if characteristic is not None and token is not None:
            try:
                characteristic.remove_value_changed(token)
            except Exception:
                pass
        if service is not None:
            service.close()
        if requester is not None:
            requester.close()

    if completed_sessions == 0:
        raise RuntimeError(
            "capture_audio_ble_wav: no completed session captured; recent serial logs:\n"
            f"{serial_monitor.recent_text()}"
        )
    return session_summaries


def main():
    args = parse_args()
    with serial.Serial(args.port, 115200, timeout=0.05) as ser:
        ser.setDTR(False)
        ser.setRTS(False)
        ser.reset_input_buffer()
        serial_monitor = SerialLogMonitor(ser)
        asyncio.run(serial_monitor.wait_for_markers(READY_MARKERS, timeout_seconds=args.boot_timeout_seconds))
        ser.reset_input_buffer()
        asyncio.run(run_ble_capture(args, ser, serial_monitor))


if __name__ == "__main__":
    main()
