# Custom key command/HID contract validation

status=PASS
scope=static source contract plus physical verifier expectations

## Evidence
- PASS: KEY1 board pin GPIO38
- PASS: KEY1 custom-key struct maps GPIO38 to F13/F17/F21
- PASS: KEY1 fallback queue log format
- PASS: KEY1 BLE HID usage value F13
- PASS: KEY1 Windows host VK capture for F13
- PASS: KEY1 physical verifier serial fallback line
- PASS: KEY2 board pin GPIO39
- PASS: KEY2 custom-key struct maps GPIO39 to F14/F18/F22
- PASS: KEY2 fallback queue log format
- PASS: KEY2 BLE HID usage value F14
- PASS: KEY2 Windows host VK capture for F14
- PASS: KEY2 physical verifier serial fallback line
- PASS: KEY3 board pin GPIO40
- PASS: KEY3 custom-key struct maps GPIO40 to F15/F19/F23
- PASS: KEY3 fallback queue log format
- PASS: KEY3 BLE HID usage value F15
- PASS: KEY3 Windows host VK capture for F15
- PASS: KEY3 physical verifier serial fallback line
- PASS: KEY4 board pin GPIO41
- PASS: KEY4 custom-key struct maps GPIO41 to F16/F20/F24
- PASS: KEY4 fallback queue log format
- PASS: KEY4 BLE HID usage value F16
- PASS: KEY4 Windows host VK capture for F16
- PASS: KEY4 physical verifier serial fallback line
- PASS: custom key path sends HID usages instead of text
- PASS: custom key diagnostic event logging
- PASS: custom key debounce tuned for physical buttons
- PASS: BLE HID usage queue dispatch
- PASS: feature map documents F13-F16 fallback
- PASS: feature map documents desktop custom action contract
- PASS: WASD verifier is marked legacy
- PASS: no active custom-key WASD or text fallback
- PASS: no active feature map WASD fallback wording
