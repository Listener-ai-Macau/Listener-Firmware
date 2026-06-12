import argparse
import csv
import json
import pathlib
import re
import statistics
import time

import serial


POWER_RE = re.compile(r"^~BOARD:POWER\s+(.*)$")
STATUS_RE = re.compile(r"^~POWER:STATUS\s+(.*)$")
TOKEN_RE = re.compile(r'([A-Za-z_][A-Za-z0-9_]*)=("[^"]*"|\S+)')


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Monitor idle power telemetry over time in one passive serial session."
    )
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--duration-seconds", type=int, default=900)
    parser.add_argument("--interval-seconds", type=int, default=30)
    parser.add_argument("--sample-read-seconds", type=float, default=4.0)
    parser.add_argument(
        "--warmup-seconds",
        type=int,
        default=75,
        help="Delay after opening serial before the first sample, so idle monitoring excludes boot/active settling.",
    )
    parser.add_argument("--output-dir", default="tests/artifacts/idle_power_monitor")
    parser.add_argument("--spike-ma", type=int, default=50)
    parser.add_argument(
        "--board-power",
        choices=("none", "cached", "force"),
        default="none",
        help=(
            "Current telemetry command per sample. none only asks ~POWER:STATUS; cached uses ~BOARD:POWER and expects "
            "firmware-side cached readings; force uses ~BOARD:POWER:FORCE and performs "
            "live ADC current reads; none avoids current telemetry queries."
        ),
    )
    parser.add_argument(
        "--force-board-power-every",
        type=int,
        default=0,
        help=(
            "When --board-power=cached, send ~BOARD:POWER:FORCE every N samples to "
            "seed or refresh cached current telemetry. 0 disables forced refresh."
        ),
    )
    return parser.parse_args()


def convert_value(value: str):
    if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
        return value[1:-1]
    if re.fullmatch(r"-?\d+", value):
        return int(value)
    return value


def parse_tokens(text: str) -> dict:
    return {match.group(1): convert_value(match.group(2)) for match in TOKEN_RE.finditer(text)}


def read_lines_until(ser: serial.Serial, deadline: float) -> list[str]:
    lines: list[str] = []
    while time.time() < deadline:
        try:
            raw = ser.readline()
        except serial.SerialException:
            raise
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        if line:
            lines.append(line)
    return lines


def parse_sample(sample_index: int, started_at: float, lines: list[str]) -> dict:
    branches: dict[str, dict] = {}
    power_status: dict = {}
    for line in lines:
        power_match = POWER_RE.match(line)
        if power_match:
            data = parse_tokens(power_match.group(1))
            branch = str(data.get("branch", "unknown"))
            branches[branch] = data
            continue
        status_match = STATUS_RE.match(line)
        if status_match:
            power_status = parse_tokens(status_match.group(1))

    tps = branches.get("TPS63020_input_branch", {})
    sy = branches.get("SY7088_input_branch", {})
    return {
        "sample": sample_index,
        "elapsed_s": round(time.time() - started_at, 3),
        "captured_at_epoch": time.time(),
        "state": power_status.get("state", ""),
        "idle_ms": power_status.get("idle_ms"),
        "user_idle_ms": power_status.get("user_idle_ms"),
        "radio_idle_ms": power_status.get("radio_idle_ms"),
        "ble_connected": power_status.get("ble_connected"),
        "external_power_present": power_status.get("external_power_present"),
        "usb_power_present": power_status.get("usb_power_present"),
        "charging": power_status.get("charging"),
        "charge_full": power_status.get("charge_full"),
        "charge_full_latched": power_status.get("charge_full_latched"),
        "charge_full_candidate_ms": power_status.get("charge_full_candidate_ms"),
        "audio_idle_power_save": power_status.get("audio_idle_power_save"),
        "low_power_idle_ms": power_status.get("low_power_idle_ms"),
        "hardware_shutdown_ms": power_status.get("hardware_shutdown_ms"),
        "battery_mv": power_status.get("battery_mv"),
        "tps_current_ma": tps.get("estimated_input_current_ma"),
        "tps_power_mw": tps.get("estimated_input_power_mw"),
        "tps_adc_mv": tps.get("adc_mv"),
        "tps_sample_mode": tps.get("sample_mode"),
        "tps_cache_valid": tps.get("cache_valid"),
        "tps_cache_sequence": tps.get("cache_sequence"),
        "sy_current_ma": sy.get("estimated_input_current_ma"),
        "sy_power_mw": sy.get("estimated_input_power_mw"),
        "sy_adc_mv": sy.get("adc_mv"),
        "sy_sample_mode": sy.get("sample_mode"),
        "sy_cache_valid": sy.get("cache_valid"),
        "sy_cache_sequence": sy.get("cache_sequence"),
        "raw_line_count": len(lines),
        "has_tps": bool(tps),
        "has_sy": bool(sy),
        "has_power_status": bool(power_status),
    }


def numeric_values(samples: list[dict], key: str) -> list[float]:
    values: list[float] = []
    for sample in samples:
        value = sample.get(key)
        if isinstance(value, (int, float)):
            values.append(float(value))
    return values


def stats_for(samples: list[dict], key: str) -> dict:
    values = numeric_values(samples, key)
    if not values:
        return {"count": 0}
    return {
        "count": len(values),
        "min": min(values),
        "max": max(values),
        "mean": statistics.fmean(values),
        "median": statistics.median(values),
    }


