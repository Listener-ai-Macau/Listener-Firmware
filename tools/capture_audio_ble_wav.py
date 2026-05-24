import argparse
import asyncio
import pathlib
import subprocess
import struct
import sys
import threading
import time
import uuid
import wave
from collections import Counter, deque

import serial
from serial import Serial
from winrt.windows.devices.bluetooth import BluetoothCacheMode, BluetoothLEDevice
from winrt.windows.devices.bluetooth.genericattributeprofile import (
    GattClientCharacteristicConfigurationDescriptorValue as GattCccdValue,
    GattCharacteristicProperties,
    GattCommunicationStatus,
)
from winrt.windows.devices.enumeration import DeviceAccessStatus
from winrt.windows.storage.streams import DataReader

SERVICE_UUID = "710af845-6d9f-6583-0c4d-9e5b3bc3091a"
NOTIFY_UUID = "710af845-6d9f-6583-0c4d-9e5b3bc3091b"
HEADER_LEN = 20
MAGIC = b"VKA1"
PACKET_TYPE_SESSION_START = 1
PACKET_TYPE_AUDIO_DATA = 2
PACKET_TYPE_AUDIO_CHUNK = PACKET_TYPE_AUDIO_DATA
PACKET_TYPE_SESSION_STOP = 3
PACKET_TYPE_SESSION_CANCEL = 4
PACKET_TYPE_SESSION_ERROR = 5
SESSION_ERROR_NAMES = {
    1: "queue_full",
    2: "notify_timeout",
    3: "link_lost",
    4: "sequence_overflow",
    5: "invalid_state",
    6: "no_memory",
    7: "packet_too_large",
    8: "transport",
}
PCM_SAMPLE_RATE = 16000
PCM_CHANNELS = 1
PCM_WIDTH_BYTES = 2
DEFAULT_OUTPUT_DIR = pathlib.Path("tests")
RECOVER_BLE_SCRIPT = pathlib.Path(__file__).with_name("recover_ble_hid_host.ps1")
ENSURE_BLE_SCRIPT = pathlib.Path(__file__).with_name("ensure_ble_hid_connection.ps1")
SERIAL_RESET_PULSE_SECONDS = 0.1
SERIAL_RESET_SETTLE_SECONDS = 0.2
SERIAL_OPEN_RETRY_COUNT = 12
SERIAL_OPEN_RETRY_DELAY_SECONDS = 1.0
READY_MARKERS = (
    "voice recording control ready: ec11_key toggle start/stop",
    "USB SERIAL INPUT READY",
)
AUDIO_NOTIFY_PACKET_SIZE_MARKER = "audio notify packet size updated"
AUDIO_NOTIFY_READY_MARKER = "audio notify subscription changed"
AUDIO_NOTIFY_RESTORED_MARKER = "audio notify subscription restored before connect"
AUDIO_NOTIFY_SUBSCRIBED_MARKER = "audio notify subscribed:"
AUDIO_NOTIFY_ENABLED_MARKER = "notify=1"
AUDIO_TRANSPORT_STATE_MARKER = "audio transport state:"
BLE_CONNECTION_ESTABLISHED_MARKER = "connection established; status=0"
AUDIO_UPLOAD_BEGIN_MARKER = "audio session upload begin"
AUDIO_UPLOAD_END_MARKER = "audio session upload end"
AUDIO_UPLOAD_SKIPPED_MARKER = "audio session upload skipped"
UNEXPECTED_RESET_MARKERS = (
    "rst:0x",
    "ESP-ROM:esp32",
    "boot: ESP-IDF",
)
STREAM_SESSION_START_MARKER = "stream session start queued"
STREAM_SESSION_AUDIO_MARKER = "stream audio packet batch queued"
STREAM_SESSION_CHUNK_MARKER = "stream session chunk queued"
STREAM_SESSION_STOP_MARKER = "stream session stop queued"
STREAM_TRANSPORT_SUMMARY_MARKER = "audio session transport summary"
BLE_NOTIFY_ENABLE_RETRY_COUNT = 3
BLE_NOTIFY_RECOVERY_DELAY_SECONDS = 0.5
BLE_NOTIFY_CCCD_TIMEOUT_SECONDS = 5
BLE_NOTIFY_PRE_CCCD_SETTLE_SECONDS = 0.35
NOTIFY_READY_SETTLE_SECONDS = 1.2
SESSION_COMPLETE_IDLE_SECONDS = 0.2
PHYSICAL_KEY_START_TIMEOUT_SECONDS = 300


class SerialLogMonitor:
    def __init__(self, ser: Serial) -> None:
        self._ser = ser
        self._buffer = bytearray()
        self._recent_lines = deque(maxlen=400)
        self._all_lines: list[str] = []

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
                self._all_lines.append(line)
                self._recent_lines.append(line)
                lines.append(line)
        return lines

    def recent_text(self) -> str:
        if not self._recent_lines:
            return "<no serial logs captured>"
        return "\n".join(self._recent_lines)

    def full_text(self) -> str:
        if not self._all_lines:
            return "<no serial logs captured>"
        return "\n".join(self._all_lines)

    def contains(self, marker: str) -> bool:
        return any(marker in line for line in self._all_lines)

    def find_lines(self, marker: str) -> list[str]:
        return [line for line in self._all_lines if marker in line]

    def find_lines_any(self, markers: tuple[str, ...]) -> list[str]:
        return [
            line
            for line in self._all_lines
            if any(marker in line for marker in markers)
        ]

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
        if any(predicate(line) for line in self._recent_lines):
            return
        deadline = time.time() + timeout_seconds
        while time.time() < deadline:
            for line in self.poll_lines():
                if predicate(line):
                    return
            await asyncio.sleep(0.05)
        raise RuntimeError(
            f"capture_audio_ble_wav: timed out waiting for {description}; recent logs:\n{self.recent_text()}"
        )


async def wait_for_ready_markers_or_running(
    serial_monitor: SerialLogMonitor,
    timeout_seconds: int,
) -> None:
    try:
        await serial_monitor.wait_for_markers(READY_MARKERS, timeout_seconds=timeout_seconds)
        return
    except RuntimeError:
        if "audio_capture: frame captured" not in serial_monitor.full_text():
            raise
        print(
            "serial_ready_fallback=audio_capture_running",
            flush=True,
        )


