# PWR_HOLD New Board Hardware Shutdown Evidence

- Result: PASS
- Port: `COM10`
- Shutdown command accepted: True
- PWR_HOLD high request observed: True
- PWR_HOLD high readback observed: False
- Serial port disappeared after shutdown: True
- Serial port reappeared after hardware power key: True
- Cold-boot status captured: True
- Post-restore POWER status captured: True
- Post-restore BOARD status captured: True
- Post-restore probe error:
- Runtime-low restore after failure observed: False
- Readback mismatch observed: False

Artifacts:

- `shutdown_serial_transcript.txt`
- `port_poll_after_shutdown.txt`
- `shutdown_summary.json`
