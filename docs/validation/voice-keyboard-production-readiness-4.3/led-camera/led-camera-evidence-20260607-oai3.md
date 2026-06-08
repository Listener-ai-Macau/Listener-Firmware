# LED camera evidence for production-readiness 4.3

Date: 2026-06-07
Agent: oai3

## Scope

This was a user-requested visual LED check while 4.3 remained hardware-waiting. No production LED effect code was changed. The test used the existing firmware command surface:

- `~LED:PROFILE factory`
- `~LED:TEST:RGBW all`
- `~LED:STATUS`
- `~LED:BUDGET`
- `~BOARD:POWER`
- `~BOARD:STATUS`
- `~POWER:STATUS`

`factory` is the highest current firmware validation profile. It is still safety-gated in source at 35% per-channel cap and 220 mA profile budget; this is not an unrestricted 100% full-white mode.

## Evidence

- Lock/camera run: `with-lock-led-camera-20260607-oai3.log`
- Serial TX/camera script log: `led-factory-rgbw-serial-20260607-222433.log`
- Camera capture log: `led-factory-rgbw-camera-20260607-222433.log`
- Camera image: `led-factory-rgbw-camera0-20260607-222433.jpg`
- Baseline image before the factory/RGBW run: `camera0-probe-20260607-oai3.jpg`
- Brightness comparison: `led-camera-comparison-20260607-oai3.log`
- Follow-up serial capture with repository send/capture scripts: `with-lock-led-status-capture-20260607-oai3.log`
- Follow-up flash retry: `..\with-lock-flash-retry-20260607-oai3.log`

## Result

Visual result: PASS for camera-observed LED output. The factory/RGBW command window visibly lit the four key LEDs and lower strip in the camera frame.

Image statistics:

- Baseline: mean 114.16, p99 217.00, `bright_ge_220=2880`
- Factory/RGBW: mean 132.18, p99 255.00, `bright_ge_220=17625`

The high-bright pixel count increased by about 6.1x compared with the baseline probe frame.

## Remaining Hardware Issue

The device accepted host-to-device LED control well enough to visibly change the LEDs, but serial diagnostics did not return RX text in either the custom camera probe script or the repository `send_serial.ps1`/`capture_serial.ps1` retry. A flash retry also failed after opening COM6:

`Failed to connect to ESP32-S3: No serial data received.`

This keeps the original 4.3 hardware gate open for flash, physical-key audio capture, and physical custom-key HID evidence. The current blocker is no longer "COM6 cannot be opened"; it is "COM6 opens, but ESP32-S3 bootloader/diagnostic TX-RX is not responsive enough for flash or serial evidence."
