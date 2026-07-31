from __future__ import annotations

from dataclasses import dataclass


NOISE_MIN = 1
EWMA_SHIFT = 5
LOW_SNR_NUMERATOR = 3
LOW_SNR_DENOMINATOR = 2
LOW_SNR_MIN_MARGIN = 3


@dataclass
class Gate:
    noise_floor_q8: int = 4 << 8
    low_snr_speech_frames: int = 0

    def step(self, mean_abs: int, vad_speech: bool, session_active: bool = False) -> bool:
        noise_floor = max(NOISE_MIN, self.noise_floor_q8 >> 8)
        ratio_threshold = (
            noise_floor * LOW_SNR_NUMERATOR + LOW_SNR_DENOMINATOR - 1
        ) // LOW_SNR_DENOMINATOR
        threshold = max(ratio_threshold, noise_floor + LOW_SNR_MIN_MARGIN)
        accepted = vad_speech
        if not session_active and vad_speech and mean_abs < threshold:
            self.low_snr_speech_frames += 1

        if not session_active and not vad_speech:
            target_q8 = mean_abs << 8
            delta = target_q8 - self.noise_floor_q8
            step = int(delta / (1 << EWMA_SHIFT))
            if step == 0 and delta:
                step = 1 if delta > 0 else -1
            self.noise_floor_q8 = max(NOISE_MIN << 8, self.noise_floor_q8 + step)
        return accepted


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    quiet = Gate()
    for index in range(300):
        level = (3, 4, 5, 4)[index % 4]
        false_vad = False
        require(
            not quiet.step(level, false_vad),
            "VADNet non-speech opened the wake gate",
        )

    for noise, speech in ((2, 8), (8, 24), (32, 96)):
        gate = Gate(noise << 8)
        for _ in range(160):
            require(not gate.step(noise, False), "noise frame was accepted as speech")
        require(
            gate.step(speech, True),
            f"speech with clear SNR was rejected: noise={noise} speech={speech}",
        )

    changing_room = Gate()
    for level in range(4, 25):
        for _ in range(80):
            require(
                not changing_room.step(level, False),
                "rising VADNet non-speech opened the wake gate",
            )

    require(
        Gate(40 << 8).step(1, True, session_active=True),
        "active recording must preserve VADNet speech for auto-stop semantics",
    )

    construction = Gate()
    for frame in range(2400):
        impulse = frame % 47 in (0, 1, 2)
        drill = (frame // 23) % 5 in (1, 2, 3)
        noise_level = 12 + (38 if impulse else 0) + (17 if drill else 0)
        construction.step(noise_level, False)
        if frame % 160 in range(48, 112):
            before = construction.noise_floor_q8
            require(
                construction.step(max(2, noise_level // 2), True),
                "low-SNR owner speech was vetoed in non-stationary noise",
            )
            require(
                construction.noise_floor_q8 == before,
                "VADNet speech contaminated the diagnostic noise estimate",
            )
    require(
        construction.low_snr_speech_frames > 0,
        "construction model did not exercise low-SNR speech preservation",
    )

    print(
        "PASS: VADNet remains the candidate authority; diagnostics learn only "
        "non-speech and preserve low-SNR speech through impulsive construction noise"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
