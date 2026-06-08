[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [string]$BluetoothAddress = "",
    [string]$OutputDir = "docs\validation\voice-keyboard-status-led-1.2",
    [string]$Target = "esp32s3",
    [int]$Baud = 115200,
    [int]$CameraIndex = 0,
    [switch]$NoFlash,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir = Join-Path $repoRoot $OutputDir
}
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$flashLog = Join-Path $OutputDir "flash.log"
$serialLog = Join-Path $OutputDir "status-led-serial.log"
$manifestPath = Join-Path $OutputDir "status-led-visual-manifest.json"
$summaryPath = Join-Path $OutputDir "hardware-led-validation.md"

function Invoke-LoggedNative {
    param(
        [Parameter(Mandatory = $true)][string]$LogPath,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @()
    )

    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $FilePath @Arguments 2>&1 | Tee-Object -FilePath $LogPath
        $exit = if ($null -ne $LASTEXITCODE) { [int]$LASTEXITCODE } elseif ($?) { 0 } else { 1 }
        if ($exit -ne 0) {
            throw "Command failed with exit code $exit; see $LogPath"
        }
    } finally {
        $ErrorActionPreference = $oldPreference
    }
}

if (-not $NoFlash) {
    $flashArgs = @(
        "-NoProfile",
        "-File",
        (Join-Path $PSScriptRoot "flash.ps1"),
        "-Port",
        $Port,
        "-Target",
        $Target
    )
    if ($NoBuild) {
        $flashArgs += "-NoBuild"
    }
    Invoke-LoggedNative -LogPath $flashLog -FilePath "pwsh" -Arguments $flashArgs
} else {
    "Skipped flash because -NoFlash was set." | Set-Content -LiteralPath $flashLog -Encoding UTF8
}

$env:VK_STATUS_LED_PORT = $Port
$env:VK_STATUS_LED_BAUD = [string]$Baud
$env:VK_STATUS_LED_OUTPUT_DIR = $OutputDir
$env:VK_STATUS_LED_CAMERA_INDEX = [string]$CameraIndex
$env:VK_STATUS_LED_BLUETOOTH_ADDRESS = $BluetoothAddress

@'
from __future__ import annotations

import json
import os
import statistics
import sys
import time
from pathlib import Path

import cv2
import serial


port = os.environ["VK_STATUS_LED_PORT"]
baud = int(os.environ.get("VK_STATUS_LED_BAUD", "115200"))
out_dir = Path(os.environ["VK_STATUS_LED_OUTPUT_DIR"])
camera_index = int(os.environ.get("VK_STATUS_LED_CAMERA_INDEX", "0"))
bluetooth_address = os.environ.get("VK_STATUS_LED_BLUETOOTH_ADDRESS", "")

serial_log_path = out_dir / "status-led-serial.log"
manifest_path = out_dir / "status-led-visual-manifest.json"
summary_path = out_dir / "hardware-led-validation.md"

captures: list[dict] = []
commands: list[dict] = []
serial_chunks: list[str] = []


def append_serial(text: str) -> None:
    if text:
        serial_chunks.append(text)


