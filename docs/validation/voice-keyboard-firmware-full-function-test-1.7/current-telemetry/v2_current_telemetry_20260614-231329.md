# V2 Current Telemetry Report

Captured at: 2026-06-14T23:13:45.4419491+08:00
Source: COM7 (serial)

Policy: telemetry only. Firmware does not use these readings for power control, shutdown, LED limiting, or user-visible power decisions.

| Branch | Net | Allowed GPIOs | Reported GPIO | Hardware present | Raw ADC | ADC mV | ADC calibrated | Input current mA valid | Battery mV | Input power mW valid | Result |
|---|---|---|---:|---|---:|---:|---|---|---:|---|---|
| TPS63020_input_branch | TPS63020_I_ADC | -1/10 | 10 | True | 0 | 0 | True | True | 3916 | True | ESP_OK |
| SY7088_input_branch | SY7088_I_ADC | -1/9 | 9 | True | 11 | 10 | True | True | 3944 | True | ESP_OK |

All expected branches reported: True
Hardware sensors populated: 2/2

## Power Status
- Hardware shutdown ms: 1800000
- Shutdown blockers: 0x00000080
- Auto shutdown blocked by external power: False
- PWR_HOLD GPIO: 11
- PWR_HOLD level: low
- Last shutdown reason: manual_command
- Current battery mV: 3930
