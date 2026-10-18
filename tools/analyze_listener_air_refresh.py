"""Recover metadata from saved nRF bad-MIC frames using a verified existing bond.

Offline only. Never writes a device, guesses a key, or exports decrypted payloads.
Missing RF frames stay missing. This does not repair live radio following.
"""
from pathlib import Path
import argparse
import collections
import datetime
import hashlib
import json
import struct
import time

from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.ciphers.aead import AESCCM


def validate_reference(bond_path, proof_path):
    raw = bond_path.read_bytes()
    proof = json.loads(proof_path.read_text(encoding='utf-8'))
    binding = proof.get('binding', {})
    if hashlib.sha256(raw).hexdigest() != binding.get('bondSnapshotSha256'):
        raise ValueError('Bond differs from independently verified snapshot')
    verified = max((len(c.get('matches', []))
                    for x in proof.get('connections', [])
                    for c in x.get('authenticatedConfigurations', [])), default=0)
    if verified < 2 or not binding.get('legacyLookupFingerprints'):
        raise ValueError('Missing independent MIC proof or handshake binding')
    bond = json.loads(raw)
    if bond.get('secureConnections'):
        raise ValueError('This recovery tool supports verified legacy bonds only')
    key = bytes.fromhex(bond['keyBigEndian'][2:])
    if len(key) != 16:
        raise ValueError('Invalid existing key size')
    return key, set(binding['legacyLookupFingerprints']), binding['bondSnapshotSha256']


def packets(path):
    with path.open('rb') as stream:
        header = stream.read(24)
        if len(header) != 24 or header[:4] != b'\xd4\xc3\xb2\xa1' or struct.unpack_from('<I', header, 20)[0] != 272:
            raise ValueError('Expected little-endian microsecond Nordic BLE PCAP')
        frame = 0
        while raw := stream.read(16):
            if len(raw) != 16:
                raise ValueError('Truncated packet header')
            sec, usec, size, original = struct.unpack('<IIII', raw)
            if size > 8192 or usec >= 1_000_000 or original < size:
                raise ValueError('Invalid Nordic packet size/timestamp')
            block = stream.read(size)
            if len(block) != size:
                raise ValueError('Truncated packet')
            frame += 1
            if size < 23:
                continue
            p = block[1:]
            if p[5] != 6 or p[6] != 10 or not p[7] & 1:
                continue
            length = p[21]
            if len(p) < 22+length:
                continue
            yield dict(frame=frame, t=sec+usec/1e6, aa=p[16:20].hex(),
                       flags=p[7], header=p[20], payload=p[22:22+length],
                       event=int.from_bytes(p[10:12], 'little'))


def control_metadata(plain):
    row = dict(opcode=hex(plain[0]))
    if plain[0] == 0 and len(plain) == 12:
        row.update(intervalMs=int.from_bytes(plain[4:6], 'little')*1.25,
                   latency=int.from_bytes(plain[6:8], 'little'),
                   supervisionTimeoutMs=int.from_bytes(plain[8:10], 'little')*10,
                   instant=int.from_bytes(plain[10:12], 'little'))
    elif plain[0] in (0x14, 0x15) and len(plain) == 9:
        row.update(maxRxOctets=int.from_bytes(plain[1:3], 'little'),
                   maxRxTimeUs=int.from_bytes(plain[3:5], 'little'),
                   maxTxOctets=int.from_bytes(plain[5:7], 'little'),
                   maxTxTimeUs=int.from_bytes(plain[7:9], 'little'))
    elif plain[0] == 0x18 and len(plain) == 5:
        row.update(centralPhy=plain[1], peripheralPhy=plain[2],
                   instant=int.from_bytes(plain[3:5], 'little'))
    return row


