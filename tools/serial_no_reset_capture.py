from __future__ import annotations

import argparse
import re
import sys
import time
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Send Listener serial commands without toggling reset lines.")
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--command", action="append", default=[])
    parser.add_argument("--command-list", default="")
    parser.add_argument("--initial-read-ms", type=int, default=800)
    parser.add_argument("--command-read-ms", type=int, default=1200)
    parser.add_argument("--write-timeout-ms", type=int, default=5000)
    parser.add_argument("--write-retries", type=int, default=3)
    parser.add_argument("--write-retry-delay-ms", type=int, default=250)
    parser.add_argument("--command-delay-ms", type=int, default=250)
    parser.add_argument("--keep-input-between-commands", action="store_true")
    parser.add_argument("--capture-only-ms", type=int, default=0)
    parser.add_argument("--signal-event-name", default="")
    parser.add_argument("--signal-event-on-output", default="")
    parser.add_argument("--scheduled-command-after-event-ms", type=int, default=-1)
    parser.add_argument("--scheduled-command", default="")
    parser.add_argument("--scheduled-command-read-ms", type=int, default=0)
    parser.add_argument("--output-path", default="")
    return parser.parse_args()


class Transcript:
    def __init__(self, output_path: str = "") -> None:
        self.lines: list[str] = []
        self.output_path = Path(output_path) if output_path else None
        if self.output_path is not None:
            self.output_path.parent.mkdir(parents=True, exist_ok=True)
            self.output_path.write_text("", encoding="utf-8")

    def add(self, text: str) -> None:
        print(text, flush=True)
        self.lines.append(text)
        if self.output_path is not None:
            with self.output_path.open("a", encoding="utf-8") as output:
                output.write(text + "\n")

    def add_text(self, text: str) -> None:
        for line in text.replace("\r", "\n").split("\n"):
            if line.strip():
                self.add(line)

    def write(self, output_path: str) -> None:
        if not output_path:
            return
        path = Path(output_path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("\n".join(self.lines) + "\n", encoding="utf-8")
        self.add(f"transcript={path.resolve()}")


def open_serial_no_reset(serial_module, port: str, baud: int, write_timeout_ms: int):
    ser = serial_module.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 0.08
    ser.write_timeout = max(write_timeout_ms, 1) / 1000.0
    ser.dsrdtr = False
    ser.rtscts = False
    ser.dtr = False
    ser.rts = False
    ser.open()
    ser.dtr = False
    ser.rts = False
    return ser


class OutputSignal:
    def __init__(self, event_name: str, output_pattern: str, transcript: Transcript) -> None:
        self.event_name = event_name
        self.output_pattern = output_pattern.lower()
        self.transcript = transcript
        self.triggered_at: float | None = None
        self._tail = ""
        self._handle = None

        if sys.platform != "win32":
            raise RuntimeError("named output signals are only supported on Windows")

        import ctypes

        self._kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self._kernel32.CreateEventW.restype = ctypes.c_void_p
        self._handle = self._kernel32.CreateEventW(None, False, False, event_name)
        if not self._handle:
            raise OSError(ctypes.get_last_error(), f"CreateEventW failed for {event_name}")

    def observe(self, data: bytes) -> None:
        if self.triggered_at is not None:
            return
        self._tail = (self._tail + data.decode("utf-8", errors="replace"))[-4096:]
        if self.output_pattern not in self._tail.lower():
            return
        if not self._kernel32.SetEvent(self._handle):
            import ctypes

            raise OSError(ctypes.get_last_error(), f"SetEvent failed for {self.event_name}")
        self.triggered_at = time.monotonic()
        self.transcript.add(
            f"output_signal event={self.event_name} pattern={self.output_pattern}"
        )

    def close(self) -> None:
        if self._handle:
            self._kernel32.CloseHandle(self._handle)
            self._handle = None


def read_serial_for(
    ser,
    transcript: Transcript,
    milliseconds: int,
    output_signal: OutputSignal | None = None,
) -> None:
    deadline = time.monotonic() + max(milliseconds, 0) / 1000.0
    chunks: list[bytes] = []
    while time.monotonic() < deadline:
        try:
            data = ser.read(4096)
        except Exception as exc:  # pragma: no cover - hardware path
            transcript.add(f"READ_ERROR {exc}")
            break
        if data:
            chunks.append(data)
            if output_signal is not None:
                output_signal.observe(data)
        else:
            time.sleep(0.02)
    if chunks:
        transcript.add_text(b"".join(chunks).decode("utf-8", errors="replace"))


def write_serial_command(ser, command: str, args: argparse.Namespace, transcript: Transcript) -> None:
    payload = f"{command}\n".encode("utf-8")
    for attempt in range(1, args.write_retries + 1):
        try:
            ser.write(payload)
            ser.flush()
            return
        except Exception as exc:
            if attempt >= args.write_retries:
                transcript.add(
                    f"WRITE_ERROR command={command} attempt={attempt}/{args.write_retries} error={exc}"
                )
                raise
            transcript.add(
                f"WRITE_RETRY command={command} attempt={attempt}/{args.write_retries} error={exc}"
            )
            try:
                ser.reset_output_buffer()
            except Exception as discard_exc:
                transcript.add(f"WRITE_RETRY_DISCARD_OUT_ERROR {discard_exc}")
            time.sleep(max(args.write_retry_delay_ms, 0) / 1000.0)


def command_list(args: argparse.Namespace) -> list[str]:
    commands = [command.strip() for command in args.command if command and command.strip()]
    if args.command_list.strip():
        commands.extend(
            command.strip()
            for command in re.split(r";;|[;\r\n]+", args.command_list)
            if command and command.strip()
        )
    return commands


def parse_wait_command(command: str) -> int | None:
    upper = command.upper()
    for prefix in ("WAIT:", "__WAIT_MS:"):
        if upper.startswith(prefix):
            value = command[len(prefix):].strip()
            if value.isdecimal():
                return int(value)
    return None


def parse_read_ms_command(command: str) -> int | None:
    upper = command.upper()
    for prefix in ("READMS:", "READ_MS:", "__READ_MS:"):
        if upper.startswith(prefix):
            value = command[len(prefix):].strip()
            if value.isdecimal():
                return int(value)
    return None


def main() -> int:
    args = parse_args()
    commands = command_list(args)
    transcript = Transcript(args.output_path)
    if args.capture_only_ms < 0:
        transcript.add("ERROR --capture-only-ms must be >= 0.")
        transcript.write(args.output_path)
        return 2
    if not commands and args.capture_only_ms <= 0:
        transcript.add(
            "ERROR At least one --command/--command-list entry or a positive --capture-only-ms is required."
        )
        transcript.write(args.output_path)
        return 2
    signal_requested = bool(args.signal_event_name or args.signal_event_on_output)
    if signal_requested != bool(args.signal_event_name and args.signal_event_on_output):
        transcript.add("ERROR --signal-event-name and --signal-event-on-output must be supplied together.")
        transcript.write(args.output_path)
        return 2
    schedule_requested = args.scheduled_command_after_event_ms >= 0 or bool(args.scheduled_command)
    if schedule_requested and (
        not signal_requested
        or args.scheduled_command_after_event_ms < 0
        or not args.scheduled_command.strip()
    ):
        transcript.add(
            "ERROR scheduled commands require an output signal, a non-negative delay, and a command."
        )
        transcript.write(args.output_path)
        return 2

    try:
        import serial
    except Exception as exc:
        transcript.add(f"ERROR pyserial import failed: {exc}")
        transcript.write(args.output_path)
        return 2

    ser = None
    output_signal = None
    try:
        if signal_requested:
            output_signal = OutputSignal(
                args.signal_event_name,
                args.signal_event_on_output,
                transcript,
            )
        ser = open_serial_no_reset(serial, args.port, args.baud, args.write_timeout_ms)
        transcript.add(f"serial_opened port={args.port} baud={args.baud} dtr=0 rts=0 no_reset=1")
        read_serial_for(ser, transcript, args.initial_read_ms, output_signal)
        current_command_read_ms = args.command_read_ms
        if commands:
            for command in commands:
                transcript.add(f"> {command}")
                read_ms = parse_read_ms_command(command)
                if read_ms is not None:
                    current_command_read_ms = read_ms
                    transcript.add(f"command_read_ms={current_command_read_ms}")
                    continue
                wait_ms = parse_wait_command(command)
                if wait_ms is not None:
                    read_serial_for(ser, transcript, wait_ms, output_signal)
                    continue
                if not args.keep_input_between_commands:
                    try:
                        ser.reset_input_buffer()
                    except Exception as exc:
                        transcript.add(f"DISCARD_IN_ERROR {exc}")
                write_serial_command(ser, command, args, transcript)
                read_serial_for(ser, transcript, current_command_read_ms, output_signal)
                if args.command_delay_ms > 0:
                    time.sleep(args.command_delay_ms / 1000.0)

            if schedule_requested:
                if output_signal is None or output_signal.triggered_at is None:
                    raise RuntimeError("scheduled command trigger output was not observed")
                deadline = output_signal.triggered_at + args.scheduled_command_after_event_ms / 1000.0
                remaining_ms = max(0, round((deadline - time.monotonic()) * 1000))
                transcript.add(f"scheduled_command_wait_ms={remaining_ms}")
                read_serial_for(ser, transcript, remaining_ms, output_signal)
                transcript.add(f"> {args.scheduled_command}")
                write_serial_command(ser, args.scheduled_command, args, transcript)
                scheduled_read_ms = (
                    args.scheduled_command_read_ms
                    if args.scheduled_command_read_ms > 0
                    else current_command_read_ms
                )
                read_serial_for(ser, transcript, scheduled_read_ms, output_signal)
        else:
            transcript.add(f"capture_only_ms={args.capture_only_ms}")
            read_serial_for(ser, transcript, args.capture_only_ms, output_signal)
        return_code = 0
    except Exception as exc:
        transcript.add(f"ERROR {exc}")
        return_code = 1
    finally:
        if ser is not None:
            try:
                if ser.is_open:
                    ser.close()
            except Exception as exc:
                transcript.add(f"CLOSE_ERROR {exc}")
        if output_signal is not None:
            output_signal.close()
        transcript.add("serial_closed")
        transcript.write(args.output_path)

    return return_code


if __name__ == "__main__":
    sys.exit(main())
