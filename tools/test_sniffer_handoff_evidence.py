"""Receiver diagnostics: reject unrelated handoffs and retain missing evidence."""
import hashlib
import unittest
from unittest.mock import patch

import analyze_listener_air_refresh as refresh
from capture_listener_air import target_advertisement_after_handoff


class HandoffEvidenceTests(unittest.TestCase):
    def test_only_target_directed_advertising_after_accepted_handoff(self):
        address = 'D7:4B:13:D9:81:C3'
        frame = bytearray(34)
        frame[5], frame[7], frame[20] = 2, 1, 1
        frame[16:20] = bytes.fromhex('d6be898e')
        frame[22:28] = bytes.fromhex(address.replace(':', ''))[::-1]
        marker = dict(targetAddress=address, acceptedEpoch=100,
                      source='installed_type_active_capture_ota_handoff_accepted')
        admit = lambda f, t, m: target_advertisement_after_handoff(f, t, address, m)
        self.assertTrue(admit(frame, 100, marker))
        self.assertTrue(admit(frame, 108, marker))
        for stamp in (99.999, 108.001):
            self.assertFalse(admit(frame, stamp, marker))
        for changes in ({'source': 'request_submitted'}, {'targetAddress': '00:00:00:00:00:00'},
                        {'acceptedEpoch': None}):
            self.assertFalse(admit(frame, 101, dict(marker, **changes)))
        for index, value in ((5, 6), (7, 0), (16, 0), (20, 0), (22, 0)):
            changed = bytearray(frame)
            changed[index] = value
            self.assertFalse(admit(changed, 101, marker))
        self.assertFalse(admit(frame[:27], 101, marker))

    def test_missing_encryption_response_keeps_frames_unverified(self):
        request = bytes([3]) + bytes(22)
        fingerprint = hashlib.sha256(request[1:11]).hexdigest()
        frames = [dict(aa='example', payload=request, flags=1, header=3, t=100)]
        frames += [dict(aa='example', payload=bytes(5), flags=5, header=2, t=101)] * 18
        with patch.object(refresh, 'packets', return_value=iter(frames)):
            result = refresh.analyze(None, bytes(16), {fingerprint})
        epoch = result['epochs'][0]
        self.assertTrue(epoch['missingResponse'])
        self.assertEqual(epoch['badMicCapturedFrames'], 18)
        self.assertEqual(epoch['unverifiedFrames'], 18)
        self.assertEqual(epoch['verifiedFrames'], 0)
        self.assertFalse(epoch['usableIndependentEpoch'])
        self.assertFalse(result['deviceOperations'])


if __name__ == '__main__':
    unittest.main()
