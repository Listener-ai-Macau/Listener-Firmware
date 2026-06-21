import os, time
path = r"\\.\COM10"
fd = os.open(path, os.O_RDWR | os.O_BINARY)
try:
    os.write(fd, b"~POWER:STATUS\n")
    time.sleep(1.0)
    try:
        data = os.read(fd, 8192)
    except OSError as exc:
        print(f"READ_ERROR {exc!r}")
        data = b""
    print(data.decode('utf-8', errors='replace'))
finally:
    os.close(fd)
