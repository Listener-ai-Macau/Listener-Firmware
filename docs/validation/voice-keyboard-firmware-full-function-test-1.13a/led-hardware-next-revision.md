# LED Hardware Next Revision Notes

Date: 2026-06-19
Agent: oai1
Scope: next hardware revision recommendations after V2 status-strip flicker root cause isolation.

## Current Finding

The current ESP32-S3 + ESP-IDF RMT TX path can reliably use DMA for the status strip only. The local ESP-IDF driver source scans only the final TX channel when `with_dma` is requested, and hardware validation in this branch matched that behavior: forcing all four LED zones to request RMT DMA left only `status:dma1` while EC11, key, and edge were unavailable. Firmware therefore keeps status on RMT TX DMA and leaves EC11/key/edge on ordinary RMT.

That fixes the worst status-tail symptom, but it does not fully remove visible instability on EC11/key/edge because those zones still use interrupt-backed RMT, separate data outputs, and their own power/signal paths. Their remaining flicker risk is lower than the status tail because it is not corrupting semantic LED5/6 state, but it is still a real hardware-quality issue.

## Recommended Hardware Architecture

1. Preferred: route all addressable product LEDs that need coordinated effects as one physical WS2812-compatible data chain from one DMA-capable output. Firmware can keep the same logical zones by segment offsets: status, EC11, key, and edge become ranges in one chain. This gives one timing source, one latch boundary, one DMA transaction, and no multi-channel skew.

2. If mechanical routing needs separate harnesses, add 0 ohm link options that can either daisy-chain zones into one data path or split them for bring-up. Default production should be the single-chain option unless EMC/mechanical testing proves otherwise.

3. If independent physical data lines must remain, do not depend on four ESP32-S3 RMT DMA outputs. Use either a different LED backend with DMA resources for the required channel count, a dedicated LED controller/co-processor, or non-WS2812 LED driver ICs for the non-status surfaces. The key requirement is that the selected architecture must provide deterministic refresh for every animated zone while BLE/Wi-Fi/audio interrupts are active.

4. Keep status semantics physically conservative even on the next revision: LED5/OK and LED6/WARN should not be used as decorative spill or chaser pixels. They are semantic outputs, and the firmware should still clear them explicitly when inactive.

## Signal Integrity Checklist

- Add a 3.3 V to 5 V logic buffer for every WS2812 DIN path, placed near the MCU or source connector. Use a fast CMOS buffer family suitable for 5 V output from 3.3 V input, such as AHCT/HCT-style devices, and avoid slow MOSFET level-shifter topologies for 800 kHz one-wire LED data.
- Add a per-zone series data resistor footprint close to the data source or connector. Populate by test; reserve practical values such as 33, 100, 220, and 330 ohm. The footprint matters more than picking one value blindly before layout validation.
- Route LED data with a continuous ground reference. Do not run data as a long isolated trace beside LED power without a nearby return path.
- Avoid stubs. If zones can be chained or split by 0 ohm links, make the production path a single continuous route, not a T split.
- Keep LED data away from USB D+/D-, microphone clock/data, antenna keep-out, and high-current LED power loops.
- Add test pads for each LED DIN source, first-pixel DIN, VLED, and local ground so timing and ringing can be checked with an oscilloscope without probing LED pads.

## Power Integrity Checklist

- Size VLED for worst-case current and transient load, even if firmware normally caps brightness. Firmware brightness caps are not a substitute for power integrity.
- Use a separate LED power branch or plane segment with a deliberate return path back to the regulator, not through sensitive audio, USB, or MCU ground paths.
- Add bulk capacitance near each LED zone connector or zone entry. Leave footprint options so the value can be tuned after measurement.
- Add local 0.1 uF decoupling near individual addressable LEDs or LED clusters where the LED package/board does not already provide it.
- Keep VLED and GND wide through high-current sections, especially EC11 and edge/frame where multiple pixels animate together.
- Consider an LED power enable/load switch with controlled slew if plug-in or wake transitions create visible LED glitches.

## Firmware Implications

- Current revision: status strip stays on RMT TX DMA; EC11/key/edge stay ordinary RMT with low brightness, low refresh pressure, and dirty-strip suppression.
- Next revision single-chain path: firmware should add a backend that packs all zones into one physical frame and transmits it with one DMA operation. Existing semantic zone arrays can remain; only the physical backend mapping changes.
- Next revision multi-controller path: firmware should expose per-zone backend type in `~LED:STATUS` so manufacturing can tell whether each zone is RMT DMA, SPI DMA, external controller, or fallback RMT.

## References Used

- Espressif RMT documentation notes that `with_dma` offloads channel work but is hardware-dependent: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/rmt.html
- Espressif FAQ describes RMT LED exceptions under Wi-Fi/Bluetooth interrupt pressure and mentions SPI DMA as an alternative: https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/rmt.html
- Espressif led_strip documentation includes an SPI backend with `.with_dma = true`: https://espressif.github.io/idf-extra-components/latest/led_strip/index.html
- WS2812B timing and voltage requirements: https://cdn-shop.adafruit.com/datasheets/WS2812B.pdf
