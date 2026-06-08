# 4020 LED Pinout Audit

Date: 2026-06-08
Agent: oai1
Scope: EC11 ring LEDs `LED7..LED10`, `LED15..LED16`, `LED23..LED28`; edge/frame LEDs `LED17..LED22`

## Result

Status/key LEDs are firmware-controllable on the current board, but the EC11 ring and edge/frame 4020 LEDs still do not light after the firmware was changed to 4020-compatible WS2812 timing. The strongest remaining explanation is a hardware symbol/footprint pin mapping mismatch for the 4020 part, not a business-logic or RMT timing issue.

## Evidence

- Firmware v4 uses 24-bit GRB output for all strips, RMT at 10 MHz, 4020-compatible timings, and a 300 us reset latch.
- `~LED:STATUS` on the flashed board reported `led_contract_rev=status_key_full_brightness_chase_pixel_4020timing_v4`, `reset_us=300`, and `timing=ws2812_4020_compatible`.
- Manual visual testing confirmed status/key 3528 LEDs respond, including full-brightness red/green/blue/white and chase output.
- Manual visual testing reported no light from EC11 and edge/frame 4020 LEDs when driven with full-strip and single-pixel commands.
- `docs/validation/voice-keyboard-camera-status-key-led-tuning-1.2/datasheet-renders/WS2812B-4020-V6-p2.png` shows the 4020 datasheet pin function table:
  - `1=DIN`
  - `2=VDD`
  - `3=DOUT`
  - `4=VSS`
- `docs/validation/voice-keyboard-camera-status-key-led-tuning-1.2/datasheet-renders/Voice-Keyboard-V2-p1-x3.png` shows the V2 Function schematic 4020 symbols for LED7 and LED17 as:
  - `1=VD`
  - `2=OUT`
  - `3=VS`
  - `4=IN`
- Direct `Function.SchDoc` record extraction confirms LED1, LED7, LED11, LED17, LED22, and LED28 all use the same schematic symbol pin names: `1=VD`, `2=OUT`, `3=VS`, `4=IN`.
- The same extraction shows the 3528 current footprint has explicit Altium pin-map records, while the 4020 current footprint does not show equivalent pin-map records. That means the 4020 case depends on the PCB footprint pad numbers already matching the schematic symbol's non-datasheet pin naming.

## Interpretation

If the `LED 4020-WS2812` footprint pad numbers follow the 4020 datasheet, the V2 schematic mapping would connect the first 4020 LED as:

- physical `DIN` receives `VDD_LED`
- physical `VDD` receives the schematic `OUT` cascade net
- physical `DOUT` receives `VSS`
- physical `VSS` receives the MCU data line

That wiring would prevent the 4020 LED from powering and receiving data correctly. Firmware cannot repair that class of failure because the MCU is not connected to the physical DIN pad and the LED power pins are not on the expected rails.

The remaining uncertainty is whether the embedded `LED 4020-WS2812` footprint deliberately renumbered pads to match the schematic symbol instead of the vendor datasheet. The repository does not include a standalone `LED.PcbLib`, so the quickest way to close this is continuity measurement on the assembled board or direct inspection in Altium.

## Hardware Checks

Check the first LED in each failed chain before doing more firmware timing work:

- `LED7` EC11 ring:
  - physical 4020 pin 1 / DIN should connect to `PWM_RGB_EC11` / GPIO5
  - physical 4020 pin 2 / VDD should connect to `VDD_LED`
  - physical 4020 pin 3 / DOUT should connect to the next LED DIN
  - physical 4020 pin 4 / VSS should connect to ground
- `LED17` edge/frame:
  - physical 4020 pin 1 / DIN should connect to `PWM_RGB_Edge` / GPIO4
  - physical 4020 pin 2 / VDD should connect to `VDD_LED`
  - physical 4020 pin 3 / DOUT should connect to `LED18` DIN
  - physical 4020 pin 4 / VSS should connect to ground

If either first LED has physical pin 1 tied to `VDD_LED` or physical pin 4 tied to the MCU data net, the board requires schematic/footprint rework or wire-level hardware rework; firmware should not continue changing color order or WS2812 timing for this symptom.
