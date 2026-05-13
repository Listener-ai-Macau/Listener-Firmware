param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [string]$Text = "abc123",
    [int]$BootCaptureSeconds = 8,
    [int]$PostSendCaptureSeconds = 4,
    [int]$HostCaptureTimeoutSeconds = 12,
    [int]$Baud = 115200,
    [switch]$ResetBeforeRead = $true
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "idf_env.ps1")

$python_path = (Get-Command python -ErrorAction Stop).Path
$reset_before_read = if ($ResetBeforeRead.IsPresent) { "True" } else { "False" }
$text_base64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Text))

@"
import base64
import ctypes
from ctypes import wintypes
import queue
import sys
import threading
import time

import serial

port = r"$Port"
text = base64.b64decode("$text_base64").decode("utf-8")
baud = $Baud
boot_capture_seconds = $BootCaptureSeconds
post_send_capture_seconds = $PostSendCaptureSeconds
host_capture_timeout_seconds = $HostCaptureTimeoutSeconds
reset_before_read = $reset_before_read

result_queue = queue.Queue()
capture_state = {
    "captured_text": "",
    "hook_error": "",
    "thread_id": 0,
}

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
                if capture_state["captured_text"].endswith(text):
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
    chunks = []
    status = "ok"
    error_message = ""

    try:
        ser = serial.Serial(port, baud, timeout=0.2)
        try:
            if reset_before_read:
                ser.dtr = False
                ser.rts = True
                time.sleep(0.1)
                ser.rts = False
                time.sleep(0.2)

            boot_deadline = time.time() + boot_capture_seconds
            while time.time() < boot_deadline:
                data = ser.read(4096)
                if data:
                    chunks.append(data)

            ser.write(text.encode("utf-8"))
            ser.flush()

            post_send_deadline = time.time() + post_send_capture_seconds
            while time.time() < post_send_deadline:
                data = ser.read(4096)
                if data:
                    chunks.append(data)
        finally:
            ser.close()
    except Exception as exc:
        status = "error"
        error_message = str(exc)

    result_queue.put({
        "status": status,
        "error": error_message,
        "log_output": b"".join(chunks).decode("utf-8", errors="replace"),
    })
hook_thread = threading.Thread(target=keyboard_hook_worker, daemon=True)
hook_thread.start()

if not hook_ready_event.wait(2.0):
    raise RuntimeError("verify_ble_hid_end_to_end: keyboard hook did not become ready")

if capture_state["hook_error"]:
    raise RuntimeError(f"verify_ble_hid_end_to_end: {capture_state['hook_error']}")

serial_thread = threading.Thread(target=serial_worker, daemon=True)
serial_thread.start()
serial_thread.join(boot_capture_seconds + post_send_capture_seconds + 3)

if serial_thread.is_alive():
    raise RuntimeError("verify_ble_hid_end_to_end: serial worker timed out")

deadline = time.time() + host_capture_timeout_seconds
while time.time() < deadline:
    try:
        worker_result = result_queue.get_nowait()
        break
    except queue.Empty:
        if capture_state["captured_text"].endswith(text):
            break
        time.sleep(0.05)
else:
    worker_result = None

if capture_state["thread_id"] != 0:
    user32.PostThreadMessageW(capture_state["thread_id"], WM_QUIT, 0, 0)

hook_done_event.wait(2.0)

if 'worker_result' not in locals() or worker_result is None:
    try:
        worker_result = result_queue.get_nowait()
    except queue.Empty as exc:
        raise RuntimeError("verify_ble_hid_end_to_end: missing serial worker result") from exc

log_output = worker_result["log_output"]
captured_text = capture_state["captured_text"]

if worker_result["status"] != "ok":
    raise RuntimeError(f"verify_ble_hid_end_to_end: serial worker failed: {worker_result['error']}")

has_start = "ble_hid: START" in log_output
has_input_ready = "ble_hid: USB SERIAL INPUT READY" in log_output
has_script_rx = "ble_hid: SCRIPT RX" in log_output
has_send_done = "hid_keyboard: send_ascii done" in log_output

if not has_start:
    raise RuntimeError("verify_ble_hid_end_to_end: missing boot marker 'ble_hid: START'")

if not has_input_ready:
    raise RuntimeError("verify_ble_hid_end_to_end: missing input ready marker 'ble_hid: USB SERIAL INPUT READY'")

if not has_script_rx:
    raise RuntimeError("verify_ble_hid_end_to_end: missing script input marker 'ble_hid: SCRIPT RX'")

if not has_send_done:
    raise RuntimeError("verify_ble_hid_end_to_end: missing HID completion marker 'hid_keyboard: send_ascii done'")

if captured_text != text:
    raise RuntimeError(
        "verify_ble_hid_end_to_end: host text mismatch. "
        f"expected={text!r} captured={captured_text!r}"
    )

print("verify_ble_hid_end_to_end: host received expected text and firmware logs show HID send completion")
print("CAPTURED_TEXT_START")
print(captured_text)
print("CAPTURED_TEXT_END")
print("LOG_OUTPUT_START")
print(log_output)
print("LOG_OUTPUT_END")
"@ | & $python_path -