def image_metrics(frame) -> dict:
    height, width = frame.shape[:2]
    small = cv2.resize(frame, (max(1, width // 4), max(1, height // 4)))
    b, g, r = cv2.split(small)
    max_channel = cv2.max(cv2.max(r, g), b)
    bright = int((max_channel > 70).sum())
    very_bright = int((max_channel > 140).sum())
    redish = int(((r > 80) & (r > g * 1.25) & (r > b * 1.25)).sum())
    greenish = int(((g > 80) & (g > r * 1.20) & (g > b * 1.20)).sum())
    blueish = int(((b > 80) & (b > r * 1.20) & (b > g * 1.20)).sum())
    means = [float(x) for x in cv2.mean(frame)[:3]]
    return {
        "width": int(width),
        "height": int(height),
        "mean_bgr": means,
        "max_bgr": [int(frame[:, :, idx].max()) for idx in range(3)],
        "bright_sample_pixels": bright,
        "very_bright_sample_pixels": very_bright,
        "redish_sample_pixels": redish,
        "greenish_sample_pixels": greenish,
        "blueish_sample_pixels": blueish,
        "sample_pixel_count": int(small.shape[0] * small.shape[1]),
    }


def capture(cap, label: str) -> None:
    frame = None
    ok = False
    for _ in range(6):
        ok, frame = cap.read()
        time.sleep(0.04)
    if not ok or frame is None:
        raise RuntimeError(f"camera {camera_index} did not return a frame for {label}")
    path = out_dir / f"{label}.jpg"
    if not cv2.imwrite(str(path), frame):
        raise RuntimeError(f"failed to write image {path}")
    record = {"label": label, "path": str(path), "relative_path": path.name}
    record.update(image_metrics(frame))
    captures.append(record)


def drain(ser: serial.Serial, duration: float) -> str:
    end = time.time() + duration
    chunks: list[bytes] = []
    while time.time() < end:
        data = ser.read(4096)
        if data:
            chunks.append(data)
    text = b"".join(chunks).decode("utf-8", errors="replace")
    append_serial(text)
    return text


def send(ser: serial.Serial, text: str, settle: float = 0.35) -> str:
    started = time.time()
    ser.write(text.encode("utf-8"))
    ser.flush()
    output = drain(ser, settle)
    commands.append({
        "command": text.strip(),
        "settle_seconds": settle,
        "duration_seconds": round(time.time() - started, 3),
        "serial_output_chars": len(output),
    })
    return output


def run() -> int:
    out_dir.mkdir(parents=True, exist_ok=True)
    cap = cv2.VideoCapture(camera_index, cv2.CAP_DSHOW)
    if not cap.isOpened():
        raise RuntimeError(f"camera index {camera_index} could not be opened")
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 0.2
    ser.dsrdtr = False
    ser.rtscts = False
    ser.dtr = True
    ser.rts = False
    ser.open()
    try:
        append_serial(f"# status LED hardware validation on {port} baud={baud}\n")
        drain(ser, 5.0)
        capture(cap, "camera-baseline")

        for command in [
            "~LED:STATUS\n",
            "~LED:BUDGET\n",
            "~LED:PRIVACY\n",
            "~BOARD:STATUS\n",
            "~LED:PROFILE factory\n",
        ]:
            send(ser, command, 0.8)

        send(ser, "~LED:TEST:RGBW all\n", 0.25)
        for label, delay in [
            ("rgbw-red-phase", 0.25),
            ("rgbw-green-phase", 1.00),
            ("rgbw-blue-phase", 1.00),
            ("rgbw-white-phase", 1.00),
        ]:
            time.sleep(delay)
            capture(cap, label)
        send(ser, "~LED:STATUS\n", 0.4)

        for group, count in [("status", 6), ("ec11", 12), ("key", 4), ("edge", 6)]:
            send(ser, f"~LED:TEST:MAP {group}\n", 0.25)
            for index in range(1, count + 1):
                time.sleep(0.62)
                capture(cap, f"map-{group}-{index:02d}")
            send(ser, "~LED:STATUS\n", 0.35)

        for state in [
            "ready",
            "pairing",
            "capture",
            "processing",
            "rec_not_available",
            "ok",
            "low_battery",
            "sleep",
        ]:
            send(ser, f"~LED:PREVIEW {state}\n", 0.45)
            capture(cap, f"preview-{state}")
            send(ser, "~LED:STATUS\n", 0.3)

        send(ser, "~LED:ERROR ota hard\n", 0.45)
        capture(cap, "preview-error-ota-hard")
        send(ser, "~LED:STATUS\n", 0.3)
        send(ser, "~LED:OFF\n", 0.45)
        capture(cap, "preview-off")
        send(ser, "~LED:STATUS\n", 0.3)
    finally:
        try:
            ser.close()
        finally:
            cap.release()

    serial_text = "".join(serial_chunks)
    serial_log_path.write_text(serial_text, encoding="utf-8")

    required_tokens = [
        "~LED:STATUS profile=",
        "semantic_order=LED1:PWR,LED2:BLE,LED3:REC,LED4:AI,LED5:OK,LED6:WARN",
        "strips=status:gpio1:count6",
        "ec11:gpio5:count12",
        "key:gpio13:count4",
        "edge:gpio4:count6",
        "gpio14_reserved=BAT_CHG_IO",
        "~LED:BUDGET profile=",
        "~LED:PRIVACY rec_allowed=",
        "~LED:STATUS group=status",
        "~LED:STATUS group=ec11",
        "~LED:STATUS group=key",
        "~LED:STATUS group=edge",
    ]
    missing_tokens = [token for token in required_tokens if token not in serial_text]
    image_count = len(captures)
    bright_counts = [int(item["bright_sample_pixels"]) for item in captures]
    visual_quality = {
        "image_count": image_count,
        "max_bright_sample_pixels": max(bright_counts) if bright_counts else 0,
        "median_bright_sample_pixels": statistics.median(bright_counts) if bright_counts else 0,
        "camera_opened": True,
        "has_rgbw_phase_images": all(
            any(item["label"] == label for item in captures)
            for label in ["rgbw-red-phase", "rgbw-green-phase", "rgbw-blue-phase", "rgbw-white-phase"]
        ),
        "has_all_map_sequences": all(
            any(item["label"] == label for item in captures)
            for label in ["map-status-06", "map-ec11-12", "map-key-04", "map-edge-06"]
        ),
    }
    result = "PASS" if not missing_tokens and image_count >= 43 else "FAIL"

    manifest = {
        "schema_version": 1,
        "generated_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "port": port,
        "baud": baud,
        "bluetooth_address": bluetooth_address,
        "camera_index": camera_index,
        "serial_log": str(serial_log_path),
        "commands": commands,
        "captures": captures,
        "serial_required_tokens_missing": missing_tokens,
        "visual_quality": visual_quality,
        "result": result,
    }
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    summary = [
        "# voice-keyboard-status-led/1.2 hardware LED validation",
        "",
        f"- Result: `{result}`",
        f"- Port: `{port}`",
        f"- BLE resource: `{bluetooth_address or 'not-provided'}`",
        f"- Serial log: `{serial_log_path.name}`",
        f"- Manifest: `{manifest_path.name}`",
        f"- Captured images: `{image_count}`",
        f"- Required serial tokens missing: `{len(missing_tokens)}`",
        "",
        "The locked run exercises `~LED:STATUS`, `~LED:BUDGET`, `~LED:PRIVACY`, `~BOARD:STATUS`,",
        "`~LED:TEST:RGBW all`, `~LED:TEST:MAP status/ec11/key/edge`, major `~LED:PREVIEW` semantic",
        "states, `~LED:ERROR ota hard`, and `~LED:OFF`.",
        "",
        "Representative images:",
        "",
    ]
    for label in [
        "rgbw-red-phase",
        "rgbw-green-phase",
        "rgbw-blue-phase",
        "rgbw-white-phase",
        "map-status-01",
        "map-ec11-01",
        "map-key-01",
        "map-edge-01",
        "preview-ready",
        "preview-rec_not_available",
        "preview-off",
    ]:
        match = next((item for item in captures if item["label"] == label), None)
        if match:
            summary.append(f"- `{label}`: `{match['relative_path']}`")
    if missing_tokens:
        summary.extend(["", "Missing required serial tokens:"])
        summary.extend([f"- `{token}`" for token in missing_tokens])
    summary_path.write_text("\n".join(summary) + "\n", encoding="utf-8")

    print(json.dumps(manifest, indent=2))
    return 0 if result == "PASS" else 1


if __name__ == "__main__":
    try:
        raise SystemExit(run())
    except Exception as exc:
        error = {
            "schema_version": 1,
            "result": "FAIL",
            "port": port,
            "bluetooth_address": bluetooth_address,
            "camera_index": camera_index,
            "error": str(exc),
            "captures": captures,
            "commands": commands,
        }
        manifest_path.write_text(json.dumps(error, indent=2), encoding="utf-8")
        if serial_chunks:
            serial_log_path.write_text("".join(serial_chunks), encoding="utf-8")
        print(json.dumps(error, indent=2), file=sys.stderr)
        raise
'@ | python -

if ($LASTEXITCODE -ne 0) {
    throw "Status LED hardware validation failed; see $manifestPath"
}

Write-Output "Status LED hardware validation artifacts:"
Write-Output "  summary: $summaryPath"
Write-Output "  manifest: $manifestPath"
Write-Output "  serial: $serialLog"
