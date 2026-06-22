import ctypes, time
from ctypes import wintypes
kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
OPEN_EXISTING = 3
INVALID_HANDLE_VALUE = wintypes.HANDLE(-1).value
class COMMTIMEOUTS(ctypes.Structure):
    _fields_ = [
        ('ReadIntervalTimeout', wintypes.DWORD),
        ('ReadTotalTimeoutMultiplier', wintypes.DWORD),
        ('ReadTotalTimeoutConstant', wintypes.DWORD),
        ('WriteTotalTimeoutMultiplier', wintypes.DWORD),
        ('WriteTotalTimeoutConstant', wintypes.DWORD),
    ]
path = r'\\.\COM10'
h = kernel32.CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, None, OPEN_EXISTING, 0, None)
if h == INVALID_HANDLE_VALUE:
    raise ctypes.WinError(ctypes.get_last_error())
try:
    timeouts = COMMTIMEOUTS(50, 0, 250, 0, 500)
    ok = kernel32.SetCommTimeouts(h, ctypes.byref(timeouts))
    print('SetCommTimeouts', bool(ok), 'err', ctypes.get_last_error())
    data = b'~POWER:STATUS\n'
    written = wintypes.DWORD(0)
    ok = kernel32.WriteFile(h, data, len(data), ctypes.byref(written), None)
    print('WriteFile', bool(ok), 'written', written.value, 'err', ctypes.get_last_error())
    deadline = time.time() + 3.0
    chunks = []
    while time.time() < deadline:
        buf = ctypes.create_string_buffer(4096)
        read = wintypes.DWORD(0)
        ok = kernel32.ReadFile(h, buf, 4096, ctypes.byref(read), None)
        if not ok:
            print('ReadFile', bool(ok), 'err', ctypes.get_last_error())
            break
        if read.value:
            chunks.append(buf.raw[:read.value])
    print(b''.join(chunks).decode('utf-8', errors='replace'))
finally:
    kernel32.CloseHandle(h)
