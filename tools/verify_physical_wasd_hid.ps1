param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [string]$ExpectedText = "was",
    [int]$TimeoutSeconds = 45,
    [int]$Baud = 115200,
    [string]$OutputPath = "",
    [switch]$ResetBeforeRead
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "idf_env.ps1")

$python_path = (Get-Command python -ErrorAction Stop).Path
$reset_before_read = if ($ResetBeforeRead.IsPresent) { "True" } else { "False" }
$expected_base64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($ExpectedText))
$output_base64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($OutputPath))

@"
import base64
import ctypes
from ctypes import wintypes
import pathlib
import sys
import threading
import time

import serial

port = r"$Port"
expected_text = base64.b64decode("$expected_base64").decode("utf-8")
timeout_seconds = $TimeoutSeconds
baud = $Baud
output_path_text = base64.b64decode("$output_base64").decode("utf-8")
reset_before_read = $reset_before_read

capture_state = {
    "captured_text": "",
    "hook_error": "",
    "thread_id": 0,
}

serial_chunks = []
serial_error = ""
serial_done = threading.Event()

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

WH_KEYBOARD_LL = 13
HC_ACTION = 0
WM_KEYDOWN = 0x0100
WM_SYSKEYDOWN = 0x0104
WM_QUIT = 0x0012
VK_SHIFT = 0x10
VK_SPACE = 0x20
VK_RETURN = 0x0D
VK_TAB = 0x09
VK_BACK = 0x08

class KBDLLHOOKSTRUCT(ctypes.Structure):
    _fields_ = [
        ("vkCode", wintypes.DWORD),
        ("scanCode", wintypes.DWORD),
        ("flags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong)),
    ]

class MSG(ctypes.Structure):
    _fields_ = [
        ("hwnd", wintypes.HWND),
        ("message", wintypes.UINT),
        ("wParam", wintypes.WPARAM),
        ("lParam", wintypes.LPARAM),
        ("time", wintypes.DWORD),
        ("pt_x", ctypes.c_long),
        ("pt_y", ctypes.c_long),
        ("lPrivate", wintypes.DWORD),
    ]

HOOKPROC = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_int, wintypes.WPARAM, wintypes.LPARAM)

hook_ready_event = threading.Event()
hook_done_event = threading.Event()
stop_event = threading.Event()

user32.SetWindowsHookExW.argtypes = [ctypes.c_int, HOOKPROC, wintypes.HINSTANCE, wintypes.DWORD]
user32.SetWindowsHookExW.restype = wintypes.HANDLE
user32.CallNextHookEx.argtypes = [wintypes.HANDLE, ctypes.c_int, wintypes.WPARAM, wintypes.LPARAM]
user32.CallNextHookEx.restype = ctypes.c_long
user32.UnhookWindowsHookEx.argtypes = [wintypes.HANDLE]
user32.UnhookWindowsHookEx.restype = wintypes.BOOL
user32.GetMessageW.argtypes = [ctypes.POINTER(MSG), wintypes.HWND, wintypes.UINT, wintypes.UINT]
user32.GetMessageW.restype = wintypes.BOOL
user32.TranslateMessage.argtypes = [ctypes.POINTER(MSG)]
user32.TranslateMessage.restype = wintypes.BOOL
user32.DispatchMessageW.argtypes = [ctypes.POINTER(MSG)]
user32.DispatchMessageW.restype = wintypes.LPARAM
user32.PostThreadMessageW.argtypes = [wintypes.DWORD, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.PostThreadMessageW.restype = wintypes.BOOL
user32.GetAsyncKeyState.argtypes = [ctypes.c_int]
user32.GetAsyncKeyState.restype = ctypes.c_short
kernel32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]
kernel32.GetModuleHandleW.restype = wintypes.HINSTANCE
kernel32.GetCurrentThreadId.restype = wintypes.DWORD

def vk_to_char(vk_code):
    shift_pressed = bool(user32.GetAsyncKeyState(VK_SHIFT) & 0x8000)

    if 0x41 <= vk_code <= 0x5A:
        ascii_char = chr(vk_code)
        return ascii_char if shift_pressed else ascii_char.lower()

    if 0x30 <= vk_code <= 0x39:
        shifted_digits = {
            0x30: ")",
            0x31: "!",
            0x32: "@",
            0x33: "#",
            0x34: "$",
            0x35: "%",
            0x36: "^",
            0x37: "&",
            0x38: "*",
            0x39: "(",
        }
        return shifted_digits.get(vk_code, "") if shift_pressed else chr(vk_code)

    if vk_code == VK_SPACE:
        return " "
    if vk_code == VK_RETURN:
        return "\n"
    if vk_code == VK_TAB:
        return "\t"
    if vk_code == VK_BACK:
        return "\b"
    return ""

