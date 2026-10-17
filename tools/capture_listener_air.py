"""Passive analyzer capture: one bulk serial owner, acknowledged radio clock.

No Listener control, pairing, input, or changes to Nordic sources.
Loads only an explicitly supplied, independently MIC-verified private bond.
"""
from pathlib import Path
import argparse, collections, ctypes, datetime, hashlib, json, logging, sys, time
import psutil, serial



def precise_utc():
    # Python3.11's ordinary clocks have 15.625ms granularity on this host.
    # Precise UTC and QPC brackets do not imply exact radio/UTC synchronization.
    from ctypes import wintypes
    stamp = wintypes.FILETIME()
    api = ctypes.WinDLL('kernel32').GetSystemTimePreciseAsFileTime
    api.argtypes, api.restype = [ctypes.POINTER(wintypes.FILETIME)], None
    api(ctypes.byref(stamp))
    ticks = (stamp.dwHighDateTime << 32) | stamp.dwLowDateTime
    return (ticks - 116444736000000000) / 10_000_000


class BulkPackets:
    def __init__(self, link, codec):
        self.link, self.codec = link, codec
        self.current = None
        self.escaped = False
        self.bytes = self.invalid = 0

    def poll(self):
        block = self.link.read(self.link.in_waiting or 1)
        self.bytes += len(block)
        packets = []
        for byte in block:
            if byte == 0xab:
                self.current, self.escaped = [], False
                continue
            if self.current is None:
                continue
            if self.escaped:
                if byte not in (0xac, 0xbd, 0xce):
                    self.current = None
                    self.invalid += 1
                else:
                    self.current.append({0xac: 0xab, 0xbd: 0xbc, 0xce: 0xcd}[byte])
                self.escaped = False
            elif byte == 0xcd:
                self.escaped = True
            elif byte == 0xbc:
                packet = self.codec.Packet(self.current)
                self.current = None
                if packet.valid:
                    packets.append(packet)
                else:
                    self.invalid += 1
            else:
                self.current.append(byte)
            if self.current is not None and len(self.current) > 4096:
                self.current = None
                self.invalid += 1
        return block, packets


