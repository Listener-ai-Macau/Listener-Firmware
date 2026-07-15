#!/usr/bin/env python3
"""Fail a physical recording capture when BLE backpressure removed real PCM time."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


INTEGRITY_RE = re.compile(
    r"record session capture integrity: session_id=(?P<session>\d+) "
    r"pcm_ms=(?P<pcm_ms>\d+) "
    r"capture_backpressure_gap_ms=(?P<gap_ms>\d+) "
    r"capture_backpressure_pause_frames=(?P<pause_frames>\d+)"
)
SUMMARY_RE = re.compile(r"audio session transport summary: session=(?P<session>\d+) (?P<body>.*)")
KEY_VALUE_RE = re.compile(r"(?P<key>[a-z_]+)=(?P<value>[^\s]+)")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", required=True, type=Path, help="Firmware serial capture")
    parser.add_argument("--max-gap-ms", type=int, default=0)
    parser.add_argument("--max-elapsed-over-pcm-ms", type=int, default=300)
    parser.add_argument("--output-json", type=Path)
    return parser.parse_args()


def as_int(values: dict[str, str], key: str) -> int:
    try:
        return int(values[key])
    except (KeyError, ValueError) as error:
        raise ValueError(f"missing or invalid {key}") from error


def main() -> int:
    args = parse_args()
    text = args.log.read_text(encoding="utf-8", errors="replace")
    integrity: dict[int, dict[str, int]] = {}
    summaries: dict[int, dict[str, str]] = {}

    for line in text.splitlines():
        match = INTEGRITY_RE.search(line)
        if match:
            session = int(match["session"])
            integrity[session] = {
                "pcm_ms": int(match["pcm_ms"]),
                "capture_backpressure_gap_ms": int(match["gap_ms"]),
                "capture_backpressure_pause_frames": int(match["pause_frames"]),
            }
            continue
        match = SUMMARY_RE.search(line)
        if match:
            summaries[int(match["session"])] = {
                item["key"]: item["value"] for item in KEY_VALUE_RE.finditer(match["body"])
            }

    errors: list[str] = []
    sessions: list[dict[str, int | str]] = []
    if not integrity:
        errors.append("no record session capture integrity line found")

    for session, capture in sorted(integrity.items()):
        report: dict[str, int | str] = {"session_id": session, **capture}
        if capture["capture_backpressure_gap_ms"] > args.max_gap_ms:
            errors.append(
                f"session {session}: capture_backpressure_gap_ms="
                f"{capture['capture_backpressure_gap_ms']} exceeds {args.max_gap_ms}"
            )
        if capture["capture_backpressure_pause_frames"] != 0:
            errors.append(
                f"session {session}: capture_backpressure_pause_frames="
                f"{capture['capture_backpressure_pause_frames']} must be 0"
            )

        summary = summaries.get(session)
        if summary is None:
            errors.append(f"session {session}: missing audio transport summary")
        else:
            try:
                elapsed_ms = as_int(summary, "elapsed_ms")
                audio_sent = as_int(summary, "audio_sent")
                expected_packet_count = as_int(summary, "expected_packet_count")
                audio_failed = as_int(summary, "audio_failed")
                pool_alloc_failed = as_int(summary, "pool_alloc_failed")
                queue_full = as_int(summary, "queue_full")
                elapsed_over_pcm_ms = elapsed_ms - capture["pcm_ms"]
                report.update(
                    elapsed_ms=elapsed_ms,
                    elapsed_over_pcm_ms=elapsed_over_pcm_ms,
                    audio_sent=audio_sent,
                    expected_packet_count=expected_packet_count,
                )
                if elapsed_over_pcm_ms > args.max_elapsed_over_pcm_ms:
                    errors.append(
                        f"session {session}: elapsed_over_pcm_ms={elapsed_over_pcm_ms} "
                        f"exceeds {args.max_elapsed_over_pcm_ms}"
                    )
                if audio_sent != expected_packet_count:
                    errors.append(
                        f"session {session}: audio_sent={audio_sent} "
                        f"does not match expected_packet_count={expected_packet_count}"
                    )
                if audio_failed != 0 or pool_alloc_failed != 0 or queue_full != 0:
                    errors.append(
                        f"session {session}: transport failure counters audio_failed={audio_failed} "
                        f"pool_alloc_failed={pool_alloc_failed} queue_full={queue_full}"
                    )
                if summary.get("last_drop_reason") != "none":
                    errors.append(
                        f"session {session}: last_drop_reason={summary.get('last_drop_reason')!r}"
                    )
            except ValueError as error:
                errors.append(f"session {session}: {error}")
        sessions.append(report)

    report = {
        "status": "PASS" if not errors else "FAIL",
        "log": str(args.log),
        "max_gap_ms": args.max_gap_ms,
        "max_elapsed_over_pcm_ms": args.max_elapsed_over_pcm_ms,
        "sessions": sessions,
        "errors": errors,
    }
    rendered = json.dumps(report, ensure_ascii=True, indent=2)
    if args.output_json:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
