from __future__ import annotations

import math
import unittest

from capture_audio_ble_wav import decode_audio_payload
from verify_ble_audio_transport_model import (
    RICE_VERSION_V1,
    RICE_VERSION_V2,
    RICE_VERSION_V3,
    rice_encode,
)


def fixture_pcm(sample_count: int = 240) -> bytes:
    samples = [
        round(4000 * math.sin(2 * math.pi * index / 37))
        for index in range(sample_count)
    ]
    return b"".join(sample.to_bytes(2, "little", signed=True) for sample in samples)


class DecodeAudioPayloadTests(unittest.TestCase):
    def test_raw_payload_is_unchanged(self) -> None:
        pcm = fixture_pcm()
        self.assertEqual(decode_audio_payload(pcm, len(pcm)), pcm)

    def test_all_supported_rice_versions_round_trip(self) -> None:
        pcm = fixture_pcm()
        for version in (RICE_VERSION_V1, RICE_VERSION_V2, RICE_VERSION_V3):
            with self.subTest(version=version):
                encoded = rice_encode(pcm, version)
                self.assertLess(len(encoded), len(pcm))
                self.assertEqual(decode_audio_payload(encoded, len(pcm)), pcm)

    def test_unknown_short_payload_is_rejected(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "without a supported lossless codec"):
            decode_audio_payload(b"\x7f\x00", 480)


if __name__ == "__main__":
    unittest.main()
