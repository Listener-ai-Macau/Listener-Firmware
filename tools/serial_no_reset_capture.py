from __future__ import annotations

import argparse
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
    parser.add_argument("--output-path", default="")
    return parser.parse_args()


class Transcript:
    def __init__(self) -> None:
        self.lines: list[str] = []

    def add(self, text: str) -> None:
        print(text, flush=True)
        self.lines.append(text)

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


def read_serial_for(ser, transcript: Transcript, milliseconds: int) -> None:
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
            for command in args.command_list.split(";;")
            if command and command.strip()
        )
    return commands


def main() -> int:
    args = parse_args()
    commands = command_list(args)
    transcript = Transcript()
    if not commands:
        transcript.add("ERROR At least one --command or --command-list entry is required.")
        transcript.write(args.output_path)
        return 2

    try:
        import serial
    except Exception as exc:
        transcript.add(f"ERROR pyserial import failed: {exc}")
        transcript.write(args.output_path)
        return 2

    ser = None
    try:
        ser = open_serial_no_reset(serial, args.port, args.baud, args.write_timeout_ms)
        transcript.add(f"serial_opened port={args.port} baud={args.baud} dtr=0 rts=0 no_reset=1")
        read_serial_for(ser, transcript, args.initial_read_ms)
        for command in commands:
            transcript.add(f"> {command}")
            try:
                ser.reset_input_buffer()
            except Exception as exc:
                transcript.add(f"DISCARD_IN_ERROR {exc}")
            write_serial_command(ser, command, args, transcript)
            read_serial_for(ser, transcript, args.command_read_ms)
            if args.command_delay_ms > 0:
                time.sleep(args.command_delay_ms / 1000.0)
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
        transcript.add("serial_closed")
        transcript.write(args.output_path)

    return return_code


if __name__ == "__main__":
    sys.exit(main())
