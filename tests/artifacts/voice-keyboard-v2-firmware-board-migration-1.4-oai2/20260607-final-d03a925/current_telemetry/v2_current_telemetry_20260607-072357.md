# V2 Current Telemetry Report

Captured at: 2026-06-07T07:24:07.0502474+08:00
Source: COM6 (serial)

Policy: telemetry only. Firmware does not use these readings for power control, shutdown, LED limiting, or user-visible power decisions.

| Branch | Net | GPIO | Present | Raw ADC | ADC mV | ADC calibrated | Input current mA valid | Battery mV | Input power mW valid | Result |
|---|---|---:|---|---:|---:|---|---|---:|---|---|
| TPS63020_input_branch | TPS63020_I_ADC | 10 | True | 37 | 34 | True | True | 4090 | True | ESP_OK |
| SY7088_input_branch | SY7088_I_ADC | 9 | True | 0 | 0 | True | True | 4092 | True | ESP_OK |

All expected sensors present: True

## Power Status
- Hardware shutdown ms: 1800000
- Shutdown blockers: 0x00000080
- Auto shutdown blocked by external power: False
- PWR_HOLD GPIO: 46
- PWR_HOLD level: low
- Last shutdown reason: none
- Current battery mV: 4100
