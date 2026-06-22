# voice-keyboard-status-led/1.2 hardware LED validation

- Result: `PASS`
- Port: `COM10`
- BLE resource: `not-provided`
- Serial log: `status-led-serial.log`
- Manifest: `status-led-visual-manifest.json`
- Captured images: `43`
- Required serial tokens missing: `0`

The locked run exercises `~LED:STATUS`, `~LED:BUDGET`, `~LED:PRIVACY`, `~BOARD:STATUS`,
`~LED:TEST:RGBW all`, `~LED:TEST:MAP status/ec11/key/edge`, major `~LED:PREVIEW` semantic
states, `~LED:ERROR ota hard`, and `~LED:OFF`.

Representative images:

- `rgbw-red-phase`: `rgbw-red-phase.jpg`
- `rgbw-green-phase`: `rgbw-green-phase.jpg`
- `rgbw-blue-phase`: `rgbw-blue-phase.jpg`
- `rgbw-white-phase`: `rgbw-white-phase.jpg`
- `map-status-01`: `map-status-01.jpg`
- `map-ec11-01`: `map-ec11-01.jpg`
- `map-key-01`: `map-key-01.jpg`
- `map-edge-01`: `map-edge-01.jpg`
- `preview-ready`: `preview-ready.jpg`
- `preview-rec_not_available`: `preview-rec_not_available.jpg`
- `preview-off`: `preview-off.jpg`
