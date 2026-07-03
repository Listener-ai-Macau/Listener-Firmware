# Status LED DMA & Flicker Tuning History — READ FIRST

> This is the consolidated index of the historical LED DMA / flicker tuning experience.
> Anyone touching the LED driver, the per-strip DMA selection, or adding a new LED
> peripheral **must read this first**. The current "status-only DMA" design is the
> result of empirical tests on this exact board, not a default to be silently changed.

Related: [status_led.md](status_led.md) (feature contract). The stability fix
evidence was captured during private hardware validation and should be preserved
in release notes or PR evidence rather than committed as raw validation logs.

## One-sentence root cause

ESP32-S3 has **only ONE RMT TX channel that can use DMA** (a silicon / ESP-IDF driver
limit). The other 3 RMT TX channels are RAM-backed (48 symbols each) and glitch under
BLE / Wi-Fi / audio interrupt pressure. **That is why only the status strip is on DMA.**

## Key facts (all empirically verified on V2 N16R8)

1. **All-strips-RMT-DMA was tried and FAILED.** Forcing `prefer_dma=true` on all 4
   strips produced only `status:dma1`; EC11 / key / edge came up **unavailable**
   (ESP-IDF RMT TX DMA is not usable on four simultaneous channels on S3). Flipping
   `prefer_dma` on the other 3 strips does **not** give them DMA — the backend's
   `dma_fallback` drops them back to non-DMA, i.e. today's behavior. See
   `all-strip-dma-status-20260619.log`.
2. **DMA is mandatory for the STATUS strip's REC/AI no-flicker baseline.** Non-DMA RMT
   under BLE/audio interrupt pressure made `LED5=OK` / `LED6=WARN` mirror
   `LED3=REC` / `LED4=AI`. Status-only DMA fixed it — human review recorded
   `Irregular flicker reports: 0` and `LED5/6 follow reports: 0`.
3. **DMA can NOT stay enabled through low-power idle / suspend** ("不能一直 DMA").
   The DMA path corrupts the WS2812 tail during low-power reconfiguration and leaves
   physical LED3-LED6 latched on. The accepted pattern is therefore
   **active effects = DMA → low-power clear/final latch = non-DMA → suspend + drive data GPIO low**.
   Low-power entry/resume clear frames, prepare-sleep all-off frames, and shutdown-final
   frames force every strip off its active DMA transport. SPI-backed EC11/key strips
   use a one-shot non-DMA RMT latch for those frames, then restore the SPI transport
   choice for the next active frame.
4. **EC11 / key / edge DO flicker on non-DMA RMT — this is documented, not hypothetical.**
   Historical hardware review recorded that the status-strip DMA fix does not fully
   remove visible instability on EC11/key/edge because those zones still use
   interrupt-backed RMT, separate data outputs, and their own power/signal paths.
   The recommended fix for non-status strips is a different LED backend with DMA
   resources for the required channel count (= SPI DMA), precisely because four
   ESP32-S3 RMT-DMA outputs are not available. The operator confirmed this flicker
   on hardware; the status strip does not flicker because it is on DMA.

## Flicker-free DMA output budget on this board (mic stays on I2S0)

| Peripheral | DMA LED outputs | Status on this board |
|---|---|---|
| RMT | **1** of 4 TX channels | used by status |
| SPI2 (GPSPI2) | 1 | **FREE** |
| SPI3 (GPSPI3) | 1 | **FREE** |
| LCD_CAM (parallel) | up to 16 | blocked — shares I2S0 with the PDM mic (move mic → I2S1 to free) |

→ Maximum **3 flicker-free DMA chains** without touching the mic. A 4th DMA chain
needs LCD_CAM (move mic to I2S1) or stays non-DMA. To add DMA chains beyond the one
RMT channel, use the official `espressif/led_strip` **SPI backend**
(`led_strip_spi_dev.c` — 3 SPI bits per WS2812 bit @ 2.5 MHz, `flags.with_dma` +
`SPI_DMA_CH_AUTO`), integrated as a sibling to `status_led_strip_backend.c`.
**Validate on this exact board first**: idf-extra-components Issue #466 reports
DMA+S3 crashes on some boards — carry the existing `dma_fallback` pattern as a
non-DMA escape hatch.

Current implementation requests that budget as: status strip on RMT DMA, EC11 on
SPI2 DMA, key strip on SPI3 DMA, and edge/frame left on ordinary RMT. Key gets the
second SPI channel instead of edge because key feedback is part of the anti-mistouch
contract; edge is decorative and can tolerate the remaining non-DMA RMT risk.

## Cheaper alternative that must be tried first

Before any peripheral change: **raise the LED task priority and pin it to a core away
from audio** (today it is `xTaskCreate(..., priority 3, ...)` unpinned). This is free,
fast, and diagnostic — it reduces non-DMA refill starvation on EC11/key/edge and
quantifies how much of the observed flicker is CPU-starvation vs. DMA-inherent. It
does **not** fix the documented status-tail corruption (that is a DMA-timing problem),
but it is the one zero-cost lever and should not be skipped.

## Historical Evidence Policy

Raw logs, images, and one-off validation captures are not kept in the public
repository. Preserve the following evidence names in PRs, release notes, or a
private validation archive when changing this area.

| Evidence | What it proves |
|---|---|
| `ad-hoc-led-stack-disposition` | Status-only-DMA rationale, the all-strips-DMA failure, and the flicker root-cause analysis. |
| `all-strip-dma-status-20260619` | Empirical proof: all-strips-DMA -> only `status:dma1`, EC11/key/edge unavailable. |
| `status-only-dma-eased-dual-core-contract-20260619` | The accepted status-only-DMA contract. |
| `led-hardware-next-revision` | EC11/key/edge residual flicker plus signal-integrity recommendations for the next board revision. |
| `human-ai-da-dada-1950-summary` | Human acceptance: 0 flicker / 0 follow reports. |
| `status-led-active-dma-idle-nondma-stability-20260623` | The active-DMA / idle-non-DMA fix and its guardrails. |
| `low-power-status-led-final-acceptance-20260624` | Final low-power LED acceptance. |

## Guardrails (do not violate without new physical evidence)

- Do **not** permanently disable status-strip DMA — REC/AI would flicker again.
- Do **not** widen DMA to more strips without physical idle-latch + flicker evidence;
  the idle-latch bug is exactly the failure mode wider DMA would multiply.
- Low-power stable-idle final latch frames **must** force the status strip off its
  active DMA transport before suspend; otherwise heartbeat wakes can reopen the
  LED3-LED6 idle-latch corruption path. Routine low-power resume must stay scoped:
  status/BLE/edge cleanup is allowed, but EC11/key SPI DMA strips are cleared only
  when their last logical frame was lit. Active REC/AI effect frames still use
  status DMA, while clear/latch frames use `force_non_dma = pwr_only_final_latch ||
  force_clear_tx`. Shutdown-final may force all strips only once per final window;
  later PWR-only changes in that window must not continuously reinitialize EC11/key
  SPI strips through the non-DMA RMT latch.
- Any DMA / peripheral change must update the `~LED:STATUS detail=contract` fields
  (`strip_transport_requested`, `strip_transport_actual`, `spi_dma_requested`,
  `spi_dma_actual`, `spi_dma_fallback`, `rmt_tx_dma_strategy`,
  `rmt_tx_dma_all_strips`, per-strip RMT `dma_requested` / `dma_actual` /
  `dma_fallback`, `rmt_mem_block_symbols`) with validation evidence, not silently.
