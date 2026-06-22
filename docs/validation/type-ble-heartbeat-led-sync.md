# Type BLE Heartbeat Status LED Sync

## Scope

The firmware no longer treats a generic BLE notify subscription as
`STATUS_LED_BLE_TYPE_READY`. Listener-Type must prove that its background
desktop listener is alive by writing heartbeat commands over the existing BLE
audio control characteristic.

## Contract

- `TYPE:READY` and `TYPE:HB` mark the Type heartbeat active.
- `TYPE:BYE` and `TYPE:STOP` mark the Type heartbeat inactive.
- A missing heartbeat for 12 seconds demotes the BLE status LED from
  `type_ready` to generic connected.
- Heartbeat commands are consumed before voice command dispatch and before
  user-activity accounting, so they do not keep the device awake.
- Status LED DMA remains enabled only for the status strip on current hardware:
  `rmt_tx_dma_requested=status:1,ec11:0,key:0,edge:0`.

## Validation

- `python .\tools\verify_status_led_static.py`: PASS
- `pwsh -NoProfile -File .\tools\verify_ble_status_led_connected_sync.ps1`: PASS
- `python .\tools\verify_power_manager_static.py`: PASS
- `pwsh -NoProfile -File .\tools\build.ps1`: PASS, binary size `0xd3fb0`
- `aiw with-lock ... .\tools\flash.ps1 -Port COM10 -NoBuild`: PASS
- `aiw with-lock ... .\tools\verify_status_led_log_no_all_on.ps1 -Port COM10`: PASS

Latest capture:

- Summary:
  `.cache\validation\status-led-log-no-all-on-20260622-204848\summary.json`
- Serial log:
  `.cache\validation\status-led-log-no-all-on-20260622-204848\serial-status-led.txt`

Key observed lines:

- `ble_audio_stream: type heartbeat active reason=TYPE:READY`
- `ble_audio_stream: type heartbeat received source=ble_audio_control command=TYPE:READY`
- `~LED:STATUS detail=state ble=type_ready rec_active=0 ...`
- `~LED:STATUS detail=rgb status_rgb=PWR:18,18,18;BLE:0,0,32;REC:0,0,0;AI:0,0,0;OK:0,0,0;WARN:0,0,0`

