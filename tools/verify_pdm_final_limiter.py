from __future__ import annotations

import random


CEILING = 23170
Q15_ONE = 32768


def limit_frame(samples: list[int]) -> tuple[list[int], int, int]:
    peak = max((abs(value) for value in samples), default=0)
    if peak <= CEILING:
        return list(samples), peak, 0
    scale_q15 = (CEILING << 15) // peak
    reduction_permille = 1000 - (scale_q15 * 1000 // Q15_ONE)
    return [int(value * scale_q15 / Q15_ONE) for value in samples], peak, reduction_permille


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    unchanged = [-CEILING, -1, 0, 1, CEILING]
    output, peak, reduction = limit_frame(unchanged)
    require(output == unchanged, "a frame at the ceiling was changed")
    require(peak == CEILING and reduction == 0, "no-op telemetry is wrong")

    extremes = [-32768, -30000, -CEILING, 0, CEILING, 30000, 32767]
    output, peak, reduction = limit_frame(extremes)
    require(peak == 32768, "signed INT16_MIN peak was not measured safely")
    require(max(abs(value) for value in output) <= CEILING, "extreme frame exceeded ceiling")
    require(reduction > 0, "limited frame did not report gain reduction")
    require(all(
        source == 0 or target == 0 or (source < 0) == (target < 0)
        for source, target in zip(extremes, output)
    ), "limiter changed sample polarity")

    rng = random.Random(104)
    limited_frames = 0
    max_input_peak = 0
    max_reduction = 0
    for _ in range(2000):
        frame = [rng.randint(-32768, 32767) for _ in range(160)]
        output, peak, reduction = limit_frame(frame)
        max_input_peak = max(max_input_peak, peak)
        max_reduction = max(max_reduction, reduction)
        limited_frames += int(peak > CEILING)
        require(max(abs(value) for value in output) <= CEILING, "random frame exceeded ceiling")

    require(limited_frames > 0, "random corpus did not exercise limiting")
    require(max_input_peak == 32768, "input peak telemetry lost INT16_MIN")
    require(max_reduction > 0, "max reduction telemetry was not accumulated")
    print(
        "PASS: final limiter preserves in-range PCM, contains INT16 extremes and "
        f"2000 random frames at {CEILING}, limited_frames={limited_frames}, "
        f"max_reduction_permille={max_reduction}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