def keyboard_hook_worker():
    capture_state["thread_id"] = kernel32.GetCurrentThreadId()

    @HOOKPROC
    def low_level_keyboard_proc(n_code, w_param, l_param):
        if n_code == HC_ACTION and w_param in (WM_KEYDOWN, WM_SYSKEYDOWN):
            keyboard_struct = ctypes.cast(l_param, ctypes.POINTER(KBDLLHOOKSTRUCT)).contents
            input_char = vk_to_char(keyboard_struct.vkCode)
            if input_char:
                capture_state["captured_text"] += input_char
                if capture_state["captured_text"].endswith(expected_text):
                    stop_event.set()
                    user32.PostQuitMessage(0)

        return user32.CallNextHookEx(None, n_code, w_param, l_param)

    hook_handle = user32.SetWindowsHookExW(
        WH_KEYBOARD_LL,
        low_level_keyboard_proc,
        kernel32.GetModuleHandleW(None),
        0,
    )
    if not hook_handle:
        capture_state["hook_error"] = f"SetWindowsHookExW failed: {ctypes.get_last_error()}"
        hook_ready_event.set()
        hook_done_event.set()
        return

    capture_state["hook_proc"] = low_level_keyboard_proc
    hook_ready_event.set()

    message = MSG()
    while user32.GetMessageW(ctypes.byref(message), None, 0, 0) != 0:
        user32.TranslateMessage(ctypes.byref(message))
        user32.DispatchMessageW(ctypes.byref(message))

    user32.UnhookWindowsHookEx(hook_handle)
    hook_done_event.set()

def serial_worker():
    global serial_error
    try:
        ser = serial.Serial()
        ser.port = port
        ser.baudrate = baud
        ser.timeout = 0.2
        ser.dsrdtr = False
        ser.rtscts = False
        ser.dtr = False
        ser.rts = False
        ser.open()
        try:
            if reset_before_read:
                ser.dtr = False
                ser.rts = True
                time.sleep(0.1)
                ser.rts = False
                time.sleep(0.2)

            deadline = time.time() + timeout_seconds
            while time.time() < deadline and not stop_event.is_set():
                data = ser.read(4096)
                if data:
                    serial_chunks.append(data)

            drain_deadline = time.time() + 2.0
            while time.time() < drain_deadline:
                data = ser.read(4096)
                if data:
                    serial_chunks.append(data)
        finally:
            ser.close()
    except Exception as exc:
        serial_error = str(exc)
    finally:
        serial_done.set()

def write_artifact(text):
    if not output_path_text:
        return
    output_path = pathlib.Path(output_path_text)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(text, encoding="utf-8", newline="\n")

print("verify_physical_wasd_hid: focus a text editor now, then physically press KEY2, KEY3, KEY4.")
print("Expected host text: " + expected_text)
sys.stdout.flush()

hook_thread = threading.Thread(target=keyboard_hook_worker, daemon=True)
hook_thread.start()

if not hook_ready_event.wait(2.0):
    raise RuntimeError("verify_physical_wasd_hid: keyboard hook did not become ready")

if capture_state["hook_error"]:
    raise RuntimeError(f"verify_physical_wasd_hid: {capture_state['hook_error']}")

serial_thread = threading.Thread(target=serial_worker, daemon=True)
serial_thread.start()

deadline = time.time() + timeout_seconds
while time.time() < deadline and not stop_event.is_set():
    time.sleep(0.05)

stop_event.set()
if capture_state["thread_id"] != 0:
    user32.PostThreadMessageW(capture_state["thread_id"], WM_QUIT, 0, 0)

hook_done_event.wait(2.0)
serial_done.wait(5.0)

log_output = b"".join(serial_chunks).decode("utf-8", errors="replace")
captured_text = capture_state["captured_text"]
required_lines = [
    "WASD key press queued: source=key2.gpio48.w output=w",
    "WASD key press queued: source=key3.gpio47.a output=a",
    "WASD key press queued: source=key4.gpio21.s output=s",
]
missing_lines = [line for line in required_lines if line not in log_output]
required_raw_sources = [
    "WASD key raw transition: source=key2.gpio48.w",
    "WASD key raw transition: source=key3.gpio47.a",
    "WASD key raw transition: source=key4.gpio21.s",
]
missing_raw_sources = [line for line in required_raw_sources if line not in log_output]

status = "PASS"
failure_reasons = []
if serial_error:
    status = "FAIL"
    failure_reasons.append("serial_error=" + serial_error)
if not captured_text.endswith(expected_text):
    status = "FAIL"
    failure_reasons.append(f"host_text_mismatch expected_suffix={expected_text!r} captured={captured_text!r}")
if missing_lines:
    status = "FAIL"
    failure_reasons.append("missing_serial_lines=" + repr(missing_lines))
if missing_raw_sources:
    status = "FAIL"
    failure_reasons.append("missing_raw_sources=" + repr(missing_raw_sources))

artifact = []
artifact.append("# Physical key BLE HID validation")
artifact.append("")
artifact.append(f"status={status}")
artifact.append(f"port={port}")
artifact.append(f"expected_host_text={expected_text!r}")
artifact.append(f"captured_host_text={captured_text!r}")
artifact.append(f"serial_error={serial_error!r}")
artifact.append(f"missing_raw_sources={missing_raw_sources!r}")
artifact.append(f"missing_serial_lines={missing_lines!r}")
artifact.append("")
artifact.append("## Required raw GPIO transition sources")
artifact.extend(required_raw_sources)
artifact.append("")
artifact.append("## Required serial lines")
artifact.extend(required_lines)
artifact.append("")
artifact.append("## Serial log")
artifact.append("``````")
artifact.append(log_output)
artifact.append("``````")
artifact_text = "\n".join(artifact) + "\n"
write_artifact(artifact_text)

print("verify_physical_wasd_hid: status=" + status)
print("captured_host_text=" + repr(captured_text))
if output_path_text:
    print("artifact=" + output_path_text)
print("LOG_OUTPUT_START")
print(log_output)
print("LOG_OUTPUT_END")

if status != "PASS":
    raise RuntimeError("verify_physical_wasd_hid: " + "; ".join(failure_reasons))
"@ | & $python_path -
