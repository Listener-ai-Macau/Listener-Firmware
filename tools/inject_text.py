"""Host-side text injection tool for voice-keyboard-firmware.

Injects text directly into the current foreground window using Windows API.
The device (ESP32 BLE HID keyboard) is NOT involved — this runs entirely on the host.

Modes:
  --mode type   ASCII typing via pyautogui (character by character)
  --mode paste  Clipboard paste: copies text to clipboard, then sends Ctrl+V
  (default)     Auto-detect: ASCII → type, non-ASCII → paste

Usage:
  python tools/inject_text.py --mode type --text "hello"
  python tools/inject_text.py --mode paste --text "你好世界"
  python tools/inject_text.py --text "hello 你好"
"""

import argparse
import sys
import time

try:
    import pyautogui
except ImportError:
    print("ERROR: pyautogui not installed. Run: pip install pyautogui", file=sys.stderr)
    sys.exit(1)


def has_non_ascii(text):
    return any(ord(c) > 127 for c in text)


def inject_type(text, interval):
    pyautogui.write(text, interval=interval)
    print(f"Typed {len(text)} characters")


def inject_paste(text):
    pyautogui.hotkey("ctrl", "v")
    print(f"Pasted text ({len(text)} chars)")


def copy_to_clipboard(text):
    import subprocess
    subprocess.run(
        ["pwsh", "-NoProfile", "-Command", "Set-Clipboard -Value $args[0]", text],
        check=True, capture_output=True, timeout=5,
    )


def main():
    parser = argparse.ArgumentParser(description="Host-side text injection (Windows API)")
    parser.add_argument("--mode", choices=["type", "paste"],
                        help="type: ASCII typing, paste: clipboard Ctrl+V (auto-detect if omitted)")
    parser.add_argument("--text", required=True, help="Text to inject")
    parser.add_argument("--delay", type=float, default=0.5,
                        help="Delay before injection in seconds (default: 0.5)")
    parser.add_argument("--interval", type=float, default=0.02,
                        help="Typing interval in seconds for --mode type (default: 0.02)")
    args = parser.parse_args()

    mode = args.mode
    if mode is None:
        mode = "paste" if has_non_ascii(args.text) else "type"

    print(f"Mode: {mode} | Delay: {args.delay}s | Text: {args.text!r}")

    if args.delay > 0:
        print(f"Waiting {args.delay}s — switch to target window...")
        time.sleep(args.delay)

    if mode == "paste":
        copy_to_clipboard(args.text)
        inject_paste(args.text)
    else:
        inject_type(args.text, args.interval)


if __name__ == "__main__":
    main()
