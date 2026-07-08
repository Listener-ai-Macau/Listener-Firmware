# Verifies generated KEY1-KEY4 diagnostics produce real Windows F13-F16 HID input.
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$TimeoutSeconds = 35,
    [int]$Baud = 115200,
    [int]$InitialReadMs = 1200,
    [int]$PostSendReadMs = 1800,
    [int]$CommandDelayMs = 120,
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
initial_read_ms = $InitialReadMs
post_send_read_ms = $PostSendReadMs
command_delay_ms = $CommandDelayMs
output_path_text = base64.b64decode("$output_base64").decode("utf-8")
reset_before_read = $reset_before_read

commands = [
    ("KEY1", "key1.gpio38.f13", "F13", "0x68", 0x7C, "~KEY:KEY1:SINGLE"),
    ("KEY2", "key2.gpio39.f14", "F14", "0x69", 0x7D, "~KEY:KEY2:SINGLE"),
    ("KEY3", "key3.gpio40.f15", "F15", "0x6A", 0x7E, "~KEY:KEY3:SINGLE"),
    ("KEY4", "key4.gpio41.f16", "F16", "0x6B", 0x7F, "~KEY:KEY4:SINGLE"),
]
expected_vks = [item[4] for item in commands]
vk_names = {
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
stop_event = threading.Event()

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

def names(values):
    return [vk_names.get(value, f"VK_0x{value:02X}") for value in values]

def has_expected_suffix():
    captured = capture_state["captured_vks"]
    return len(captured) >= len(expected_vks) and captured[-len(expected_vks):] == expected_vks

def keyboard_hook_worker():
    capture_state["thread_id"] = kernel32.GetCurrentThreadId()

    @HOOKPROC
    def low_level_keyboard_proc(n_code, w_param, l_param):
        if n_code == HC_ACTION and w_param in (WM_KEYDOWN, WM_SYSKEYDOWN):
            keyboard_struct = ctypes.cast(l_param, ctypes.POINTER(KBDLLHOOKSTRUCT)).contents
            vk_code = int(keyboard_struct.vkCode)
            if vk_code in vk_names:
                capture_state["captured_vks"].append(vk_code)
                if has_expected_suffix():
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

def read_for(ser, milliseconds):
    deadline = time.time() + max(milliseconds, 0) / 1000.0
    while time.time() < deadline and not stop_event.is_set():
        data = ser.read(4096)
        if data:
            serial_chunks.append(data)
        else:
            time.sleep(0.02)

def serial_worker():
    global serial_error
    try:
        ser = serial.Serial()
        ser.port = port
        ser.baudrate = baud
        ser.timeout = 0.08
        ser.write_timeout = 3.0
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

            read_for(ser, initial_read_ms)
            for _logical, _source, _usage_name, _usage_hex, _vk, command in commands:
                ser.write((command + "\n").encode("utf-8"))
                ser.flush()
                read_for(ser, command_delay_ms)
            read_for(ser, post_send_read_ms)
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

hook_thread = threading.Thread(target=keyboard_hook_worker, daemon=True)
hook_thread.start()

if not hook_ready_event.wait(2.0):
    raise RuntimeError("verify_generated_custom_key_hid_host: keyboard hook did not become ready")
if capture_state["hook_error"]:
    raise RuntimeError("verify_generated_custom_key_hid_host: " + capture_state["hook_error"])

serial_thread = threading.Thread(target=serial_worker, daemon=True)
serial_thread.start()

deadline = time.time() + timeout_seconds
while time.time() < deadline and not stop_event.is_set():
    if serial_done.is_set() and has_expected_suffix():
        break
    time.sleep(0.05)

stop_event.set()
if capture_state["thread_id"] != 0:
    user32.PostThreadMessageW(capture_state["thread_id"], WM_QUIT, 0, 0)

hook_done_event.wait(2.0)
serial_done.wait(5.0)

log_output = b"".join(serial_chunks).decode("utf-8", errors="replace")
captured_vks = capture_state["captured_vks"]
captured_names = names(captured_vks)
failures = []

if serial_error:
    failures.append("serial_error=" + serial_error)
if not has_expected_suffix():
    failures.append(f"host_key_mismatch expected_suffix={names(expected_vks)!r} captured={captured_names!r}")

for logical, source, usage_name, usage_hex, _vk, _command in commands:
    required = [
        f"~KEY:GENERATED logical={logical} gesture=single result=ESP_OK",
        f"custom key raw debounce candidate: logical={logical}",
        f"custom key single pending: logical={logical}",
        f"ble_hid: {source} HID usage queued: usage={usage_hex}",
        f"custom key fallback queued: logical={logical} source={source} usage={usage_name} gesture=single",
        f"hid_keyboard: send_usage done usage={usage_hex}",
    ]
    missing = [token for token in required if token not in log_output]
    if missing:
        failures.append(f"missing_{logical}_serial_evidence={missing!r}")

for forbidden in [
    "usage press failed",
    "usage release failed",
    "immediate HID usage dispatch failed",
    "custom key gesture dropped",
    "WRITE_ERROR",
    "READ_ERROR",
    "ESP_ERR_TIMEOUT",
    "ESP_ERR_INVALID_STATE",
    "ESP_ERR_INVALID_ARG",
]:
    if forbidden in log_output:
        failures.append("forbidden_serial_token=" + forbidden)

status = "PASS" if not failures else "FAIL"
artifact = []
artifact.append("# Generated custom key host HID validation")
artifact.append("")
artifact.append(f"status={status}")
artifact.append(f"port={port}")
artifact.append(f"expected_host_keys={names(expected_vks)!r}")
artifact.append(f"captured_host_keys={captured_names!r}")
artifact.append(f"captured_host_vks={captured_vks!r}")
artifact.append(f"serial_error={serial_error!r}")
artifact.append(f"failures={failures!r}")
artifact.append("")
artifact.append("## Serial log")
artifact.append("``````")
artifact.append(log_output)
artifact.append("``````")
artifact_text = "\n".join(artifact) + "\n"
write_artifact(artifact_text)

print("verify_generated_custom_key_hid_host: status=" + status)
print("expected_host_keys=" + repr(names(expected_vks)))
print("captured_host_keys=" + repr(captured_names))
if output_path_text:
    print("artifact=" + output_path_text)
print("LOG_OUTPUT_START")
print(log_output)
print("LOG_OUTPUT_END")

if failures:
    raise RuntimeError("verify_generated_custom_key_hid_host: " + "; ".join(failures))
"@ | & $python_path -
