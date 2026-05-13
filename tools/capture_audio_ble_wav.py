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
DEFAULT_OUTPUT_DIR = pathlib.Path("tests/artifacts/audio")
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
    parser.add_argument("--timeout-seconds", type=int, default=60)
    parser.add_argument("--boot-timeout-seconds", type=int, default=15)
    parser.add_argument("--notify-ready-timeout-seconds", type=int, default=20)
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


async def run_ble_capture(args, ser: Serial, serial_monitor: SerialLogMonitor):
    address = get_paired_device_address(args.device_name)
    address_hex = get_paired_device_address_hex(args.device_name)
    if address is None or address_hex is None:
        raise RuntimeError(f"capture_audio_ble_wav: unable to resolve paired BLE device '{args.device_name}'")

    recover_host_ble(args.device_name, address_hex)

    requester = await BluetoothLEDevice.from_bluetooth_address_async(int(address_hex, 16))
    if requester is None:
        raise RuntimeError(f"capture_audio_ble_wav: unable to open BluetoothLEDevice for '{args.device_name}'")

    session_id = None
    stop_received = False
    chunk_fragments = {}
    chunk_sizes = {}
    complete_chunks = {}

    def handle_notification(_sender, data: bytearray):
        nonlocal session_id, stop_received
        header = parse_header(bytes(data))
        packet_type = header["packet_type"]
        if packet_type == PACKET_TYPE_SESSION_START:
            session_id = header["session_id"]
            return
        if session_id is None or header["session_id"] != session_id:
            return
        if packet_type == PACKET_TYPE_AUDIO_CHUNK:
            chunk_index = header["chunk_index"]
            if chunk_index not in chunk_fragments:
                chunk_fragments[chunk_index] = [None] * header["fragment_count"]
                chunk_sizes[chunk_index] = header["chunk_pcm_bytes"]
            chunk_fragments[chunk_index][header["fragment_index"]] = header["payload"]
            if all(part is not None for part in chunk_fragments[chunk_index]):
                joined = b"".join(chunk_fragments[chunk_index])
                complete_chunks[chunk_index] = joined[:chunk_sizes[chunk_index]]
        elif packet_type == PACKET_TYPE_SESSION_STOP:
            stop_received = True

    service_uuid = uuid.UUID(SERVICE_UUID)
    notify_uuid = uuid.UUID(NOTIFY_UUID)

    services_result = await requester.get_gatt_services_for_uuid_async(service_uuid)
    if services_result.status != 0 or len(services_result.services) == 0:
        requester.close()
        raise RuntimeError(
            f"capture_audio_ble_wav: target service unavailable status={services_result.status} count={len(services_result.services)}"
        )

    service = services_result.services[0]
    chars_result = await service.get_characteristics_for_uuid_async(notify_uuid)
    if chars_result.status != 0 or len(chars_result.characteristics) == 0:
        service.close()
        requester.close()
        raise RuntimeError(
            f"capture_audio_ble_wav: target characteristic unavailable status={chars_result.status} count={len(chars_result.characteristics)}"
        )

    characteristic = chars_result.characteristics[0]

    def on_value_changed(sender, args):
        reader = DataReader.from_buffer(args.characteristic_value)
        payload = bytearray(reader.unconsumed_buffer_length)
        reader.read_bytes(payload)
        handle_notification(sender, payload)
        reader.close()

    token = characteristic.add_value_changed(on_value_changed)
    cccd_status = await characteristic.write_client_characteristic_configuration_descriptor_async(GattCccdValue.NOTIFY)
    if cccd_status != 0:
        characteristic.remove_value_changed(token)
        service.close()
        requester.close()
        raise RuntimeError(f"capture_audio_ble_wav: notify enable failed status={cccd_status}")

    try:
        await serial_monitor.wait_for_predicate(
            lambda line: AUDIO_NOTIFY_READY_MARKER in line and AUDIO_NOTIFY_ENABLED_MARKER in line,
            timeout_seconds=args.notify_ready_timeout_seconds,
            description="audio notify enabled marker",
        )

        send_toggle(ser)
        capture_deadline = time.time() + args.capture_seconds
        while time.time() < capture_deadline:
            serial_monitor.poll_lines()
            await asyncio.sleep(0.05)
        send_toggle(ser)

        deadline = time.time() + args.timeout_seconds
        while time.time() < deadline:
            serial_monitor.poll_lines()
            if stop_received and complete_chunks:
                break
            if AUDIO_UPLOAD_SKIPPED_MARKER in serial_monitor.recent_text():
                break
            await asyncio.sleep(0.1)
    finally:
        try:
            await characteristic.write_client_characteristic_configuration_descriptor_async(GattCccdValue.NONE)
        except Exception:
            pass
        characteristic.remove_value_changed(token)
        service.close()
        requester.close()

    if session_id is None:
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
    if not stop_received:
        raise RuntimeError(
            "capture_audio_ble_wav: did not receive session_stop; recent serial logs:\n"
            f"{serial_monitor.recent_text()}"
        )
    if not complete_chunks:
        raise RuntimeError(
            "capture_audio_ble_wav: did not receive any audio chunks; recent serial logs:\n"
            f"{serial_monitor.recent_text()}"
        )

    ordered_pcm = b"".join(complete_chunks[index] for index in sorted(complete_chunks))
    output_dir = pathlib.Path(args.output_dir)
    timestamp = time.strftime("%Y%m%d_%H%M%S")
    output_path = output_dir / f"capture_ble_{timestamp}_16k_mono.wav"
    write_wav(output_path, ordered_pcm)
    print(f"wav_path={output_path}")
    print(f"session_id={session_id}")
    print(f"chunk_count={len(complete_chunks)}")
    print(f"pcm_bytes={len(ordered_pcm)}")


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
