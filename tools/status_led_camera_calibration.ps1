param(
    [string]$Port = "COMx",
    [string]$Camera = "auto",
    [string]$Zones = "status,key",
    [string]$Mode = "rgbw-single-led",
    [string]$OutputDir = "docs\validation\voice-keyboard-camera-status-key-led-tuning-1.1",
    [int]$Baud = 115200,
    [int]$SettleMs = 250,
    [int]$ReadbackMs = 350,
    [int]$PostFlashWaitMs = 3000,
    [switch]$ManifestOnly,
    [switch]$SkipFlash
)

$ErrorActionPreference = "Stop"

if (-not $ManifestOnly.IsPresent -and $Camera.ToLowerInvariant() -ne "dry-run") {
    . (Join-Path $PSScriptRoot "idf_env.ps1")
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedOutputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath(
    (Join-Path $repoRoot $OutputDir))
New-Item -ItemType Directory -Force -Path $resolvedOutputDir | Out-Null

$pythonPath = (Get-Command python -ErrorAction Stop).Path
$payload = [ordered]@{
    port = $Port
    camera = $Camera
    zones = $Zones
    mode = $Mode
    output_dir = $resolvedOutputDir
    repo_root = $repoRoot
    tools_dir = $PSScriptRoot
    baud = $Baud
    settle_ms = $SettleMs
    readback_ms = $ReadbackMs
    post_flash_wait_ms = $PostFlashWaitMs
    manifest_only = $ManifestOnly.IsPresent
    flash_before_capture = -not $SkipFlash.IsPresent
}
$payloadJson = $payload | ConvertTo-Json -Depth 8 -Compress
$payloadBase64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($payloadJson))

@"
import base64
import json
import math
import os
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

payload = json.loads(base64.b64decode("$payloadBase64").decode("utf-8"))

PLAN = "voice-keyboard-camera-status-key-led-tuning"
STEP = "1.1"
EXPECTED_MODE = "rgbw-single-led"


def utc_now():
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def fail(message, manifest=None, exit_code=2):
    if manifest is not None:
        manifest.setdefault("exact_failures", []).append(message)
        write_manifest(manifest)
    raise SystemExit(message)


def write_manifest(manifest):
    output_dir = Path(payload["output_dir"])
    output_dir.mkdir(parents=True, exist_ok=True)
    manifest["updated_at"] = utc_now()
    manifest_path = output_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")


def resolve_esp32_port(requested):
    from serial.tools import list_ports

    ports = list(list_ports.comports())
    if requested and requested.lower() != "comx":
        for port in ports:
            if port.device.lower() == requested.lower():
                return port, ports
        raise RuntimeError(f"requested port {requested} was not found")

    esp32_ports = []
    for port in ports:
        hwid = (port.hwid or "").upper()
        desc = (port.description or "").lower()
        if "VID:PID=303A:" in hwid or "ESP32" in desc or "USB JTAG/serial" in desc:
            esp32_ports.append(port)
    if len(esp32_ports) != 1:
        details = [f"{p.device} {p.description} {p.hwid}" for p in ports]
        raise RuntimeError(
            "COMx requires exactly one ESP32 serial port; found "
            f"{len(esp32_ports)} ESP32-like port(s). all_ports={details}"
        )
    return esp32_ports[0], ports


def open_camera(requested):
    import cv2

    candidates = []
    if requested and requested.lower() != "auto":
        candidates = [int(requested)]
    else:
        candidates = list(range(0, 8))

    probe = []
    for index in candidates:
        cap = cv2.VideoCapture(index, cv2.CAP_DSHOW)
        opened = bool(cap.isOpened())
        frame_ok = False
        shape = None
        if opened:
            for _ in range(4):
                frame_ok, frame = cap.read()
                if frame_ok:
                    shape = list(frame.shape)
                    break
                time.sleep(0.05)
        probe.append({"index": index, "opened": opened, "frame_ok": bool(frame_ok), "shape": shape})
        if opened and frame_ok:
            return index, cap, probe
        cap.release()
    raise RuntimeError(f"no usable camera found for requested camera={requested}; probe={probe}")


