import argparse
import json
import pathlib
import subprocess
import sys


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run Listener-Type foundry_asr_probe and write its machine-readable JSON to a file."
    )
    parser.add_argument("--probe-exe", required=True)
    parser.add_argument("--audio", required=True)
    parser.add_argument("--out-json", required=True)
    parser.add_argument("--model", default="whisper-small")
    parser.add_argument("--runtime-source", default="auto")
    parser.add_argument("--language", default="zh")
    parser.add_argument("--timeout-seconds", type=int, default=180)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    command = [
        args.probe_exe,
        "transcribe",
        "--model",
        args.model,
        "--runtime-source",
        args.runtime_source,
        "--language",
        args.language,
        "--audio",
        args.audio,
        "--timeout-seconds",
        str(args.timeout_seconds),
    ]
    completed = subprocess.run(
        command,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
        text=True,
    )
    if completed.stdout:
        print(completed.stdout, end="")
    if completed.stderr:
        print(completed.stderr, end="", file=sys.stderr)

    report = None
    for line in completed.stdout.splitlines():
        if line.startswith("foundry_probe_result_json="):
            report = json.loads(line.split("=", 1)[1])

    if report is None:
        report = {
            "status": "FAIL",
            "command": "transcribe",
            "model": args.model,
            "runtimeSource": args.runtime_source,
            "runtimeReady": None,
            "modelId": None,
            "transcript": None,
            "error": "foundry_probe_result_json line missing",
        }

    out_path = pathlib.Path(args.out_json)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"foundry_probe_result_json={out_path}", flush=True)

    if completed.returncode != 0 or report.get("status") != "PASS":
        raise SystemExit(completed.returncode if completed.returncode != 0 else 1)


if __name__ == "__main__":
    main()