def line_indicates_notify_ready(line: str) -> bool:
    if AUDIO_NOTIFY_READY_MARKER in line and AUDIO_NOTIFY_ENABLED_MARKER in line:
        return True
    if AUDIO_NOTIFY_RESTORED_MARKER in line or AUDIO_NOTIFY_SUBSCRIBED_MARKER in line:
        return True
    return AUDIO_TRANSPORT_STATE_MARKER in line and AUDIO_NOTIFY_ENABLED_MARKER in line


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--device-name", default="listener")
    parser.add_argument("--capture-seconds", type=int, default=4)
    parser.add_argument("--output-dir", default=str(DEFAULT_OUTPUT_DIR))
    parser.add_argument("--serial-log-path", default=None)
    parser.add_argument("--timeout-seconds", type=int, default=None, help="Total session timeout. Defaults to capture-seconds + 45s for connection overhead.")
    parser.add_argument("--boot-timeout-seconds", type=int, default=15)
    parser.add_argument("--ble-connect-timeout-seconds", type=int, default=12)
    parser.add_argument("--notify-ready-timeout-seconds", type=int, default=45)
    parser.add_argument("--trigger-mode", choices=["serial-toggle", "physical-key"], default="serial-toggle")
    parser.add_argument("--max-sessions", type=int, default=1)
    parser.add_argument("--bluetooth-address", default=None)
    parser.add_argument("--no-reset-before-capture", action="store_false", dest="reset_before_capture")
    parser.set_defaults(reset_before_capture=True)
    return parser.parse_args()


def configure_utf8_stdio() -> None:
    for stream_name in ("stdout", "stderr"):
        stream = getattr(sys, stream_name, None)
        if stream is None or not hasattr(stream, "reconfigure"):
            continue
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass


def get_paired_device_address(device_name: str) -> str | None:
    escaped_name = device_name.replace("'", "''")
    ps = (
        "Get-PnpDevice -Class Bluetooth | "
        f"Where-Object {{ $_.FriendlyName -eq '{escaped_name}' }} | "
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


def ensure_host_ble_connection(
    device_name: str,
    address_hex: str,
    duration_seconds: int = 12,
    poll_interval_seconds: int = 2,
) -> None:
    if not ENSURE_BLE_SCRIPT.exists():
        return
    subprocess.run(
        [
            "powershell",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(ENSURE_BLE_SCRIPT),
            "-DeviceName",
            device_name,
            "-BluetoothAddress",
            address_hex,
            "-DurationSeconds",
            str(duration_seconds),
            "-PollIntervalSeconds",
            str(poll_interval_seconds),
            "-ExitOnReady",
        ],
        check=True,
        capture_output=True,
        text=True,
    )


def close_ble_objects(*, characteristic=None, token=None, service=None, requester=None) -> None:
    if characteristic is not None and token is not None:
        try:
            characteristic.remove_value_changed(token)
        except Exception:
            pass
    if service is not None:
        try:
            service.close()
        except Exception:
            pass
    if requester is not None:
        try:
            requester.close()
        except Exception:
            pass


async def disable_notify_best_effort(characteristic) -> None:
    if characteristic is None:
        return
    try:
        status = await characteristic.write_client_characteristic_configuration_descriptor_async(
            GattCccdValue.NONE
        )
        print(f"notify_disable_status={status}", flush=True)
    except OSError as exc:
        print(f"notify_disable_error={exc!r}", flush=True)
    except Exception as exc:
        print(f"notify_disable_error={type(exc).__name__}:{exc}", flush=True)


async def resolve_notify_characteristic(
    requester: BluetoothLEDevice,
    service_uuid: uuid.UUID,
    notify_uuid: uuid.UUID,
):
    services_result = await requester.get_gatt_services_with_cache_mode_async(BluetoothCacheMode.CACHED)
    if services_result.status != GattCommunicationStatus.SUCCESS:
        return None, None, RuntimeError(
            "capture_audio_ble_wav: target service unavailable "
            f"status={services_result.status} count={len(services_result.services)}"
        )

    service = None
    for candidate in services_result.services:
        if candidate.uuid == service_uuid:
            service = candidate
        else:
            try:
                candidate.close()
            except Exception:
                pass

    if service is None:
        return None, None, RuntimeError(
            "capture_audio_ble_wav: target service unavailable "
            f"status={services_result.status} count={len(services_result.services)}"
        )

    try:
        access_status = await service.request_access_async()
        if access_status not in (DeviceAccessStatus.ALLOWED, DeviceAccessStatus.UNSPECIFIED):
            service.close()
            return None, None, RuntimeError(
                f"capture_audio_ble_wav: target service access denied status={access_status}"
            )

        session = service.session
        if session is not None and session.can_maintain_connection:
            session.maintain_connection = True
    except Exception:
        pass

    chars_result = await service.get_characteristics_with_cache_mode_async(BluetoothCacheMode.CACHED)
    if chars_result.status != GattCommunicationStatus.SUCCESS:
        service.close()
        return None, None, RuntimeError(
            "capture_audio_ble_wav: target characteristic unavailable "
            f"status={chars_result.status} count={len(chars_result.characteristics)}"
        )

    characteristic = None
    for candidate in chars_result.characteristics:
        if candidate.uuid == notify_uuid:
            characteristic = candidate
            break

    if characteristic is None:
        service.close()
        return None, None, RuntimeError(
            "capture_audio_ble_wav: target characteristic unavailable "
            f"status={chars_result.status} count={len(chars_result.characteristics)}"
        )

    if not (
        int(characteristic.characteristic_properties)
        & int(GattCharacteristicProperties.NOTIFY)
    ):
        service.close()
        return None, None, RuntimeError(
            "capture_audio_ble_wav: target characteristic does not advertise NOTIFY property"
        )

    return service, characteristic, None


async def open_ble_device(address_hex: str):
    requester = await BluetoothLEDevice.from_bluetooth_address_async(int(address_hex, 16))
    if requester is None:
        return None

    device_id = ""
    try:
        device_id = requester.device_id or ""
    except Exception:
        device_id = ""

    if not device_id:
        return requester

    requester_by_id = await BluetoothLEDevice.from_id_async(device_id)
    if requester_by_id is None:
        return requester

    try:
        requester.close()
    except Exception:
        pass
    return requester_by_id


async def enable_notify_with_rebuild(
    *,
    address_hex: str,
    device_name: str,
    service_uuid: uuid.UUID,
    notify_uuid: uuid.UUID,
    on_value_changed,
):
    requester = None
    service = None
    characteristic = None
    token = None
    last_error = None

    for attempt in range(1, BLE_NOTIFY_ENABLE_RETRY_COUNT + 1):
        await disable_notify_best_effort(characteristic)
        close_ble_objects(
            characteristic=characteristic,
            token=token,
            service=service,
            requester=requester,
        )
        requester = None
        service = None
        characteristic = None
        token = None

        if attempt > 1:
            try:
                ensure_host_ble_connection(device_name, address_hex)
            except subprocess.CalledProcessError:
                pass
            await asyncio.sleep(BLE_NOTIFY_RECOVERY_DELAY_SECONDS)

        requester = await open_ble_device(address_hex)
        if requester is None:
            last_error = RuntimeError(
                f"capture_audio_ble_wav: unable to open BluetoothLEDevice for '{device_name}' attempt={attempt}"
            )
            await asyncio.sleep(BLE_NOTIFY_RECOVERY_DELAY_SECONDS)
            continue

        print(f"notify_enable_attempt={attempt}", flush=True)
        conn_status = 0
        try:
            conn_status = requester.connection_status
            print(f"ble_connection_status={conn_status}", flush=True)
        except Exception:
            pass
        if conn_status == 0:
            print("ble_device_disconnected=1; ensuring connection", flush=True)
            try:
                ensure_host_ble_connection(
                    device_name, address_hex,
                    duration_seconds=8, poll_interval_seconds=2,
                )
            except subprocess.CalledProcessError:
                pass
            await asyncio.sleep(0.3)

        try:
            cached_services_result = await requester.get_gatt_services_with_cache_mode_async(
                BluetoothCacheMode.CACHED
            )
            print(
                "cached_service_probe_status="
                f"{cached_services_result.status} count={len(cached_services_result.services)}",
                flush=True,
            )
            if cached_services_result is not None and cached_services_result.services is not None:
                for cached_service in cached_services_result.services:
                    try:
                        session = cached_service.session
                        if session is not None and session.can_maintain_connection:
                            session.maintain_connection = True
                    except Exception:
                        pass
                    try:
                        cached_service.close()
                    except Exception:
                        pass
        except Exception as exc:
            print(f"cached_service_probe_error={type(exc).__name__}:{exc}", flush=True)

        service, characteristic, resolve_error = await resolve_notify_characteristic(
            requester,
            service_uuid,
            notify_uuid,
        )
        if resolve_error is not None:
            last_error = resolve_error
            print(f"notify_resolve_error={resolve_error}", flush=True)
            try:
                recover_host_ble(device_name, address_hex)
            except subprocess.CalledProcessError:
                pass
            await asyncio.sleep(BLE_NOTIFY_RECOVERY_DELAY_SECONDS)
            continue
        await asyncio.sleep(BLE_NOTIFY_PRE_CCCD_SETTLE_SECONDS)

        def record_notify_failure(prefix: str, exc: Exception) -> RuntimeError:
            return RuntimeError(
                f"capture_audio_ble_wav: {prefix} raised {type(exc).__name__}: {exc} attempt={attempt}"
            )

        cccd_timed_out = False
        try:
            cccd_result = await asyncio.wait_for(
                characteristic.write_client_characteristic_configuration_descriptor_with_result_async(
                    GattCccdValue.NOTIFY
                ),
                timeout=BLE_NOTIFY_CCCD_TIMEOUT_SECONDS,
            )
            cccd_status = cccd_result.status
            protocol_error = getattr(cccd_result, "protocol_error", None)
            print(
                f"notify_enable_status={cccd_status} protocol_error={protocol_error}",
                flush=True,
            )
            if cccd_status == GattCommunicationStatus.SUCCESS:
                token = characteristic.add_value_changed(on_value_changed)
                return requester, service, characteristic, token
            last_error = RuntimeError(
                "capture_audio_ble_wav: notify enable failed "
                f"status={cccd_status} protocol_error={protocol_error} attempt={attempt}"
            )
        except asyncio.TimeoutError:
            cccd_timed_out = True
            print(
                f"notify_enable_primary_timeout=1 timeout_s={BLE_NOTIFY_CCCD_TIMEOUT_SECONDS}",
                flush=True,
            )
            last_error = RuntimeError(
                f"capture_audio_ble_wav: notify enable timed out after {BLE_NOTIFY_CCCD_TIMEOUT_SECONDS}s attempt={attempt}"
            )
        except OSError as exc:
            print(f"notify_enable_primary_error={exc!r}", flush=True)
            last_error = RuntimeError(
                f"capture_audio_ble_wav: notify enable raised {exc!r} attempt={attempt}"
            )
        except Exception as exc:
            print(f"notify_enable_primary_error={type(exc).__name__}:{exc}", flush=True)
            last_error = RuntimeError(
                f"capture_audio_ble_wav: notify enable raised {type(exc).__name__}: {exc} attempt={attempt}"
            )

        should_try_warmup_fallback = token is None
        if should_try_warmup_fallback:
            try:
                # Intentional deviation from documented CCCD-first order:
                # The primary path already tried CCCD write and failed (typically
                # with OSError(22)).  The warmup path hooks ValueChanged first to
                # prime the WinRT GATT stack, then writes CCCD.  This reversed
                # order is safe here because the stack is in a known-bad state
                # and needs the ValueChanged registration to accept the CCCD write.
                # Do NOT use this order in the primary path.
                token = characteristic.add_value_changed(on_value_changed)
                print("notify_enable_fallback=winrt_warmup", flush=True)
                try:
                    warmup_status = await asyncio.wait_for(
                        characteristic.write_client_characteristic_configuration_descriptor_async(
                            GattCccdValue.NOTIFY
                        ),
                        timeout=BLE_NOTIFY_CCCD_TIMEOUT_SECONDS,
                    )
                    print(f"notify_enable_warmup_status={warmup_status}", flush=True)
                    if warmup_status == GattCommunicationStatus.SUCCESS:
                        return requester, service, characteristic, token
                except asyncio.TimeoutError:
                    print("notify_enable_warmup_timeout=1", flush=True)
                except OSError as exc:
                    print(f"notify_enable_warmup_error={exc!r}", flush=True)
                except Exception as exc:
                    print(
                        f"notify_enable_warmup_error={type(exc).__name__}:{exc}",
                        flush=True,
                    )

                cccd_result = await asyncio.wait_for(
                    characteristic.write_client_characteristic_configuration_descriptor_with_result_async(
                        GattCccdValue.NOTIFY
                    ),
                    timeout=BLE_NOTIFY_CCCD_TIMEOUT_SECONDS,
                )
                cccd_status = cccd_result.status
                protocol_error = getattr(cccd_result, "protocol_error", None)
                print(
                    f"notify_enable_fallback_status={cccd_status} protocol_error={protocol_error}",
                    flush=True,
                )
                if cccd_status == GattCommunicationStatus.SUCCESS:
                    return requester, service, characteristic, token
                last_error = RuntimeError(
                    "capture_audio_ble_wav: notify fallback failed "
                    f"status={cccd_status} protocol_error={protocol_error} attempt={attempt}"
                )
            except asyncio.TimeoutError:
                last_error = RuntimeError(
                    f"capture_audio_ble_wav: notify fallback timed out attempt={attempt}"
                )
            except OSError as exc:
                last_error = RuntimeError(
                    f"capture_audio_ble_wav: notify fallback raised {exc!r} attempt={attempt}"
                )
            except Exception as exc:
                last_error = record_notify_failure("notify fallback", exc)
            if token is not None:
                try:
                    characteristic.remove_value_changed(token)
                except Exception:
                    pass
                token = None

        try:
            recover_host_ble(device_name, address_hex)
        except subprocess.CalledProcessError:
            pass
        await asyncio.sleep(BLE_NOTIFY_RECOVERY_DELAY_SECONDS)

    close_ble_objects(
        characteristic=characteristic,
        token=token,
        service=service,
        requester=requester,
    )
    raise last_error if last_error is not None else RuntimeError(
        "capture_audio_ble_wav: notify enable failed without detailed error"
    )


def send_toggle(ser: Serial) -> None:
    ser.write(b"~VREC:TOGGLE\n")
    ser.flush()


def send_cancel(ser: Serial) -> None:
    ser.write(b"~VREC:CANCEL\n")
    ser.flush()


def open_serial_with_retry(port: str, baudrate: int = 115200, timeout: float = 0.05) -> Serial:
    last_error = None
    for attempt in range(1, SERIAL_OPEN_RETRY_COUNT + 1):
        try:
            if attempt > 1:
                print(f"serial_open_retry_attempt={attempt}", flush=True)
            ser = serial.Serial()
            ser.port = port
            ser.baudrate = baudrate
            ser.timeout = timeout
            ser.dsrdtr = False
            ser.rtscts = False
            ser.dtr = False
            ser.rts = False
            ser.open()
            ser.setDTR(False)
            ser.setRTS(False)
            return ser
        except (OSError, serial.SerialException) as exc:
            last_error = exc
            if attempt >= SERIAL_OPEN_RETRY_COUNT:
                break
            time.sleep(SERIAL_OPEN_RETRY_DELAY_SECONDS)
    raise RuntimeError(
        "capture_audio_ble_wav: unable to open serial port "
        f"{port} after {SERIAL_OPEN_RETRY_COUNT} attempts; last_error={last_error!r}"
    )


def reset_target_before_capture(ser: Serial) -> None:
    ser.setDTR(False)
    ser.setRTS(True)
    time.sleep(SERIAL_RESET_PULSE_SECONDS)
    ser.setRTS(False)
    time.sleep(SERIAL_RESET_SETTLE_SECONDS)


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
    payload_end = min(len(packet), header_len + payload_len)
    payload = packet[header_len:payload_end]
    return {
        "packet_type": packet_type,
        "session_id": session_id,
        "chunk_index": chunk_index,
        "fragment_index": fragment_index,
        "fragment_count": fragment_count,
        "payload_len": payload_len,
        "chunk_pcm_bytes": chunk_pcm_bytes,
        "payload": payload,
        "packet_sequence": chunk_index,
        "expected_packet_count": chunk_index,
        "packet_pcm_bytes": chunk_pcm_bytes,
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
        self._lock = threading.Lock()
        self.session_id = None
        self.stop_received = False
        self.expected_packet_count = None
        self.cancel_received = False
        self.error_received = False
        self.session_error_code = None
        self.session_error_name = ""
        self.audio_packets = {}
        self.packet_pcm_bytes = {}
        self.packet_size_counter = Counter()
        self.last_packet_time = 0.0
        self.explicit_start_received = False
        self.start_inferred_from_audio = False

    def reset(self) -> None:
        with self._lock:
            self.session_id = None
            self.stop_received = False
            self.expected_packet_count = None
            self.cancel_received = False
            self.error_received = False
            self.session_error_code = None
            self.session_error_name = ""
            self.audio_packets = {}
            self.packet_pcm_bytes = {}
            self.packet_size_counter = Counter()
            self.last_packet_time = 0.0
            self.explicit_start_received = False
            self.start_inferred_from_audio = False

    def handle_notification(self, packet: bytes) -> None:
        header = parse_header(packet)
        packet_type = header["packet_type"]
        with self._lock:
            self.last_packet_time = time.time()
            if packet_type == PACKET_TYPE_SESSION_START:
                if self.session_id != header["session_id"]:
                    self.session_id = None
                    self.stop_received = False
                    self.expected_packet_count = None
                    self.cancel_received = False
                    self.error_received = False
                    self.session_error_code = None
                    self.session_error_name = ""
                    self.audio_packets = {}
                    self.packet_pcm_bytes = {}
                    self.packet_size_counter = Counter()
                    self.session_id = header["session_id"]
                    self.start_inferred_from_audio = False
                self.explicit_start_received = True
                return
            if self.session_id is None:
                if packet_type != PACKET_TYPE_AUDIO_DATA:
                    return
                self.session_id = header["session_id"]
                self.start_inferred_from_audio = True
            if header["session_id"] != self.session_id:
                return
            if packet_type == PACKET_TYPE_AUDIO_DATA:
                packet_sequence = header["packet_sequence"]
                packet_pcm_bytes = header["packet_pcm_bytes"] or header["payload_len"]
                if packet_pcm_bytes <= 0:
                    packet_pcm_bytes = len(header["payload"])
                payload = header["payload"][:packet_pcm_bytes]
                if not payload:
                    return

                existing_payload = self.audio_packets.get(packet_sequence)
                if existing_payload is not None and len(existing_payload) >= len(payload):
                    return

                if existing_payload is not None:
                    existing_size = self.packet_pcm_bytes.get(packet_sequence, len(existing_payload))
                    self.packet_size_counter.subtract([existing_size])
                    if self.packet_size_counter[existing_size] <= 0:
                        del self.packet_size_counter[existing_size]

                self.audio_packets[packet_sequence] = payload
                self.packet_pcm_bytes[packet_sequence] = len(payload)
                self.packet_size_counter.update([len(payload)])
            elif packet_type == PACKET_TYPE_SESSION_STOP:
                self.stop_received = True
                self.expected_packet_count = header["expected_packet_count"]
            elif packet_type == PACKET_TYPE_SESSION_CANCEL:
                self.cancel_received = True
                self.stop_received = True
                self.expected_packet_count = header["expected_packet_count"]
            elif packet_type == PACKET_TYPE_SESSION_ERROR:
                self.error_received = True
                self.stop_received = True
                self.expected_packet_count = header["expected_packet_count"]
                error_code = header["packet_pcm_bytes"]
                self.session_error_code = error_code
                self.session_error_name = SESSION_ERROR_NAMES.get(error_code, f"unknown_{error_code}")

    def has_completed_session(self) -> bool:
        with self._lock:
            if (
                self.session_id is None
                or not self.stop_received
                or self.cancel_received
                or self.error_received
                or self.expected_packet_count is None
                or self.expected_packet_count == 0
            ):
                return False
            if not self.is_packet_complete():
                return False
            return (time.time() - self.last_packet_time) >= SESSION_COMPLETE_IDLE_SECONDS

    def has_started_session(self) -> bool:
        with self._lock:
            return self.session_id is not None

    def received_packet_count(self) -> int:
        return len(self.audio_packets)

    def received_pcm_bytes(self) -> int:
        return sum(self.packet_pcm_bytes.values())

    def missing_packet_indices(self) -> list[int]:
        if self.expected_packet_count is None:
            return []
        return [
            index
            for index in range(self.expected_packet_count)
            if index not in self.audio_packets
        ]

    def is_packet_complete(self) -> bool:
        if self.expected_packet_count is None:
            return False
        return len(self.missing_packet_indices()) == 0

    def packet_integrity_summary(self) -> str:
        expected = (
            str(self.expected_packet_count)
            if self.expected_packet_count is not None
            else "unknown"
        )
        missing = self.missing_packet_indices()
        missing_preview = ",".join(str(index) for index in missing[:16])
        if len(missing) > 16:
            missing_preview += ",..."
        if not missing_preview:
            missing_preview = "<none>"
        return (
            f"expected_packet_count={expected} "
            f"received_packet_count={self.received_packet_count()} "
            f"missing_packet_count={len(missing)} "
            f"missing_packet_indices={missing_preview} "
            f"received_pcm_bytes={self.received_pcm_bytes()}"
        )

    def _inferred_cycle_length(self) -> int:
        if len(self.packet_pcm_bytes) < 6:
            return 1

        best_cycle_length = 1
        best_match_count = -1
        best_score = -1.0
        for cycle_length in range(1, 17):
            buckets = {}
            for sequence, size in self.packet_pcm_bytes.items():
                buckets.setdefault(sequence % cycle_length, Counter()).update([size])
            match_count = 0
            for sequence, size in self.packet_pcm_bytes.items():
                predicted_size, _ = buckets[sequence % cycle_length].most_common(1)[0]
                if predicted_size == size:
                    match_count += 1
            score = match_count / float(len(self.packet_pcm_bytes))
            if score > best_score or (score == best_score and match_count > best_match_count):
                best_cycle_length = cycle_length
                best_match_count = match_count
                best_score = score
        return best_cycle_length

    def _build_cycle_size_map(self, cycle_length: int) -> dict[int, int]:
        cycle_size_map = {}
        if cycle_length <= 0:
            return cycle_size_map
        for sequence, size in self.packet_pcm_bytes.items():
            cycle_size_map.setdefault(sequence % cycle_length, Counter()).update([size])
        return {
            offset: bucket.most_common(1)[0][0]
            for offset, bucket in cycle_size_map.items()
            if bucket
        }

    def inferred_packet_pcm_bytes(self, packet_sequence: int) -> int:
        actual_size = self.packet_pcm_bytes.get(packet_sequence)
        if actual_size is not None:
            return actual_size

        if self.packet_size_counter:
            cycle_length = self._inferred_cycle_length()
            cycle_size_map = self._build_cycle_size_map(cycle_length)
            inferred_size = cycle_size_map.get(packet_sequence % cycle_length)
            if inferred_size is not None:
                return inferred_size
            return self.packet_size_counter.most_common(1)[0][0]
        return 0

    def reconstructed_pcm(self) -> bytes:
        with self._lock:
            if self.expected_packet_count is None:
                return b"".join(self.audio_packets[index] for index in sorted(self.audio_packets))

            parts = []
            for packet_sequence in range(self.expected_packet_count):
                payload = self.audio_packets.get(packet_sequence)
                if payload is not None:
                    parts.append(payload)
                    continue

                inferred_size = self.inferred_packet_pcm_bytes(packet_sequence)
                if inferred_size > 0:
                    parts.append(bytes(inferred_size))
            return b"".join(parts)


async def run_ble_capture(args, ser: Serial, serial_monitor: SerialLogMonitor):
    explicit_address = getattr(args, "bluetooth_address", None)
    address_hex = "".join(
        ch for ch in str(explicit_address or "") if ch in "0123456789ABCDEFabcdef"
    ).upper() or None
    address = None
    if address_hex is None:
        address = get_paired_device_address(args.device_name)
        address_hex = get_paired_device_address_hex(args.device_name)
    if address_hex is None:
        raise RuntimeError(f"capture_audio_ble_wav: unable to resolve paired BLE device '{args.device_name}'")
    if address is None:
        address = ":".join(address_hex[i:i + 2] for i in range(0, 12, 2))
    print(
        "ble_address_source="
        f"{'argument' if explicit_address else 'paired_device_lookup'} address={address_hex}",
        flush=True,
    )

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

    def on_value_changed(sender, args):
        reader = DataReader.from_buffer(args.characteristic_value)
        payload = bytearray(reader.unconsumed_buffer_length)
        reader.read_bytes(payload)
        handle_notification(sender, payload)
        reader.close()

    def fail_if_unexpected_reset(context: str) -> None:
        if getattr(args, "reset_before_capture", False):
            return
        reset_lines = serial_monitor.find_lines_any(UNEXPECTED_RESET_MARKERS)
        if not reset_lines:
            return
        raise RuntimeError(
            "capture_audio_ble_wav: unexpected device reset detected while no-reset capture is active; "
            f"context={context}; reset_lines=\n"
            + "\n".join(reset_lines[-12:])
            + "\nrecent serial logs:\n"
            + serial_monitor.recent_text()
        )

    if getattr(args, "reset_before_capture", False):
        try:
            ensure_host_ble_connection(
                args.device_name,
                address_hex,
                duration_seconds=args.ble_connect_timeout_seconds,
                poll_interval_seconds=2,
            )
        except subprocess.CalledProcessError:
            pass
        try:
            await serial_monitor.wait_for_predicate(
                lambda line: BLE_CONNECTION_ESTABLISHED_MARKER in line,
                timeout_seconds=args.ble_connect_timeout_seconds,
                description="BLE connection established marker",
            )
        except RuntimeError:
            print(
                "ble_connection_marker_wait=timeout; continuing with host-side notify enable",
                flush=True,
            )
        await asyncio.sleep(0.5)

    requester, service, characteristic, token = await enable_notify_with_rebuild(
        address_hex=address_hex,
        device_name=args.device_name,
        service_uuid=service_uuid,
        notify_uuid=notify_uuid,
        on_value_changed=on_value_changed,
    )

    try:
        try:
            await serial_monitor.wait_for_predicate(
                line_indicates_notify_ready,
                timeout_seconds=min(args.notify_ready_timeout_seconds, 3),
                description="audio notify enabled marker",
            )
        except RuntimeError:
            print(
                "notify_ready_marker_wait=timeout; continuing after host-side notify enable",
                flush=True,
            )
        fail_if_unexpected_reset("after_notify_ready_wait")
        try:
            await serial_monitor.wait_for_predicate(
                lambda line: AUDIO_NOTIFY_PACKET_SIZE_MARKER in line,
                timeout_seconds=min(args.notify_ready_timeout_seconds, 3),
                description="audio notify packet size marker",
            )
        except RuntimeError:
            print(
                "notify_packet_size_marker_wait=timeout; continuing after host-side notify enable",
                flush=True,
            )
        fail_if_unexpected_reset("after_packet_size_wait")
        await asyncio.sleep(NOTIFY_READY_SETTLE_SECONDS)
        fail_if_unexpected_reset("after_notify_settle")

        if args.trigger_mode == "physical-key":
            print("ready_for_key=1", flush=True)
            print("trigger_mode=physical-key", flush=True)
            print("instruction=press KEY1 once to start recording, then press KEY1 again to stop", flush=True)

        target_sessions = args.max_sessions if args.max_sessions > 0 else None
        capture_seconds_per_session = getattr(args, "capture_seconds_per_session", None)
        session_pre_start_delay_seconds = getattr(args, "session_pre_start_delay_seconds", None)
        session_cancel_after_start_seconds = getattr(args, "session_cancel_after_start_seconds", None)
        session_cancel_post_wait_seconds = float(getattr(args, "session_cancel_post_wait_seconds", 2.0))
        while target_sessions is None or completed_sessions < target_sessions:
            collector.reset()
            session_capture_seconds = int(args.capture_seconds)
            if capture_seconds_per_session is not None and completed_sessions < len(capture_seconds_per_session):
                session_capture_seconds = int(capture_seconds_per_session[completed_sessions])
            session_pre_start_delay_seconds_value = 0.0
            if (
                session_pre_start_delay_seconds is not None
                and completed_sessions < len(session_pre_start_delay_seconds)
            ):
                session_pre_start_delay_seconds_value = max(
                    0.0,
                    float(session_pre_start_delay_seconds[completed_sessions]),
                )

            if args.trigger_mode == "serial-toggle":
                if session_pre_start_delay_seconds_value > 0.0:
                    print(
                        "session_pre_start_delay_seconds="
                        f"{session_pre_start_delay_seconds_value:.2f} session_index={completed_sessions + 1}",
                        flush=True,
                    )
                    pre_start_deadline = time.time() + session_pre_start_delay_seconds_value
                    while time.time() < pre_start_deadline:
                        serial_monitor.poll_lines()
                        fail_if_unexpected_reset("during_pre_start_delay")
                        await asyncio.sleep(0.05)
                send_toggle(ser)
                start_deadline = time.time() + max(30, int(session_capture_seconds * 2 + 10))
                while time.time() < start_deadline:
                    serial_monitor.poll_lines()
                    fail_if_unexpected_reset("during_session_start_wait")
                    if collector.has_started_session():
                        break
                    await asyncio.sleep(0.05)

                if not collector.has_started_session():
                    raise RuntimeError(
                        "capture_audio_ble_wav: timed out waiting for serial-toggle session start; "
                        f"budget_seconds={max(30, int(session_capture_seconds * 2 + 10))}; recent serial logs:\n"
                        f"{serial_monitor.recent_text()}"
                    )

                if session_cancel_after_start_seconds is not None:
                    cancel_delay_seconds = max(0.0, float(session_cancel_after_start_seconds))
                    print(
                        f"session_cancel_after_start_seconds={cancel_delay_seconds:.2f} "
                        f"session_index={completed_sessions + 1}",
                        flush=True,
                    )
                    cancel_deadline = time.time() + cancel_delay_seconds
                    while time.time() < cancel_deadline:
                        serial_monitor.poll_lines()
                        fail_if_unexpected_reset("during_cancel_probe_hold")
                        await asyncio.sleep(0.05)
                    send_cancel(ser)
                    post_cancel_deadline = time.time() + session_cancel_post_wait_seconds
                    while time.time() < post_cancel_deadline:
                        serial_monitor.poll_lines()
                        fail_if_unexpected_reset("during_cancel_probe_wait")
                        await asyncio.sleep(0.05)

                    full_text = serial_monitor.full_text()
                    cancel_requested = "record session cancel requested" in full_text
                    cancel_completed = (
                        collector.cancel_received
                        or "record session canceled" in full_text
                        or "recording cancel source=" in full_text
                        or "record session canceled before activation" in full_text
                    )
                    serial_transport_summary_lines = serial_monitor.find_lines(STREAM_TRANSPORT_SUMMARY_MARKER)
                    completed_sessions += 1
                    session_summary = {
                        "wav_path": "",
                        "serial_log_path": str(args.serial_log_path) if args.serial_log_path else "",
                        "session_id": collector.session_id,
                        "cancel_requested": cancel_requested,
                        "cancel_completed": cancel_completed,
                        "cancel_received": collector.cancel_received,
                        "expected_packet_count": collector.expected_packet_count,
                        "received_packet_count": collector.received_packet_count(),
                        "missing_packet_count": len(collector.missing_packet_indices()),
                        "missing_packet_indices": collector.missing_packet_indices(),
                        "received_pcm_bytes": collector.received_pcm_bytes(),
                        "serial_transport_summary_count": len(serial_transport_summary_lines),
                        "serial_transport_summary_last": serial_transport_summary_lines[-1] if serial_transport_summary_lines else "",
                        "completed_session_count": completed_sessions,
                        "capture_seconds_target": session_capture_seconds,
                        "pre_start_delay_seconds": session_pre_start_delay_seconds_value,
                        "cancel_hold_seconds": cancel_delay_seconds,
                        "result": "pass" if cancel_requested and cancel_completed else "fail",
                    }
                    session_summaries.append(session_summary)
                    if args.serial_log_path:
                        serial_log_path = pathlib.Path(args.serial_log_path)
                        serial_log_path.parent.mkdir(parents=True, exist_ok=True)
                        serial_log_path.write_text(full_text, encoding="utf-8")
                    print(f"cancel_probe_result={session_summary['result']}", flush=True)
                    print(f"cancel_requested={1 if cancel_requested else 0}", flush=True)
                    print(f"cancel_completed={1 if cancel_completed else 0}", flush=True)
                    print(f"cancel_received={1 if collector.cancel_received else 0}", flush=True)
                    if serial_transport_summary_lines:
                        print(f"serial_transport_summary_last={serial_transport_summary_lines[-1]}", flush=True)
                    continue

                capture_deadline = time.time() + session_capture_seconds
                while time.time() < capture_deadline:
                    serial_monitor.poll_lines()
                    fail_if_unexpected_reset("during_capture_window")
                    await asyncio.sleep(0.05)
                send_toggle(ser)
            else:
                start_deadline = time.time() + PHYSICAL_KEY_START_TIMEOUT_SECONDS
                while time.time() < start_deadline:
                    serial_monitor.poll_lines()
                    fail_if_unexpected_reset("during_physical_key_start_wait")
                    if collector.has_started_session():
                        break
                    await asyncio.sleep(0.1)

                if not collector.has_started_session():
                    raise RuntimeError(
                        "capture_audio_ble_wav: timed out waiting for physical-key session start; "
                        f"budget_seconds={PHYSICAL_KEY_START_TIMEOUT_SECONDS}; recent serial logs:\n"
                        f"{serial_monitor.recent_text()}"
                    )

            deadline = time.time() + args.timeout_seconds
            while time.time() < deadline:
                serial_monitor.poll_lines()
                fail_if_unexpected_reset("during_session_complete_wait")
                if collector.has_completed_session():
                    break
                if collector.error_received:
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
                    + "\npacket_size_lines=\n"
                    + "\n".join(serial_monitor.find_lines(AUDIO_NOTIFY_PACKET_SIZE_MARKER))
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
            if collector.cancel_received:
                raise RuntimeError(
                    "capture_audio_ble_wav: session canceled before completion; recent serial logs:\n"
                    f"{serial_monitor.recent_text()}"
                )
            if collector.error_received:
                raise RuntimeError(
                    "capture_audio_ble_wav: session error before completion; "
                    f"session_error_code={collector.session_error_code} "
                    f"session_error_name={collector.session_error_name}; "
                    "recent serial logs:\n"
                    f"{serial_monitor.recent_text()}"
                )
            if collector.expected_packet_count is None:
                raise RuntimeError(
                    "capture_audio_ble_wav: session_stop did not provide expected packet count; recent serial logs:\n"
                    f"{serial_monitor.recent_text()}"
                )
            if collector.received_packet_count() == 0:
                raise RuntimeError(
                    "capture_audio_ble_wav: did not receive any audio packets; recent serial logs:\n"
                    f"{serial_monitor.recent_text()}"
                )
            reconstructed_pcm = collector.reconstructed_pcm()
            output_dir = pathlib.Path(args.output_dir)
            latest_output_path = output_dir / "capture_ble_latest_16k_mono.wav"
            if target_sessions is not None and target_sessions > 1:
                output_path = output_dir / f"capture_ble_session_{completed_sessions + 1}_16k_mono.wav"
            else:
                output_path = latest_output_path
            write_wav(output_path, reconstructed_pcm)
            if output_path != latest_output_path:
                write_wav(latest_output_path, reconstructed_pcm)
            completed_sessions += 1
            duration_seconds = pcm_duration_seconds(len(reconstructed_pcm))
            missing_packet_indices = collector.missing_packet_indices()
            serial_stream_audio_lines = serial_monitor.find_lines_any(
                (STREAM_SESSION_AUDIO_MARKER, STREAM_SESSION_CHUNK_MARKER)
            )
            serial_transport_summary_lines = serial_monitor.find_lines(STREAM_TRANSPORT_SUMMARY_MARKER)

            session_summary = {
                "wav_path": str(output_path),
                "serial_log_path": str(args.serial_log_path) if args.serial_log_path else "",
                "session_id": collector.session_id,
                "received_packet_count": collector.received_packet_count(),
                "expected_packet_count": collector.expected_packet_count,
                "missing_packet_count": len(missing_packet_indices),
                "missing_packet_indices": missing_packet_indices,
                "session_error_code": collector.session_error_code,
                "session_error_name": collector.session_error_name,
                "received_pcm_bytes": collector.received_pcm_bytes(),
                "pcm_bytes": len(reconstructed_pcm),
                "duration_seconds": duration_seconds,
                "serial_stream_start_count": len(serial_monitor.find_lines(STREAM_SESSION_START_MARKER)),
                "serial_stream_audio_count": len(serial_stream_audio_lines),
                "serial_stream_stop_count": len(serial_monitor.find_lines(STREAM_SESSION_STOP_MARKER)),
                "serial_transport_summary_count": len(serial_transport_summary_lines),
                "serial_transport_summary_last": serial_transport_summary_lines[-1] if serial_transport_summary_lines else "",
                "serial_upload_begin_count": len(serial_monitor.find_lines(AUDIO_UPLOAD_BEGIN_MARKER)),
                "serial_upload_end_count": len(serial_monitor.find_lines(AUDIO_UPLOAD_END_MARKER)),
                "serial_upload_skipped_count": len(serial_monitor.find_lines(AUDIO_UPLOAD_SKIPPED_MARKER)),
                "explicit_start_received": collector.explicit_start_received,
                "start_inferred_from_audio": collector.start_inferred_from_audio,
                "completed_session_count": completed_sessions,
                "chunk_count": collector.received_packet_count(),
                "expected_chunk_count": collector.expected_packet_count,
                "missing_chunk_count": len(missing_packet_indices),
                "serial_stream_chunk_count": len(serial_stream_audio_lines),
                "capture_seconds_target": session_capture_seconds,
                "pre_start_delay_seconds": session_pre_start_delay_seconds_value,
            }
            session_summaries.append(session_summary)

            if args.serial_log_path:
                serial_log_path = pathlib.Path(args.serial_log_path)
                serial_log_path.parent.mkdir(parents=True, exist_ok=True)
                serial_log_path.write_text(serial_monitor.full_text(), encoding="utf-8")

            print(f"wav_path={output_path}", flush=True)
            print(f"session_id={collector.session_id}", flush=True)
            print(f"received_packet_count={collector.received_packet_count()}", flush=True)
            print(f"expected_packet_count={collector.expected_packet_count}", flush=True)
            print(f"missing_packet_count={len(missing_packet_indices)}", flush=True)
            if collector.session_error_code is not None:
                print(f"session_error_code={collector.session_error_code}", flush=True)
                print(f"session_error_name={collector.session_error_name}", flush=True)
            print(f"received_pcm_bytes={collector.received_pcm_bytes()}", flush=True)
            print(f"chunk_count={collector.received_packet_count()}", flush=True)
            print(f"expected_chunk_count={collector.expected_packet_count}", flush=True)
            print(f"missing_chunk_count={len(missing_packet_indices)}", flush=True)
            print(f"pcm_bytes={len(reconstructed_pcm)}", flush=True)
            print(f"duration_seconds={duration_seconds:.3f}", flush=True)
            print(f"capture_seconds_target={session_capture_seconds}", flush=True)
            print(f"pre_start_delay_seconds={session_pre_start_delay_seconds_value:.2f}", flush=True)
            print(
                f"serial_stream_start_count={len(serial_monitor.find_lines(STREAM_SESSION_START_MARKER))}",
                flush=True,
            )
            print(
                f"serial_stream_audio_count={len(serial_stream_audio_lines)}",
                flush=True,
            )
            print(
                f"serial_stream_chunk_count={len(serial_stream_audio_lines)}",
                flush=True,
            )
            print(
                f"serial_stream_stop_count={len(serial_monitor.find_lines(STREAM_SESSION_STOP_MARKER))}",
                flush=True,
            )
            print(
                f"serial_transport_summary_count={len(serial_transport_summary_lines)}",
                flush=True,
            )
            if serial_transport_summary_lines:
                print(f"serial_transport_summary_last={serial_transport_summary_lines[-1]}", flush=True)
            print(f"explicit_start_received={1 if collector.explicit_start_received else 0}", flush=True)
            print(f"start_inferred_from_audio={1 if collector.start_inferred_from_audio else 0}", flush=True)
            print(
                f"serial_upload_begin_count={len(serial_monitor.find_lines(AUDIO_UPLOAD_BEGIN_MARKER))}",
                flush=True,
            )
            print(
                f"serial_upload_end_count={len(serial_monitor.find_lines(AUDIO_UPLOAD_END_MARKER))}",
                flush=True,
            )
            print(
                f"serial_upload_skipped_count={len(serial_monitor.find_lines(AUDIO_UPLOAD_SKIPPED_MARKER))}",
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
            serial_log_path.write_text(serial_monitor.full_text(), encoding="utf-8")
        await disable_notify_best_effort(characteristic)
        close_ble_objects(
            characteristic=characteristic,
            token=token,
            service=service,
            requester=requester,
        )

    if completed_sessions == 0:
        raise RuntimeError(
            "capture_audio_ble_wav: no completed session captured; recent serial logs:\n"
            f"{serial_monitor.recent_text()}"
        )
    return session_summaries


async def run_capture_with_args(args):
    with open_serial_with_retry(args.port, 115200, timeout=0.05) as ser:
        ser.setDTR(False)
        ser.setRTS(False)
        ser.reset_input_buffer()
        serial_monitor = SerialLogMonitor(ser)
        if args.reset_before_capture:
            reset_target_before_capture(ser)
            await wait_for_ready_markers_or_running(serial_monitor, args.boot_timeout_seconds)
        ser.reset_input_buffer()
        return await run_ble_capture(args, ser, serial_monitor)


def main():
    configure_utf8_stdio()
    args = parse_args()
    # Auto-calculate timeout if not explicitly set
    if args.timeout_seconds is None:
        args.timeout_seconds = args.capture_seconds + 45
    asyncio.run(run_capture_with_args(args))


if __name__ == "__main__":
    main()