def write_outputs(output_dir: pathlib.Path, samples: list[dict], raw_lines: list[str], spike_ma: int) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    jsonl_path = output_dir / "idle_power_samples.jsonl"
    csv_path = output_dir / "idle_power_samples.csv"
    raw_path = output_dir / "idle_power_raw.log"
    summary_path = output_dir / "idle_power_summary.json"
    md_path = output_dir / "idle_power_summary.md"

    jsonl_path.write_text(
        "\n".join(json.dumps(sample, ensure_ascii=False) for sample in samples) + "\n",
        encoding="utf-8",
    )
    raw_path.write_text("\n".join(raw_lines) + "\n", encoding="utf-8")
    if samples:
        with csv_path.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(samples[0].keys()))
            writer.writeheader()
            writer.writerows(samples)

    spikes = [
        sample
        for sample in samples
        if (isinstance(sample.get("tps_current_ma"), int) and sample["tps_current_ma"] >= spike_ma)
        or (isinstance(sample.get("sy_current_ma"), int) and sample["sy_current_ma"] >= spike_ma)
        or sample.get("state") == "ACTIVE"
    ]
    summary = {
        "sample_count": len(samples),
        "spike_threshold_ma": spike_ma,
        "spike_count": len(spikes),
        "states": sorted({str(sample.get("state", "")) for sample in samples}),
        "tps_current_ma": stats_for(samples, "tps_current_ma"),
        "sy_current_ma": stats_for(samples, "sy_current_ma"),
        "battery_mv": stats_for(samples, "battery_mv"),
        "telemetry_modes": sorted(
            {
                str(mode)
                for sample in samples
                for mode in (sample.get("tps_sample_mode"), sample.get("sy_sample_mode"))
                if mode is not None
            }
        ),
        "spikes": spikes,
        "artifacts": {
            "jsonl": str(jsonl_path),
            "csv": str(csv_path),
            "raw_log": str(raw_path),
            "summary_json": str(summary_path),
            "summary_md": str(md_path),
        },
    }
    summary_path.write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")

    lines = [
        "# Idle Power Monitor Summary",
        "",
        f"Samples: {len(samples)}",
        f"States: {', '.join(summary['states'])}",
        f"Spike threshold: {spike_ma} mA",
        f"Spike count: {len(spikes)}",
        f"Telemetry modes: {', '.join(summary['telemetry_modes']) if summary['telemetry_modes'] else 'none'}",
        "",
        "| Branch | min mA | median mA | mean mA | max mA |",
        "|---|---:|---:|---:|---:|",
    ]
    for label, key in (("TPS63020_input_branch", "tps_current_ma"), ("SY7088_input_branch", "sy_current_ma")):
        stats = summary[key]
        if stats.get("count", 0) == 0:
            lines.append(f"| {label} | n/a | n/a | n/a | n/a |")
        else:
            lines.append(
                f"| {label} | {stats['min']:.0f} | {stats['median']:.0f} | "
                f"{stats['mean']:.1f} | {stats['max']:.0f} |"
            )
    if spikes:
        lines.extend(["", "## Spike Samples", ""])
        for sample in spikes:
            lines.append(
                "- sample={sample} elapsed_s={elapsed_s} state={state} "
                "idle_ms={idle_ms} tps={tps_current_ma}mA sy={sy_current_ma}mA "
                "battery={battery_mv}mV charge_full={charge_full} "
                "latched={charge_full_latched} candidate_ms={charge_full_candidate_ms} "
                "audio_idle={audio_idle_power_save}".format(
                    **sample
                )
            )
    md_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def open_serial_no_reset(port: str, baud: int) -> serial.Serial:
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 0.2
    ser.dsrdtr = False
    ser.rtscts = False
    ser.dtr = False
    ser.rts = False
    ser.open()
    ser.dtr = False
    ser.rts = False
    return ser


def main() -> None:
    args = parse_args()
    output_dir = pathlib.Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    samples: list[dict] = []
    raw_lines: list[str] = []
    sample_index = 0

    with open_serial_no_reset(args.port, args.baud) as ser:
        time.sleep(0.3)
        ser.reset_input_buffer()
        if args.warmup_seconds > 0:
            print(f"warmup_seconds={args.warmup_seconds}", flush=True)
            warmup_lines = read_lines_until(ser, time.time() + args.warmup_seconds)
            raw_lines.extend(f"[warmup] {line}" for line in warmup_lines)
            write_outputs(output_dir, samples, raw_lines, args.spike_ma)
        started_at = time.time()
        next_sample_at = started_at
        deadline = started_at + args.duration_seconds
        while time.time() < deadline:
            now = time.time()
            if now < next_sample_at:
                time.sleep(min(0.2, next_sample_at - now))
                continue
            sample_index += 1
            if args.board_power == "force" or (
                args.board_power == "cached"
                and args.force_board_power_every > 0
                and sample_index % args.force_board_power_every == 0
            ):
                ser.write(b"~BOARD:POWER:FORCE\n")
            elif args.board_power == "cached":
                ser.write(b"~BOARD:POWER\n")
            ser.write(b"~POWER:STATUS\n")
            ser.flush()
            lines = read_lines_until(ser, time.time() + args.sample_read_seconds)
            raw_lines.extend(f"[sample={sample_index}] {line}" for line in lines)
            sample = parse_sample(sample_index, started_at, lines)
            samples.append(sample)
            write_outputs(output_dir, samples, raw_lines, args.spike_ma)
            print(
                "sample={sample} elapsed_s={elapsed_s} state={state} idle_ms={idle_ms} "
                "tps={tps_current_ma}mA sy={sy_current_ma}mA battery={battery_mv}mV "
                "audio_idle={audio_idle_power_save} charge_full={charge_full}".format(**sample),
                flush=True,
            )
            next_sample_at += args.interval_seconds

    write_outputs(output_dir, samples, raw_lines, args.spike_ma)
    print(f"summary={output_dir / 'idle_power_summary.json'}", flush=True)
    print(f"report={output_dir / 'idle_power_summary.md'}", flush=True)


if __name__ == "__main__":
    main()
