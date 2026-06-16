# PWR_HOLD New Board Hardware Shutdown Evidence

- Result: FAIL
- Port: `COM9`
- PWR_HOLD high request observed: True
- PWR_HOLD high readback observed: False
- Serial port disappeared after shutdown: False
- Serial port reappeared after hardware power key: True
- Runtime-low restore after failure observed: False
- Readback mismatch observed: True

Artifacts:

- `shutdown_serial_transcript.txt`
- `port_poll_after_shutdown.txt`
- `shutdown_summary.json`
