# Verifies logical KEY1-KEY4 safe fallback HID usages F13-F16 on Windows.
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$TimeoutSeconds = 45,
    [int]$Baud = 115200,
    [string]$OutputPath = "",
    [switch]$ResetBeforeRead
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "idf_env.ps1")

$python_path = (Get-Command python -ErrorAction Stop).Path
$reset_before_read = if ($ResetBeforeRead.IsPresent) { "True" } else { "False" }
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
timeout_seconds = $TimeoutSeconds
baud = $Baud
output_path_text = base64.b64decode("$output_base64").decode("utf-8")
reset_before_read = $reset_before_read

expected_vk_sequence = [0x7C, 0x7D, 0x7E, 0x7F]
expected_vk_names = {
    0x7C: "F13",
    0x7D: "F14",
    0x7E: "F15",
    0x7F: "F16",
}

capture_state = {
    "captured_vks": [],
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
kernel32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]
kernel32.GetModuleHandleW.restype = wintypes.HINSTANCE
kernel32.GetCurrentThreadId.restype = wintypes.DWORD

def captured_expected_suffix():
    captured = capture_state["captured_vks"]
    return len(captured) >= len(expected_vk_sequence) and captured[-len(expected_vk_sequence):] == expected_vk_sequence

def keyboard_hook_worker():
    capture_state["thread_id"] = kernel32.GetCurrentThreadId()

    @HOOKPROC
    def low_level_keyboard_proc(n_code, w_param, l_param):
        if n_code == HC_ACTION and w_param in (WM_KEYDOWN, WM_SYSKEYDOWN):
            keyboard_struct = ctypes.cast(l_param, ctypes.POINTER(KBDLLHOOKSTRUCT)).contents
            vk_code = int(keyboard_struct.vkCode)
            if vk_code in expected_vk_names:
                capture_state["captured_vks"].append(vk_code)
                if captured_expected_suffix():
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

def vk_names(values):
    return [expected_vk_names.get(value, f"VK_0x{value:02X}") for value in values]

print("verify_physical_custom_key_hid: focus any window, then physically press logical KEY1, KEY2, KEY3, KEY4.")
print("Expected host keys: F13, F14, F15, F16")
sys.stdout.flush()

hook_thread = threading.Thread(target=keyboard_hook_worker, daemon=True)
hook_thread.start()

if not hook_ready_event.wait(2.0):
    raise RuntimeError("verify_physical_custom_key_hid: keyboard hook did not become ready")

if capture_state["hook_error"]:
    raise RuntimeError(f"verify_physical_custom_key_hid: {capture_state['hook_error']}")

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
captured_vks = capture_state["captured_vks"]
required_lines = [
    "custom key fallback queued: logical=KEY1 source=key1.gpio45.f13 usage=F13",
    "custom key fallback queued: logical=KEY2 source=key2.gpio48.f14 usage=F14",
    "custom key fallback queued: logical=KEY3 source=key3.gpio47.f15 usage=F15",
    "custom key fallback queued: logical=KEY4 source=key4.gpio21.f16 usage=F16",
]
missing_lines = [line for line in required_lines if line not in log_output]
required_raw_sources = [
    "custom key raw transition: logical=KEY1 source=key1.gpio45.f13",
    "custom key raw transition: logical=KEY2 source=key2.gpio48.f14",
    "custom key raw transition: logical=KEY3 source=key3.gpio47.f15",
    "custom key raw transition: logical=KEY4 source=key4.gpio21.f16",
]
missing_raw_sources = [line for line in required_raw_sources if line not in log_output]

status = "PASS"
failure_reasons = []
if serial_error:
    status = "FAIL"
    failure_reasons.append("serial_error=" + serial_error)
if not captured_expected_suffix():
    status = "FAIL"
    failure_reasons.append(f"host_key_mismatch expected_suffix={vk_names(expected_vk_sequence)!r} captured={vk_names(captured_vks)!r}")
if missing_lines:
    status = "FAIL"
    failure_reasons.append("missing_serial_lines=" + repr(missing_lines))
if missing_raw_sources:
    status = "FAIL"
    failure_reasons.append("missing_raw_sources=" + repr(missing_raw_sources))

artifact = []
artifact.append("# Physical custom key BLE HID validation")
artifact.append("")
artifact.append(f"status={status}")
artifact.append(f"port={port}")
artifact.append(f"expected_host_keys={vk_names(expected_vk_sequence)!r}")
artifact.append(f"captured_host_keys={vk_names(captured_vks)!r}")
artifact.append(f"captured_host_vks={captured_vks!r}")
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

print("verify_physical_custom_key_hid: status=" + status)
print("captured_host_keys=" + repr(vk_names(captured_vks)))
if output_path_text:
    print("artifact=" + output_path_text)
print("LOG_OUTPUT_START")
print(log_output)
print("LOG_OUTPUT_END")

if status != "PASS":
    raise RuntimeError("verify_physical_custom_key_hid: " + "; ".join(failure_reasons))
"@ | & $python_path -
