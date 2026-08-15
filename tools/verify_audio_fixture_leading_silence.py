from __future__ import annotations

import struct
import tempfile
import wave
from pathlib import Path

from ble_audio_regression_common import (
    PCM_CHANNELS,
    PCM_SAMPLE_RATE,
    PCM_WIDTH_BYTES,
    prepend_wav_silence_pcm16,
    read_wav_frames,
)


def main() -> int:
    source = [123, -456] * 160
    with tempfile.TemporaryDirectory(prefix="listener-leading-silence-") as temp_dir:
        wav_path = Path(temp_dir) / "fixture.wav"
        with wave.open(str(wav_path), "wb") as wav_file:
            wav_file.setnchannels(PCM_CHANNELS)
            wav_file.setsampwidth(PCM_WIDTH_BYTES)
            wav_file.setframerate(PCM_SAMPLE_RATE)
            wav_file.writeframes(struct.pack("<" + "h" * len(source), *source))

        prepend_wav_silence_pcm16(wav_path, 500)
        frames = read_wav_frames(wav_path)
        silence_samples = PCM_SAMPLE_RATE // 2
        assert len(frames) == silence_samples + len(source)
        assert frames[:silence_samples] == [0] * silence_samples
        assert frames[silence_samples:] == source
        with wave.open(str(wav_path), "rb") as wav_file:
            assert wav_file.getnchannels() == PCM_CHANNELS
            assert wav_file.getsampwidth() == PCM_WIDTH_BYTES
            assert wav_file.getframerate() == PCM_SAMPLE_RATE

    print("PASS: physical playback fixtures retain exact PCM after 500 ms leading silence")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