def flash_current_build(port_device, output_dir):
    if not payload.get("flash_before_capture", True):
        return {"skipped": True, "reason": "SkipFlash requested"}
    flash_script = Path(payload["tools_dir"]) / "flash.ps1"
    flash_log = output_dir / "flash-before-calibration.log"
    command = [
        "pwsh",
        "-NoProfile",
        "-File",
        str(flash_script),
        "-Port",
        port_device,
        "-Target",
        "esp32s3",
        "-NoBuild",
    ]
    started = utc_now()
    proc = subprocess.run(
        command,
        cwd=payload["repo_root"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    flash_log.write_text(proc.stdout or "", encoding="utf-8", errors="replace")
    result = {
        "skipped": False,
        "command": " ".join(command),
        "started_at": started,
        "finished_at": utc_now(),
        "exit_code": proc.returncode,
        "log_path": str(flash_log),
    }
    if proc.returncode != 0:
        raise RuntimeError(f"flash before calibration failed with exit code {proc.returncode}; see {flash_log}")
    time.sleep(int(payload.get("post_flash_wait_ms", 3000)) / 1000.0)
    return result


def make_sequence(zones_text):
    zones = [z.strip().lower() for z in zones_text.split(",") if z.strip()]
    allowed = {"status", "key"}
    unknown = [z for z in zones if z not in allowed]
    if unknown:
        raise RuntimeError(f"unsupported zones {unknown}; this harness intentionally supports only status,key")
    if not zones:
        raise RuntimeError("at least one zone is required")

    zone_defs = {
        "status": {"first_led": 1, "count": 6, "labels": ["PWR", "BLE", "REC", "AI", "OK", "WARN"]},
        "key": {"first_led": 11, "count": 4, "labels": ["KEY1", "KEY2", "KEY3", "KEY4"]},
    }
    color_steps = [
        {"name": "red", "command_color": "red", "percent": 25, "expected": "red"},
        {"name": "green", "command_color": "green", "percent": 25, "expected": "green"},
        {"name": "blue", "command_color": "blue", "percent": 25, "expected": "blue"},
        {"name": "white", "command_color": "white", "percent": 25, "expected": "white"},
        {"name": "off", "command_color": "off", "percent": 0, "expected": "off"},
        {"name": "brightness_05", "command_color": "white", "percent": 5, "expected": "white"},
        {"name": "brightness_15", "command_color": "white", "percent": 15, "expected": "white"},
        {"name": "brightness_25", "command_color": "white", "percent": 25, "expected": "white"},
    ]

    sequence = []
    for zone in zones:
        zone_def = zone_defs[zone]
        for local_index in range(zone_def["count"]):
            led = zone_def["first_led"] + local_index
            for color in color_steps:
                sequence.append({
                    "zone": zone,
                    "led": f"LED{led}",
                    "local_index": local_index,
                    "label": zone_def["labels"][local_index],
                    **color,
                })
    return zones, sequence


def send_command(ser, text, readback_ms):
    command = text if text.endswith("\n") else text + "\n"
    ser.reset_input_buffer()
    ser.write(command.encode("utf-8"))
    ser.flush()
    deadline = time.time() + (readback_ms / 1000.0)
    chunks = []
    while time.time() < deadline:
        data = ser.read(4096)
        if data:
            chunks.append(data)
        else:
            time.sleep(0.02)
    output = b"".join(chunks).decode("utf-8", errors="replace")
    return output


def capture_frame(cap, settle_ms):
    time.sleep(settle_ms / 1000.0)
    frame = None
    ok = False
    for _ in range(5):
        ok, frame = cap.read()
        if ok:
            break
        time.sleep(0.05)
    if not ok or frame is None:
        raise RuntimeError("camera frame capture failed after LED command")
    return frame


def frame_metrics(frame):
    import numpy as np

    arr = frame.astype(np.float32)
    bgr_mean = arr.mean(axis=(0, 1))
    bgr_p95 = np.percentile(arr.reshape(-1, 3), 95, axis=0)
    bgr_max = arr.reshape(-1, 3).max(axis=0)
    luma = (0.114 * arr[:, :, 0]) + (0.587 * arr[:, :, 1]) + (0.299 * arr[:, :, 2])
    bright_pixels = int((luma > 80).sum())
    very_bright_pixels = int((luma > 150).sum())
    return {
        "width": int(frame.shape[1]),
        "height": int(frame.shape[0]),
        "mean_bgr": [round(float(x), 3) for x in bgr_mean],
        "p95_bgr": [round(float(x), 3) for x in bgr_p95],
        "max_bgr": [int(x) for x in bgr_max],
        "mean_luma": round(float(luma.mean()), 3),
        "p95_luma": round(float(np.percentile(luma, 95)), 3),
        "max_luma": round(float(luma.max()), 3),
        "bright_pixels_luma_gt_80": bright_pixels,
        "very_bright_pixels_luma_gt_150": very_bright_pixels,
    }


def classify_observation(expected, metrics):
    b, g, r = metrics["p95_bgr"]
    max_luma = metrics["max_luma"]
    p95_luma = metrics["p95_luma"]
    bright_pixels = metrics["bright_pixels_luma_gt_80"]
    if expected == "off":
        if bright_pixels < 50:
            return "PASS", "off frame has few bright pixels"
        return "INCONCLUSIVE", "off frame still has bright pixels; ambient light or LED residual needs review"
    if max_luma < 25 or bright_pixels == 0:
        return "FAIL", "no visible bright response detected"
    if expected == "white":
        channels = [r, g, b]
        spread = max(channels) - min(channels)
        if p95_luma > 30 and spread < 90:
            return "PASS", "white-like response detected"
        return "INCONCLUSIVE", "visible response is not balanced enough to classify white"
    expected_value = {"red": r, "green": g, "blue": b}[expected]
    other_values = [x for name, x in [("red", r), ("green", g), ("blue", b)] if name != expected]
    if expected_value >= max(other_values) + 12:
        return "PASS", f"{expected} is the dominant camera channel"
    return "FAIL", f"{expected} is not the dominant camera channel; possible color-order mismatch or visibility issue"


def main():
    output_dir = Path(payload["output_dir"])
    frames_dir = output_dir / "frames"
    frames_dir.mkdir(parents=True, exist_ok=True)
    transcript_path = output_dir / "serial-transcript.txt"

    if payload["mode"] != EXPECTED_MODE:
        raise RuntimeError(f"unsupported mode {payload['mode']}; expected {EXPECTED_MODE}")

    zones, sequence = make_sequence(payload["zones"])
    manifest_only = bool(payload.get("manifest_only")) or str(payload.get("camera", "")).lower() == "dry-run"
    command_plan = []
    for ordinal, item in enumerate(sequence, start=1):
        percent = int(item["percent"])
        command = (
            f"~LED:TEST:PIXEL {item['zone']} {item['led']} "
            f"{item['command_color']} {percent}"
        )
        command_plan.append({
            "index": ordinal,
            "zone": item["zone"],
            "led": item["led"],
            "local_index": item["local_index"],
            "label": item["label"],
            "color": item["name"],
            "expected": item["expected"],
            "brightness_percent": percent,
            "command": command,
        })
    if manifest_only:
        manifest = {
            "schema_version": 1,
            "plan": PLAN,
            "step_id": STEP,
            "generated_at": utc_now(),
            "updated_at": utc_now(),
            "result": "PASS",
            "dry_run": True,
            "mode": payload["mode"],
            "zones": zones,
            "hardware": {
                "requested_port": payload["port"],
                "requested_camera": payload["camera"],
                "resolved_port": None,
                "camera_index": None,
                "note": "Manifest-only dry run does not require ESP32 serial, OpenCV, or a camera.",
            },
            "manifest_schema": {
                "command_plan": "ordered LED/color commands to send during real capture",
                "sequence_summary": "target LED and color coverage",
                "real_capture_prerequisites": "exact prerequisites before first hardware/camera run",
                "frames": "populated only by real capture mode",
                "per_led_results": "populated only by real capture mode",
            },
            "expected_capture_layout": {
                "frame_dir": str(frames_dir),
                "frame_name_pattern": "NNN_<zone>_<LEDn>_<color>.jpg",
                "transcript_path": str(transcript_path),
                "per_command_frame_count": 1,
            },
            "sequence_summary": {
                "led_count": len({(item["zone"], item["led"]) for item in sequence}),
                "command_count": len(command_plan),
                "target_led_refs": ["LED1..LED6", "LED11..LED14"],
                "colors": ["red", "green", "blue", "white", "off"],
                "brightness_steps": [5, 15, 25],
            },
            "real_capture_prerequisites": [
                "Current unique ESP32 serial resource is discoverable at capture time or passed as -Port <COMn>.",
                "Firmware has been built for esp32s3 and can be flashed, or -SkipFlash is used against already-flashed matching firmware.",
                "A usable camera is discoverable with OpenCV or passed as -Camera <index>.",
                "Camera is positioned close enough to resolve each status LED LED1..LED6 and key LED LED11..LED14 one at a time.",
                "Run the real capture inside aiw with-lock for the current COM resource only during flash/serial/camera capture.",
            ],
            "command_plan": command_plan,
            "frames": [],
            "per_led_results": [],
            "serial_transcript_path": str(transcript_path),
            "exact_failures": [],
        }
        write_manifest(manifest)
        print(
            "status_led_camera_calibration: "
            f"result=PASS dry_run=True commands={len(command_plan)} manifest={output_dir / 'manifest.json'}"
        )
        return 0

    import cv2
    import serial

    port, all_ports = resolve_esp32_port(payload["port"])
    camera_index, cap, camera_probe = open_camera(payload["camera"])

    manifest = {
        "schema_version": 1,
        "plan": PLAN,
        "step_id": STEP,
        "generated_at": utc_now(),
        "result": "INCONCLUSIVE",
        "mode": payload["mode"],
        "zones": zones,
        "hardware": {
            "requested_port": payload["port"],
            "resolved_port": port.device,
            "port_description": port.description,
            "port_hwid": port.hwid,
            "all_serial_ports": [
                {"device": p.device, "description": p.description, "hwid": p.hwid}
                for p in all_ports
            ],
            "requested_camera": payload["camera"],
            "camera_index": camera_index,
            "camera_backend": "opencv_dshow",
            "camera_probe": camera_probe,
        },
        "capture_settings": {
            "baud": payload["baud"],
            "settle_ms": payload["settle_ms"],
            "readback_ms": payload["readback_ms"],
            "post_flash_wait_ms": payload["post_flash_wait_ms"],
            "frame_dir": str(frames_dir),
        },
        "flash": {"pending": True},
        "sequence_summary": {
            "led_count": len({(item["zone"], item["led"]) for item in sequence}),
            "frame_count": len(sequence),
            "target_led_refs": ["LED1..LED6", "LED11..LED14"],
            "colors": ["red", "green", "blue", "white", "off"],
            "brightness_steps": [5, 15, 25],
        },
        "frames": [],
        "per_led_results": [],
        "serial_transcript_path": str(transcript_path),
        "exact_failures": [],
    }

    transcript_lines = []
    try:
        manifest["flash"] = flash_current_build(port.device, output_dir)
        with serial.Serial(
            port=port.device,
            baudrate=int(payload["baud"]),
            timeout=0.08,
            dsrdtr=False,
            rtscts=False,
        ) as ser:
            ser.dtr = False
            ser.rts = False
            startup_commands = [
                "~LED:STATUS",
                "~LED:OFF",
            ]
            for command in startup_commands:
                response = send_command(ser, command, int(payload["readback_ms"]))
                transcript_lines.append(f"> {command}\n{response}")

            for ordinal, item in enumerate(sequence, start=1):
                percent = int(item["percent"])
                command = command_plan[ordinal - 1]["command"]
                response = send_command(ser, command, int(payload["readback_ms"]))
                transcript_lines.append(f"> {command}\n{response}")
                if "invalid args" in response or "unknown" in response.lower() or "requires" in response:
                    item_result = "FAIL"
                    item_note = "firmware rejected calibration command"
                    manifest["exact_failures"].append(f"{item['led']} {item['name']}: {item_note}")
                else:
                    frame = capture_frame(cap, int(payload["settle_ms"]))
                    filename = f"{ordinal:03d}_{item['zone']}_{item['led']}_{item['name']}.jpg"
                    frame_path = frames_dir / filename
                    cv2.imwrite(str(frame_path), frame)
                    metrics = frame_metrics(frame)
                    item_result, item_note = classify_observation(item["expected"], metrics)
                    if item_result == "FAIL":
                        manifest["exact_failures"].append(f"{item['led']} {item['name']}: {item_note}")
                    manifest["frames"].append({
                        "index": ordinal,
                        "zone": item["zone"],
                        "led": item["led"],
                        "local_index": item["local_index"],
                        "label": item["label"],
                        "color": item["name"],
                        "command": command,
                        "path": str(frame_path),
                        "metrics": metrics,
                        "result": item_result,
                        "note": item_note,
                    })

            response = send_command(ser, "~LED:OFF", int(payload["readback_ms"]))
            transcript_lines.append(f"> ~LED:OFF\n{response}")
    finally:
        cap.release()

    transcript_path.write_text("\n".join(transcript_lines), encoding="utf-8")

    by_led = {}
    for frame in manifest["frames"]:
        key = (frame["zone"], frame["led"], frame["label"])
        by_led.setdefault(key, []).append(frame)
    for (zone, led, label), frames in sorted(by_led.items()):
        results = {frame["color"]: frame["result"] for frame in frames}
        if any(result == "FAIL" for result in results.values()):
            result = "FAIL"
        elif any(result == "INCONCLUSIVE" for result in results.values()):
            result = "INCONCLUSIVE"
        else:
            result = "PASS"
        manifest["per_led_results"].append({
            "zone": zone,
            "led": led,
            "label": label,
            "result": result,
            "colors": results,
        })

    if any(row["result"] == "FAIL" for row in manifest["per_led_results"]):
        manifest["result"] = "FAIL"
    elif any(row["result"] == "INCONCLUSIVE" for row in manifest["per_led_results"]):
        manifest["result"] = "INCONCLUSIVE"
    else:
        manifest["result"] = "PASS"
    manifest["frame_count"] = len(manifest["frames"])
    manifest["updated_at"] = utc_now()
    write_manifest(manifest)

    if manifest["frame_count"] != len(sequence):
        fail(f"captured {manifest['frame_count']} frames, expected {len(sequence)}", manifest)

    print(
        "status_led_camera_calibration: "
        f"result={manifest['result']} port={port.device} camera={camera_index} "
        f"frames={manifest['frame_count']} manifest={output_dir / 'manifest.json'}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        output_dir = Path(payload["output_dir"])
        output_dir.mkdir(parents=True, exist_ok=True)
        manifest = {
            "schema_version": 1,
            "plan": PLAN,
            "step_id": STEP,
            "generated_at": utc_now(),
            "updated_at": utc_now(),
            "result": "FAIL",
            "mode": payload.get("mode"),
            "zones": payload.get("zones"),
            "hardware": {
                "requested_port": payload.get("port"),
                "requested_camera": payload.get("camera"),
            },
            "frames": [],
            "per_led_results": [],
            "exact_failures": [str(exc)],
        }
        write_manifest(manifest)
        raise
"@ | & $pythonPath -

if ($LASTEXITCODE -ne 0) {
    throw "status_led_camera_calibration failed with exit code $LASTEXITCODE"
}