def analyze(pcap, key, fingerprints, max_counter_gap=2048, seconds=25):
    epochs, active = [], {}
    for f in packets(pcap):
        aa, pay, flags = f['aa'], f['payload'], f['flags']
        plain_control = not flags & 4 and f['header'] & 3 == 3
        if plain_control and len(pay) == 23 and pay[0] == 3:
            fingerprint = hashlib.sha256(pay[1:11]).hexdigest()
            if fingerprint not in fingerprints:
                active.pop(aa, None)
                continue
            if aa in active and active[aa]['request'] == pay:
                continue  # retransmission is not a new encryption epoch
            e = dict(aa=aa, request=pay, response=None, frames=[], requestTime=f['t'])
            active[aa] = e
            epochs.append(e)
        elif plain_control and len(pay) == 13 and pay[0] == 4 and aa in active:
            active[aa]['response'] = pay
        elif aa in active and flags & 4 and not flags & 8 and len(pay) >= 5:
            active[aa]['frames'].append(f)
    results, started = [], time.monotonic()
    for e in epochs:
        row = dict(accessAddress=e['aa'], requestUtc=datetime.datetime.fromtimestamp(
            e['requestTime'], datetime.timezone.utc).isoformat(),
            badMicCapturedFrames=len(e['frames']), verifiedFrames=0,
            unverifiedFrames=len(e['frames']),
            usableIndependentEpoch=False, recovered=[], missingResponse=not bool(e['response']))
        results.append(row)
        if not e['response']:
            continue
        req, rsp = e['request'], e['response']
        aes = Cipher(algorithms.AES(key), modes.ECB()).encryptor()
        session = aes.update((req[11:19]+rsp[1:9])[::-1])+aes.finalize()
        ccm, iv, last = AESCCM(session, tag_length=4), req[19:23]+rsp[9:13], {0: 0, 1: 0}
        recovered = []
        for f in e['frames']:
            if time.monotonic()-started >= seconds:
                row['boundedAnalysisStopped'] = True
                break
            direction, match = int(bool(f['flags'] & 2)), None
            for counter in range(max(0, last[direction]-2), last[direction]+max_counter_gap+1):
                if time.monotonic()-started >= seconds:
                    break
                nonce = bytearray(counter.to_bytes(5, 'little'))
                nonce[4] |= direction << 7
                try:
                    plain = ccm.decrypt(bytes(nonce)+iv, f['payload'], bytes([f['header'] & 0xe3]))
                except InvalidTag:
                    continue
                match = dict(frame=f['frame'], t=f['t'], event=f['event'], direction=direction,
                             counter=counter, llid=f['header'] & 3,
                             plainBytes=len(plain), phy=f['flags'] >> 4 & 7)
                if match['llid'] == 3 and plain:
                    match['control'] = control_metadata(plain)
                last[direction] = max(last[direction], counter)
                break
            if match:
                recovered.append(match)
        row['verifiedFrames'] = len(recovered)
        row['unverifiedFrames'] = len(e['frames'])-len(recovered)
        row['usableIndependentEpoch'] = len(recovered) >= 2
        if row['usableIndependentEpoch']:
            row['recovered'] = recovered
    return dict(at=datetime.datetime.now().astimezone().isoformat(), epochs=results,
                elapsedSec=time.monotonic()-started, keyOrPayloadExported=False,
                deviceOperations=False,
                scope='Only independently authenticated captured bad-MIC frames. Missing RF remains missing; no live-following repair or complete throughput certification.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pcap', type=Path, required=True)
    p.add_argument('--bond', type=Path, required=True)
    p.add_argument('--mic-proof', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    key, fingerprints, binding = validate_reference(args.bond, args.mic_proof)
    result = analyze(args.pcap, key, fingerprints)
    result.update(bondSnapshotSha256=binding,
                  captureSha256=hashlib.sha256(args.pcap.read_bytes()).hexdigest())
    # Metadata only. Do not overwrite an existing evidence file by accident.
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2)
    print(json.dumps(dict(epochs=len(result['epochs']),
        independentlyRecovered=sum(x['verifiedFrames'] for x in result['epochs']),
        elapsedSec=result['elapsedSec'], deviceOperations=False)))


if __name__ == '__main__':
    main()