def capture(output, seconds, port, bond_path, proof):
    from SnifferAPI import Packet, Pcap, Notifications
    output.mkdir(exist_ok=False)
    if sys.platform == "win32":
        import csv, io, subprocess
        owner = subprocess.check_output(["whoami", "/user", "/fo", "csv", "/nh"], text=True)
        sid = list(csv.reader(io.StringIO(owner)))[0][1]
        subprocess.run(["icacls", str(output.resolve()), "/inheritance:r", "/grant:r", "*"+sid+":(OI)(CI)F", "SYSTEM:(OI)(CI)F"], stdout=subprocess.DEVNULL, check=True)
    record = dict(startedAt=datetime.datetime.now().astimezone().isoformat(),
                  productCommands=0, productReset=False, keyCommandSent=False,
                  scope="Passive analyzer clock/advertisements; no Listener reconnection, OTA or RF throughput acceptance.",
                  startupDiscardedIds=collections.Counter(), accepted=0, rejectedStaleOrOutOfRun=0, observedIds=collections.Counter(), uartCounterGaps=[])
    save = lambda: (output / "capture.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
    save()
    logging.disable(logging.CRITICAL)
    link = serial.Serial(port=None, baudrate=1000000, timeout=.05, rtscts=True)
    link.port = port
    link.open()

    class Writer:
        def writeList(self, values):
            link.write(bytes(values))

    writer = Packet.PacketReader.__new__(Packet.PacketReader)
    Notifications.Notifier.__init__(writer)
    writer.packetCounter, writer.uart = 0, Writer()
    reader = BulkPackets(link, Packet)

    def drain_quiet(timeout):
        deadline, last_byte = time.perf_counter() + timeout, time.perf_counter()
        while time.perf_counter() < deadline:
            block, packets = reader.poll()
            if block:
                last_byte = time.perf_counter()
            for packet in packets:
                record["startupDiscardedIds"][str(packet.id)] += 1
            if time.perf_counter() - last_byte >= .25 and link.in_waiting == 0:
                return True
        return False

    try:
        link.reset_input_buffer()
        record["goIdleSupported"] = "not_assumed; protocol implementations may omit it"
        # Some 4.1.1 runs keep emitting advertisements/PINGs after GO_IDLE.
        # Silence is not an acknowledgement. Calibrate from explicit timestamp
        # replies while consuming RF traffic, and reject pre-anchor RF packets.
        anchors = []
        for attempt in range(2):
            wall_read_began = time.perf_counter()
            sent_wall, sent_mono = precise_utc(), time.perf_counter()
            wall_read_span = sent_mono - wall_read_began
            writer.sendVersionReq()
            writer.sendTimestampReq()
            anchor = None
            while time.perf_counter() - sent_mono < 3 and anchor is None:
                _, packets = reader.poll()
                for packet in packets:
                    if packet.id == 28:
                        record["firmware"] = packet.version
                    elif packet.id == 30 and anchor is None:
                        anchor = int(packet.timestamp)
            received_mono = time.perf_counter()
            if anchor is None:
                raise RuntimeError("No timestamp reply while draining RF input")
            anchors.append(dict(deviceUs=anchor, sentMono=sent_mono,
                                replyMono=received_mono, sentEpoch=sent_wall))
            if attempt == 0:
                # Consume, rather than accumulate, packets between calibration
                # requests. This interval is analyzer calibration, not product delay.
                until_calibration = time.perf_counter() + .1
                while time.perf_counter() < until_calibration:
                    reader.poll()
        first, second = anchors
        hardware_delta = ((second['deviceUs'] - first['deviceUs']) & 0xffffffff) / 1e6
        lower = max(0, second['sentMono'] - first['replyMono'])
        upper = second['replyMono'] - first['sentMono']
        record.update(calibration=anchors, calibrationHardwareDeltaMs=hardware_delta * 1000,
                      calibrationHostBoundsMs=[lower * 1000, upper * 1000])
        if not lower - .002 <= hardware_delta <= upper + .002:
            raise RuntimeError("Hardware timestamps disagree with the two request/reply intervals")
        rtt = received_mono - sent_mono
        anchor_wall = sent_wall + rtt / 2
        record.update(anchorDeviceUs=anchor, anchorHostEpoch=anchor_wall,
                      anchorRequestEpoch=sent_wall, anchorReplyEpoch=sent_wall + rtt,
                      anchorRoundTripMs=rtt * 1000,
                      absoluteTimeUncertaintyMs=(rtt / 2 + wall_read_span + .000001 + time.get_clock_info("perf_counter").resolution) * 1000,
                      hostWallClock="GetSystemTimePreciseAsFileTime()",
                      hostBracketClock=time.get_clock_info("perf_counter").implementation,
                      alignmentScope="Two explicit timestamp replies consistent with host brackets; relative RF times use one fixed hardware anchor. Not a new Listener connection.")
        bond=json.loads(bond_path.read_text())
        address=list(bytes.fromhex(bond['listenerAddress'].replace(':','')))+[1]
        writer.sendHopSequence([37,38,39])
        record["advertisingHopSequence"] = [37,38,39]
        writer.sendScan()
        writer.sendFollow(address, False, False, False)
        key=list(bytes.fromhex(bond['keyBigEndian'][2:]))
        (writer.sendSCLTK if bond['secureConnections'] else writer.sendLegacyLTK)(key)
        record.update(bondSnapshotSha256=proof["binding"]["bondSnapshotSha256"], legacyLookupObservations=[], authenticatedNonempty=0, coverageScope="Receiver observations only; complete OTA coverage requires independent HCI/firmware alignment", ready=True,targetAddress=bond['listenerAddress'],keyCommandSent=True,
                      readinessScope="Serial clock and command submission only; no key/follow acknowledgement or complete RF coverage implied",
                      keyRepresentation='current read-only bond; all9 saved encrypted frames MIC-verified offline; big-endian wire format from primary nrf4 implementation',
                      scope='Single-reader bulk transport, timestamp-acknowledged and address-followed; receiver observations only, no product commands from analyzer')
        save()
        until = time.perf_counter() + seconds
        previous_counter = None
        with (output / "radio.pcap").open("wb") as pcap:
            pcap.write(Pcap.get_global_header())
            while time.perf_counter() < until:
                _, packets = reader.poll()
                for packet in packets:
                    record["observedIds"][str(packet.id)] += 1
                    if previous_counter is not None and packet.packetCounter != ((previous_counter+1)&65535) and len(record["uartCounterGaps"]) < 50:
                        record["uartCounterGaps"].append([previous_counter,packet.packetCounter])
                    previous_counter=packet.packetCounter
                    if packet.id not in (2, 6):
                        continue
                    if packet.id == 6 and packet.crcOK:
                        frame = packet.packetList
                        if packet.encrypted and packet.micOK and frame[21] > 0:
                            record["authenticatedNonempty"] += 1
                        if not packet.encrypted and len(frame) >= 45 and (frame[20]&3) == 3 and frame[21] == 23 and frame[22] == 3:
                            fingerprint = hashlib.sha256(bytes(frame[23:33])).hexdigest()
                            record["legacyLookupObservations"].append(dict(accessAddress=bytes(frame[16:20])[::-1].hex(), matchesReference=fingerprint in proof["binding"].get("legacyLookupFingerprints", [])))
                    if packet.protover != 3:
                        raise RuntimeError("Relative/unknown protocol cannot use absolute hardware clock")
                    delta = (int(packet.timestamp) - anchor) & 0xffffffff
                    if delta > (seconds + 5) * 1_000_000:
                        record["rejectedStaleOrOutOfRun"] += 1
                        continue
                    pcap.write(Pcap.create_packet(bytes([int(port[3:])] + packet.getList()),
                                                 anchor_wall + delta / 1_000_000))
                    record["accepted"] += 1
                save()
    except Exception as error:
        record["error"] = str(error)
    finally:
        # Closing a host reader alone leaves the radio producing data. Stop and
        # drain only this analyzer before releasing our serial handle.
        try:
            writer.sendHopSequence([37,38,39])
            record["hopSequenceRestored"] = True
            writer.sendGoIdle()
            record["postStopTrafficQuietObserved"] = drain_quiet(1)
            record["firmwareStopped"] = "not_assumed; GO_IDLE is not a confirmed firmware acknowledgement"
        except Exception as error:
            record["stopError"] = str(error)
        link.close()
        record.update(completedAt=datetime.datetime.now().astimezone().isoformat(),
                      transportBytes=reader.bytes, invalidFrames=reader.invalid,
                      sourceSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
        save()
    print(json.dumps(record), flush=True)


def validate_bond_proof(bond_path, proof_path):
    proof = json.loads(proof_path.read_text(encoding="utf-8"))
    expected = proof.get("binding", {}).get("bondSnapshotSha256")
    if not expected or hashlib.sha256(bond_path.read_bytes()).hexdigest() != expected:
        raise ValueError("Bond snapshot differs from MIC-verified reference; no serial port opened")
    matches = max((len(c.get("matches", [])) for row in proof.get("connections", []) for c in row.get("authenticatedConfigurations", [])), default=0)
    if matches < 2:
        raise ValueError("No independently MIC-verified known-key reference; no serial port opened")
    return proof


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path)
    parser.add_argument("--sdk", type=Path, required=True, help="Directory containing the vendor SnifferAPI package")
    parser.add_argument("--bond", type=Path, required=True, help="Private read-only bond snapshot; never printed")
    parser.add_argument("--mic-proof", type=Path, required=True, help="Offline MIC report bound to that exact snapshot")
    parser.add_argument("--check-config-only", action="store_true")
    parser.add_argument("--seconds", type=int, default=30)
    parser.add_argument("--port", default="COM14")
    args = parser.parse_args()
    if not 1 <= args.seconds <= 90:
        parser.error("duration must fit the bounded analyzer run")
    proof = validate_bond_proof(args.bond, args.mic_proof)
    if args.check_config_only:
        print(json.dumps(dict(bondProofValid=True, serialPortOpened=False)))
        raise SystemExit(0)
    if args.output is None:
        parser.error("--output required for capture")
    if not (args.sdk / "SnifferAPI").is_dir():
        parser.error("--sdk must contain SnifferAPI")
    proc = psutil.Process()
    proc.nice(psutil.BELOW_NORMAL_PRIORITY_CLASS)
    proc.cpu_affinity([1, 3])
    sys.path.insert(0, str(args.sdk))
    capture(args.output, args.seconds, args.port, args.bond, proof)
