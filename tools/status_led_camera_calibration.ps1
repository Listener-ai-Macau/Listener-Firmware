param(
    [string]$Port = "COMx",
    [string]$Camera = "auto",
    [string]$Zones = "status,key",
    [string]$Mode = "rgbw-single-led",
    [string]$OutputDir = "docs\validation\voice-keyboard-camera-status-key-led-tuning-1.2",
    [int]$Baud = 115200,
    [int]$SettleMs = 250,
    [int]$ReadbackMs = 350,
    [int]$PostFlashWaitMs = 3000,
    [switch]$ManifestOnly,
    [switch]$SkipFlash,
    [switch]$VerifyMapping
)

$ErrorActionPreference = "Stop"

$defaultOutputDir = "docs\validation\voice-keyboard-camera-status-key-led-tuning-1.2"
if ($Mode.ToLowerInvariant() -eq "semantic-preview" -and $OutputDir -eq $defaultOutputDir) {
    $OutputDir = "docs\validation\voice-keyboard-camera-status-key-led-tuning-1.3"
}

if (-not $ManifestOnly.IsPresent -and $Camera.ToLowerInvariant() -ne "dry-run") {
    . (Join-Path $PSScriptRoot "idf_env.ps1")
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDirPath = if ([System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir
} else {
    Join-Path $repoRoot $OutputDir
}
$resolvedOutputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath(
    $outputDirPath)
New-Item -ItemType Directory -Force -Path $resolvedOutputDir | Out-Null

function Test-PythonModules {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PythonPath,
        [string[]]$Modules = @("cv2", "serial")
    )

    $moduleList = ($Modules | ForEach-Object { "import $_" }) -join "; "
    & $PythonPath -c $moduleList 2>$null
    return $LASTEXITCODE -eq 0
}

function Resolve-CapturePython {
    $candidates = New-Object System.Collections.Generic.List[string]
    $current = (Get-Command python -ErrorAction Stop).Path
    $candidates.Add($current)

    $pyLauncher = Get-Command py -ErrorAction SilentlyContinue
    if ($pyLauncher) {
        $probe = & $pyLauncher.Source -3.11 -c "import sys; print(sys.executable)" 2>$null
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($probe)) {
            $candidates.Add($probe.Trim())
        }
    }

    $systemPython = Join-Path $env:LOCALAPPDATA "Programs\Python\Python311\python.exe"
    if (Test-Path -LiteralPath $systemPython) {
        $candidates.Add($systemPython)
    }

    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (Test-PythonModules -PythonPath $candidate -Modules @("serial")) {
            if ($ManifestOnly.IsPresent -or $Camera.ToLowerInvariant() -eq "dry-run" -or
                (Test-PythonModules -PythonPath $candidate -Modules @("cv2", "serial"))) {
                return $candidate
            }
        }
    }

    throw "No Python interpreter with required camera modules found. Need cv2 and pyserial for real capture."
}

$pythonPath = Resolve-CapturePython
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
    verify_mapping = $VerifyMapping.IsPresent
}
$payloadJson = $payload | ConvertTo-Json -Depth 8 -Compress
$payloadBase64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($payloadJson))

@"
import base64
import json
import math
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

payload = json.loads(base64.b64decode("$payloadBase64").decode("utf-8"))

PLAN = "voice-keyboard-camera-status-key-led-tuning"
STEP_BY_MODE = {
    "rgbw-single-led": "1.2",
    "semantic-preview": "1.3",
}
MODE = payload["mode"].lower()
STEP = STEP_BY_MODE.get(MODE, "unknown")
EXPECTED_MODES = set(STEP_BY_MODE)
STATUS_EFFECT_BASELINE = "status_key_isolated_charge_deep_breath_v12"
PROFILE_CAPS_PERCENT = {
    "low": 100,
    "standard": 100,
    "ambient": 100,
    "factory": 100,
}
FIRMWARE_MAPPING_CONTRACT = {
    "status": {
        "data_gpio": 1,
        "led_refs": "LED1..LED6",
        "first_led": 1,
        "led_count": 6,
        "default_color_order": "GRB",
        "labels": ["PWR", "BLE", "REC", "AI", "OK", "WARN"],
        "physical_map": {
            "LED1": "PWR",
            "LED2": "BLE",
            "LED3": "REC",
            "LED4": "AI",
            "LED5": "OK",
            "LED6": "WARN",
        },
    },
    "key": {
        "data_gpio": 13,
        "led_refs": "LED11..LED14",
        "first_led": 11,
        "led_count": 4,
        "default_color_order": "GRB",
        "labels": ["KEY1", "KEY2", "KEY3", "KEY4"],
        "physical_map": {
            "LED11": "KEY1",
            "LED12": "KEY2",
            "LED13": "KEY3",
            "LED14": "KEY4",
        },
    },
}


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


def markdown_escape(text):
    return str(text).replace("|", "\\|").replace("\n", " ")


def write_status_effects_markdown(manifest):
    if manifest.get("mode") != "semantic-preview":
        return None

    output_dir = Path(payload["output_dir"])
    frames = manifest.get("frames", [])
    profile_rows = manifest.get("profile_cap_observations", [])
    failures = manifest.get("exact_failures", [])
    status_path = output_dir / "status-effects.md"
    lines = [
        "# voice-keyboard-camera-status-key-led-tuning/1.3 status effects evidence",
        "",
        f"- result: {manifest.get('result')}",
        f"- generated_at: {manifest.get('generated_at')}",
        f"- baseline: {STATUS_EFFECT_BASELINE}",
        f"- output_dir: {output_dir}",
        f"- frame_dir: {output_dir / 'frames'}",
        f"- serial_transcript: {manifest.get('serial_transcript_path')}",
        f"- manifest: {output_dir / 'manifest.json'}",
        "",
        "## Baseline Decision",
        "",
        "The current product baseline is preserved in firmware. This run captures semantic previews and records exact residuals; it does not retune brightness or timing constants unless camera/operator evidence names a specific delta.",
        "",
        "## Profile Caps",
        "",
        "| profile | expected cap percent | observed status text |",
        "|---|---:|---|",
    ]
    for row in profile_rows:
        lines.append(
            f"| {markdown_escape(row.get('profile'))} | {row.get('expected_cap_percent')} | {markdown_escape(row.get('status_summary'))} |"
        )
    if not profile_rows:
        lines.append("| none |  | no profile observations recorded |")

    lines.extend([
        "",
        "## Semantic Preview Frames",
        "",
        "| preview | expected LEDs | focus | timing sample ms | result | frame | note |",
        "|---|---|---|---:|---|---|---|",
    ])
    for frame in frames:
        frame_path = Path(frame.get("path", ""))
        try:
            frame_ref = frame_path.relative_to(output_dir.parent.parent.parent)
        except Exception:
            frame_ref = frame_path
        lines.append(
            "| {name} | {leds} | {focus} | {delay} | {result} | {path} | {note} |".format(
                name=markdown_escape(frame.get("name")),
                leds=markdown_escape(",".join(frame.get("expected_leds", []))),
                focus=markdown_escape(frame.get("acceptance_focus")),
                delay=frame.get("sample_delay_ms"),
                result=markdown_escape(frame.get("result")),
                path=markdown_escape(frame_ref),
                note=markdown_escape(frame.get("note")),
            )
        )

    lines.extend([
        "",
        "## Timing And Brightness Decisions",
        "",
        "- PWR/BLE: standard healthy awake preview keeps the low visual-weight v7 baseline; connected preview is steady blue instead of pairing/reconnect blink.",
        "- REC: capture preview is the strongest routine status. rec_not_available preview records WARN + REC instead of active REC alone.",
        "- AI: processing preview includes an initial breath sample and a settled long-processing sample.",
        "- OK: preview captures the short 900 ms confirmation window.",
        "- WARN: retryable and hard previews capture amber/red severity plus source pairing.",
        f"- Routine profile percent caps remain explicit and user-capped: low={PROFILE_CAPS_PERCENT['low']}%, standard={PROFILE_CAPS_PERCENT['standard']}%, ambient={PROFILE_CAPS_PERCENT['ambient']}%, factory={PROFILE_CAPS_PERCENT['factory']}%; current-budget dimming is tracked separately.",
        "",
        "## Remaining Deltas",
        "",
    ])
    if failures:
        for failure in failures:
            lines.append(f"- {failure}")
    else:
        lines.append("- None from this automated semantic preview pass; future changes should name the exact LED/effect/color/timing/brightness delta from the v7 baseline.")

    lines.append("")
    status_path.write_text("\n".join(lines), encoding="utf-8")
    return status_path


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
        zone_def = FIRMWARE_MAPPING_CONTRACT[zone]
        for local_index in range(zone_def["led_count"]):
            led = zone_def["first_led"] + local_index
            for color in color_steps:
                sequence.append({
                    "zone": zone,
                    "led": f"LED{led}",
                    "local_index": local_index,
                    "data_gpio": zone_def["data_gpio"],
                    "firmware_color_order": zone_def["default_color_order"],
                    "label": zone_def["labels"][local_index],
                    **color,
                })
    return zones, sequence


def make_semantic_sequence(zones_text):
    zones = [z.strip().lower() for z in zones_text.split(",") if z.strip()]
    if zones != ["status"]:
        raise RuntimeError("semantic-preview supports only -Zones status for the six semantic LEDs")

    return zones, [
        {
            "name": "standard_ready_connected",
            "label": "PWR+BLE",
            "profile": "standard",
            "commands": ["~LED:PROFILE standard", "~LED:PREVIEW ready"],
            "expected_leds": ["PWR", "BLE"],
            "expected": "healthy awake baseline and connected confidence: low green PWR plus steady blue BLE",
            "acceptance_focus": "PWR/BLE low visual weight in healthy awake state; connected BLE steady blue",
            "sample_delay_ms": 700,
            "state_expect": {"ble": "connected", "battery_level": 80},
        },
        {
            "name": "low_profile_ready",
            "label": "PWR+BLE low profile",
            "profile": "low",
            "commands": ["~LED:PROFILE low", "~LED:PREVIEW ready"],
            "expected_leds": ["PWR", "BLE"],
            "expected": "darker low-profile ready indication, still bounded by safety/status behavior",
            "acceptance_focus": "low profile is explicitly darker than standard",
            "sample_delay_ms": 700,
            "state_expect": {"ble": "connected", "battery_level": 80},
        },
        {
            "name": "ambient_profile_ready",
            "label": "PWR+BLE ambient profile",
            "profile": "ambient",
            "commands": ["~LED:PROFILE ambient", "~LED:PREVIEW ready"],
            "expected_leds": ["PWR", "BLE"],
            "expected": "restrained ambient-ready status rail; no factory calibration brightness",
            "acceptance_focus": "ambient profile is restrained and explicit",
            "sample_delay_ms": 700,
            "state_expect": {"ble": "connected", "battery_level": 80},
        },
        {
            "name": "factory_profile_ready",
            "label": "PWR+BLE factory profile",
            "profile": "factory",
            "commands": ["~LED:PROFILE factory", "~LED:PREVIEW ready"],
            "expected_leds": ["PWR", "BLE"],
            "expected": "factory keeps full-brightness calibration path available without changing product baseline",
            "acceptance_focus": "factory profile keeps 100% calibration cap",
            "sample_delay_ms": 700,
            "state_expect": {"ble": "connected", "battery_level": 80},
        },
        {
            "name": "ble_pairing",
            "label": "BLE pairing",
            "profile": "standard",
            "commands": ["~LED:PROFILE standard", "~LED:PREVIEW clear", "~LED:PREVIEW pairing"],
            "expected_leds": ["BLE"],
            "expected": "blue pairing blink/pulse on LED2 without permanent bright lamp",
            "acceptance_focus": "pairing is visible and diagnostic, not connected steady",
            "sample_delay_ms": 900,
            "sample_count": 5,
            "sample_interval_ms": 260,
            "state_expect": {"ble": "pairing"},
        },
        {
            "name": "ble_reconnect",
            "label": "BLE reconnect",
            "profile": "standard",
            "commands": ["~LED:PROFILE standard", "~LED:PREVIEW clear", "~LED:PREVIEW reconnect"],
            "expected_leds": ["BLE"],
            "expected": "blue reconnect double pulse on LED2",
            "acceptance_focus": "reconnect is visible without becoming a permanent lamp",
            "sample_delay_ms": 0,
            "sample_count": 12,
            "sample_interval_ms": 90,
            "state_expect": {"ble": "reconnecting"},
        },
        {
            "name": "rec_capture",
            "label": "REC active",
            "profile": "standard",
            "commands": ["~LED:PROFILE standard", "~LED:PREVIEW clear", "~LED:PREVIEW capture"],
            "expected_leds": ["REC"],
            "expected": "REC is the strongest routine status while proven capture/upload is active",
            "acceptance_focus": "REC only lights for capture/upload or preview command",
            "sample_delay_ms": 500,
            "state_expect": {"rec_active": 1, "rec_source": "device_mic"},
        },
        {
            "name": "rec_unavailable_warn",
            "label": "WARN + REC unavailable",
            "profile": "standard",
            "commands": ["~LED:PROFILE standard", "~LED:PREVIEW clear", "~LED:PREVIEW rec_not_available"],
            "expected_leds": ["WARN", "REC"],
            "expected": "capture unavailable is represented as WARN plus REC companion, not active REC alone",
            "acceptance_focus": "unavailable capture maps to WARN + REC",
            "sample_delay_ms": 220,
            "sample_count": 6,
            "sample_interval_ms": 180,
            "state_expect": {"rec_active": 0, "rec_source": "not_available", "error_domain": "recording"},
        },
        {
            "name": "ai_processing",
            "label": "AI processing",
            "profile": "standard",
            "commands": ["~LED:PROFILE standard", "~LED:PREVIEW clear", "~LED:PREVIEW processing"],
            "expected_leds": ["AI"],
            "expected": "AI uses calm violet breathing during processing",
            "acceptance_focus": "AI timing is camera-measured and settles for long processing",
            "sample_delay_ms": 900,
            "state_expect": {"processing": 1},
        },
        {
            "name": "ai_processing_settled",
            "label": "AI processing settled",
            "profile": "standard",
            "commands": ["~LED:PROFILE standard", "~LED:PREVIEW clear", "~LED:PREVIEW processing"],
            "expected_leds": ["AI"],
            "expected": "long processing sample is calmer than initial active processing",
            "acceptance_focus": "AI long processing settles rather than escalating indefinitely",
            "sample_delay_ms": 10800,
            "state_expect": {"processing": 1},
        },
        {
            "name": "ok_success",
            "label": "OK success",
            "profile": "standard",
            "commands": ["~LED:PROFILE standard", "~LED:PREVIEW clear", "~LED:PREVIEW ok"],
            "expected_leds": ["OK"],
            "expected": "OK is a short green confirmation with 900 ms total hold/fade",
            "acceptance_focus": "OK timing is short confirmation",
            "sample_delay_ms": 120,
            "state_expect": {},
        },
        {
            "name": "warn_retryable",
            "label": "WARN retryable",
            "profile": "standard",
            "commands": ["~LED:PROFILE standard", "~LED:PREVIEW clear", "~LED:ERROR ai retryable"],
            "expected_leds": ["WARN", "AI"],
            "expected": "retryable warning uses amber WARN and source pairing",
            "acceptance_focus": "WARN amber severity with source pairing",
            "sample_delay_ms": 220,
            "sample_count": 7,
            "sample_interval_ms": 180,
            "state_expect": {"error_domain": "ai", "error_severity": "retryable"},
        },
        {
            "name": "warn_hard",
            "label": "WARN hard",
            "profile": "standard",
            "commands": ["~LED:PROFILE standard", "~LED:PREVIEW clear", "~LED:ERROR system hard"],
            "expected_leds": ["WARN", "PWR"],
            "expected": "hard warning uses red WARN and source pairing",
            "acceptance_focus": "WARN red hard severity with source pairing",
            "sample_delay_ms": 160,
            "sample_count": 8,
            "sample_interval_ms": 120,
            "state_expect": {"error_domain": "system", "error_severity": "hard"},
        },
        {
            "name": "charging",
            "label": "PWR charging",
            "profile": "standard",
            "commands": ["~LED:PROFILE standard", "~LED:PREVIEW clear", "~LED:PREVIEW charging"],
            "expected_leds": ["PWR"],
            "expected": "PWR high-contrast white breath for charging",
            "acceptance_focus": "charging visibly breathes without reading as steady full-charge white",
            "sample_delay_ms": 900,
            "state_expect": {"charging": 1},
        },
        {
            "name": "low_battery",
            "label": "PWR low battery",
            "profile": "standard",
            "commands": ["~LED:PROFILE standard", "~LED:PREVIEW clear", "~LED:PREVIEW low_battery"],
            "expected_leds": ["PWR"],
            "expected": "PWR red low-battery pulse",
            "acceptance_focus": "low battery is visible and source-specific",
            "sample_delay_ms": 520,
            "sample_count": 8,
            "sample_interval_ms": 300,
            "state_expect": {"battery_level": 15},
        },
        {
            "name": "critical_battery",
            "label": "PWR critical battery",
            "profile": "standard",
            "commands": ["~LED:PROFILE standard", "~LED:PREVIEW clear", "~LED:PREVIEW critical_battery"],
            "expected_leds": ["PWR"],
            "expected": "PWR critical battery double pulse",
            "acceptance_focus": "critical battery escalates visibly",
            "sample_delay_ms": 160,
            "sample_count": 10,
            "sample_interval_ms": 160,
            "state_expect": {"battery_level": 5},
        },
    ]


def is_semantic_mode():
    return MODE == "semantic-preview"


def build_mapping_verification(manifest):
    required_colors = {"red", "green", "blue", "white"}
    expected_leds = {
        zone: set(contract["physical_map"].keys())
        for zone, contract in FIRMWARE_MAPPING_CONTRACT.items()
        if zone in manifest.get("zones", [])
    }
    frames = manifest.get("frames", [])
    residuals = []
    by_zone_led_color = {}
    for frame in frames:
        key = (frame.get("zone"), frame.get("led"), frame.get("color"))
        by_zone_led_color[key] = frame

    per_strip = {}
    for zone, leds in expected_leds.items():
        strip_residuals = []
        for led in sorted(leds, key=lambda value: int(value[3:])):
            for color in sorted(required_colors):
                frame = by_zone_led_color.get((zone, led, color))
                if frame is None:
                    strip_residuals.append(f"{zone} {led} {color}: missing frame")
                    continue
                if frame.get("result") != "PASS":
                    strip_residuals.append(
                        f"{zone} {led} {color}: {frame.get('result')} {frame.get('note')}"
                    )
        per_strip[zone] = {
            "data_gpio": FIRMWARE_MAPPING_CONTRACT[zone]["data_gpio"],
            "led_refs": FIRMWARE_MAPPING_CONTRACT[zone]["led_refs"],
            "default_color_order": FIRMWARE_MAPPING_CONTRACT[zone]["default_color_order"],
            "separate_color_order_supported": True,
            "required_colors": sorted(required_colors),
            "result": "PASS" if not strip_residuals else "FAIL",
            "residuals": strip_residuals,
        }
        residuals.extend(strip_residuals)

    return {
        "result": "PASS" if not residuals else "FAIL",
        "status_key_only": True,
        "ec11_edge_untouched": True,
        "firmware_mapping_contract": FIRMWARE_MAPPING_CONTRACT,
        "per_strip": per_strip,
        "residuals": residuals,
    }


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


def open_serial_no_reset(serial_module, port_device, baudrate, timeout=0.08):
    ser = serial_module.Serial()
    ser.port = port_device
    ser.baudrate = int(baudrate)
    ser.timeout = timeout
    ser.dsrdtr = False
    ser.rtscts = False
    ser.dtr = False
    ser.rts = False
    ser.open()
    ser.dtr = False
    ser.rts = False
    return ser


def parse_led_status(text):
    status_line = ""
    for line in text.splitlines():
        if "~LED:STATUS" in line:
            status_line = line.strip()
    result = {
        "raw": status_line,
        "profile": None,
        "profile_cap_percent": None,
        "ble": None,
        "rec_active": None,
        "rec_source": None,
        "processing": None,
        "error_domain": None,
        "error_severity": None,
        "battery_level": None,
        "charging": None,
        "full": None,
        "active_flags": {},
    }
    if not status_line:
        return result
    for key in ["profile", "ble", "rec_source", "error_domain", "error_severity"]:
        match = re.search(rf"(?:^|\s){key}=([^\s]+)", status_line)
        if match:
            result[key] = match.group(1)
    for key in ["profile_cap_percent", "rec_active", "processing", "battery_level", "charging", "full"]:
        match = re.search(rf"(?:^|\s){key}=([0-9]+)", status_line)
        if match:
            result[key] = int(match.group(1))
    flags = re.search(r"active_flags=([^\s]+)", status_line)
    if flags:
        for item in flags.group(1).split(","):
            if ":" not in item:
                continue
            name, value = item.split(":", 1)
            result["active_flags"][name] = value == "1"
    return result


def status_matches_expect(status, expected):
    mismatches = []
    for key, expected_value in (expected or {}).items():
        if status.get(key) != expected_value:
            mismatches.append(f"{key}: expected {expected_value} observed {status.get(key)}")
    return mismatches


def semantic_status_matches(status, expected):
    return bool(status.get("raw")) and not status_matches_expect(status, expected)


def capture_semantic_samples(cap, ser, item, ordinal, frames_dir):
    import cv2

    sample_count = int(item.get("sample_count", 1))
    sample_interval_ms = int(item.get("sample_interval_ms", 0))
    sample_frames = []
    best_metrics = None
    best_path = None
    best_luma = -1
    best_status = None
    first_status = None
    last_status = None
    expected_state = item.get("state_expect", {})
    for sample_index in range(sample_count):
        if sample_index == 0:
            time.sleep(int(item["sample_delay_ms"]) / 1000.0)
        elif sample_interval_ms > 0:
            time.sleep(sample_interval_ms / 1000.0)
        status_response = send_command(ser, "~LED:STATUS", int(payload["readback_ms"]))
        status = parse_led_status(status_response)
        if first_status is None:
            first_status = status
        last_status = status
        frame = capture_frame(cap, int(payload["settle_ms"]))
        suffix = "" if sample_count == 1 else f"_{sample_index + 1:02d}"
        filename = f"{ordinal:03d}_{item['name']}{suffix}.jpg"
        frame_path = frames_dir / filename
        cv2.imwrite(str(frame_path), frame)
        metrics = frame_metrics(frame)
        expected_match = semantic_status_matches(status, expected_state)
        prefer_this_frame = False
        if expected_match and not semantic_status_matches(best_status or {}, expected_state):
            prefer_this_frame = True
        elif expected_match == semantic_status_matches(best_status or {}, expected_state) and metrics["max_luma"] > best_luma:
            prefer_this_frame = True
        if prefer_this_frame:
            best_luma = metrics["max_luma"]
            best_metrics = metrics
            best_path = frame_path
            best_status = status
        sample_frames.append({
            "sample": sample_index + 1,
            "path": str(frame_path),
            "metrics": metrics,
            "status": status,
            "active_flags": status.get("active_flags", {}),
            "status_raw": status.get("raw"),
            "state_matches_expect": expected_match,
        })
    return {
        "path": str(best_path),
        "metrics": best_metrics,
        "status": best_status or last_status or {},
        "first_status": first_status or {},
        "last_status": last_status or {},
        "sample_frames": sample_frames,
    }


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

    if MODE not in EXPECTED_MODES:
        raise RuntimeError(f"unsupported mode {payload['mode']}; expected one of {sorted(EXPECTED_MODES)}")

    if is_semantic_mode():
        zones, sequence = make_semantic_sequence(payload["zones"])
        manifest_only = bool(payload.get("manifest_only")) or str(payload.get("camera", "")).lower() == "dry-run"
        command_plan = []
        for ordinal, item in enumerate(sequence, start=1):
            command_plan.append({
                "index": ordinal,
                "name": item["name"],
                "label": item["label"],
                "profile": item["profile"],
                "commands": item["commands"],
                "expected_leds": item["expected_leds"],
                "expected": item["expected"],
                "acceptance_focus": item["acceptance_focus"],
                "sample_delay_ms": item["sample_delay_ms"],
                "sample_count": item.get("sample_count", 1),
                "sample_interval_ms": item.get("sample_interval_ms", 0),
                "state_expect": item.get("state_expect", {}),
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
                "baseline": STATUS_EFFECT_BASELINE,
                "profile_caps_percent": PROFILE_CAPS_PERCENT,
                "hardware": {
                    "requested_port": payload["port"],
                    "requested_camera": payload["camera"],
                    "resolved_port": None,
                    "camera_index": None,
                    "note": "Manifest-only semantic preview does not require ESP32 serial, OpenCV, or a camera.",
                },
                "manifest_schema": {
                    "command_plan": "ordered semantic preview commands to send during real capture",
                    "frames": "populated by real capture with status frame metrics and serial status",
                    "profile_cap_observations": "profile cap values parsed from ~LED:STATUS",
                    "status_effects_markdown": "human-readable evidence summary for review",
                },
                "expected_capture_layout": {
                    "frame_dir": str(frames_dir),
                    "frame_name_pattern": "NNN_<semantic-name>.jpg",
                    "transcript_path": str(transcript_path),
                    "per_preview_frame_count": 1,
                },
                "sequence_summary": {
                    "preview_count": len(command_plan),
                    "target_led_refs": ["LED1=PWR", "LED2=BLE", "LED3=REC", "LED4=AI", "LED5=OK", "LED6=WARN"],
                    "profiles": sorted(PROFILE_CAPS_PERCENT.keys()),
                    "baseline_preserved": True,
                },
                "real_capture_prerequisites": [
                    "Current unique ESP32 serial resource is discoverable at capture time or passed as -Port <COMn>.",
                    "Firmware has been built for esp32s3 and can be flashed, or -SkipFlash is used against already-flashed matching firmware.",
                    "A usable camera is discoverable with OpenCV or passed as -Camera <index>.",
                    "Camera is positioned to see status LEDs LED1..LED6 together.",
                    "Run real semantic preview inside aiw with-lock for the current COM resource only during flash/serial/camera capture.",
                ],
                "command_plan": command_plan,
                "frames": [],
                "profile_cap_observations": [],
                "serial_transcript_path": str(transcript_path),
                "status_effects_markdown": str(output_dir / "status-effects.md"),
                "exact_failures": [],
            }
            write_manifest(manifest)
            write_status_effects_markdown(manifest)
            print(
                "status_led_camera_calibration: "
                f"result=PASS dry_run=True mode=semantic-preview previews={len(command_plan)} manifest={output_dir / 'manifest.json'}"
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
            "baseline": STATUS_EFFECT_BASELINE,
            "profile_caps_percent": PROFILE_CAPS_PERCENT,
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
                "preview_count": len(sequence),
                "target_led_refs": ["LED1=PWR", "LED2=BLE", "LED3=REC", "LED4=AI", "LED5=OK", "LED6=WARN"],
                "profiles": sorted(PROFILE_CAPS_PERCENT.keys()),
                "baseline_preserved": True,
            },
            "command_plan": command_plan,
            "frames": [],
            "profile_cap_observations": [],
            "serial_transcript_path": str(transcript_path),
            "status_effects_markdown": str(output_dir / "status-effects.md"),
            "exact_failures": [],
        }

        transcript_lines = []
        try:
            manifest["flash"] = flash_current_build(port.device, output_dir)
            with open_serial_no_reset(serial, port.device, payload["baud"]) as ser:
                for command in ["~LED:STATUS", "~LED:OFF"]:
                    response = send_command(ser, command, int(payload["readback_ms"]))
                    transcript_lines.append(f"> {command}\n{response}")

                for ordinal, item in enumerate(sequence, start=1):
                    response_bundle = []
                    for command in item["commands"]:
                        response = send_command(ser, command, int(payload["readback_ms"]))
                        response_bundle.append(f"> {command}\n{response}")
                        transcript_lines.append(response_bundle[-1])
                    capture = capture_semantic_samples(cap, ser, item, ordinal, frames_dir)
                    for sample in capture["sample_frames"]:
                        transcript_lines.append(f"> ~LED:STATUS sample={item['name']}#{sample['sample']}\n{sample.get('status_raw') or ''}")
                    status = capture["status"]
                    if item["profile"] in PROFILE_CAPS_PERCENT:
                        manifest["profile_cap_observations"].append({
                            "preview": item["name"],
                            "profile": item["profile"],
                            "expected_cap_percent": PROFILE_CAPS_PERCENT[item["profile"]],
                            "observed_cap_percent": status.get("profile_cap_percent"),
                            "status_summary": status.get("raw"),
                        })
                    metrics = capture["metrics"]
                    frame_path = capture["path"]
                    sample_frames = capture["sample_frames"]
                    active_seen = {
                        led: any(sample.get("active_flags", {}).get(led, False) for sample in sample_frames)
                        for led in item["expected_leds"]
                    }
                    missing_leds = [led for led, seen in active_seen.items() if not seen]
                    state_expect = item.get("state_expect", {})
                    state_matched = any(sample.get("state_matches_expect", False) for sample in sample_frames)
                    state_mismatches = [] if state_matched else status_matches_expect(status, state_expect)
                    cap_ok = True
                    if item["profile"] in PROFILE_CAPS_PERCENT and status.get("profile_cap_percent") != PROFILE_CAPS_PERCENT[item["profile"]]:
                        cap_ok = False
                    status_ok = bool(status.get("raw"))
                    if not status_ok:
                        result = "FAIL"
                        note = "missing ~LED:STATUS response after preview"
                    elif state_mismatches:
                        result = "FAIL"
                        note = "state mismatch: " + "; ".join(state_mismatches)
                    elif not cap_ok:
                        result = "FAIL"
                        note = f"profile cap mismatch: expected {PROFILE_CAPS_PERCENT[item['profile']]} observed {status.get('profile_cap_percent')}"
                    elif missing_leds and item["name"] not in {"low_profile_ready", "ambient_profile_ready", "factory_profile_ready"}:
                        result = "INCONCLUSIVE"
                        note = "semantic state matched but sample window did not catch active phase for: " + ",".join(missing_leds)
                    elif metrics["max_luma"] < 20 and item["name"] != "low_profile_ready":
                        result = "INCONCLUSIVE"
                        note = "camera frame was dark; serial status is present but visual classification needs review"
                    else:
                        result = "PASS"
                        note = "serial semantic state and camera sample window captured for preview"
                    if result == "FAIL":
                        manifest["exact_failures"].append(f"{item['name']}: {note}")
                    manifest["frames"].append({
                        "index": ordinal,
                        "name": item["name"],
                        "label": item["label"],
                        "profile": item["profile"],
                        "commands": item["commands"],
                        "expected_leds": item["expected_leds"],
                        "expected": item["expected"],
                        "acceptance_focus": item["acceptance_focus"],
                        "sample_delay_ms": item["sample_delay_ms"],
                        "sample_count": item.get("sample_count", 1),
                        "sample_interval_ms": item.get("sample_interval_ms", 0),
                        "state_expect": item.get("state_expect", {}),
                        "path": str(frame_path),
                        "metrics": metrics,
                        "status": status,
                        "sample_frames": sample_frames,
                        "active_seen": active_seen,
                        "result": result,
                        "note": note,
                    })

                response = send_command(ser, "~LED:PROFILE standard", int(payload["readback_ms"]))
                transcript_lines.append(f"> ~LED:PROFILE standard\n{response}")
                response = send_command(ser, "~LED:OFF", int(payload["readback_ms"]))
                transcript_lines.append(f"> ~LED:OFF\n{response}")
        finally:
            cap.release()

        transcript_path.write_text("\n".join(transcript_lines), encoding="utf-8")
        if any(frame["result"] == "FAIL" for frame in manifest["frames"]):
            manifest["result"] = "FAIL"
        elif any(frame["result"] == "INCONCLUSIVE" for frame in manifest["frames"]):
            manifest["result"] = "INCONCLUSIVE"
        else:
            manifest["result"] = "PASS"
        manifest["frame_count"] = len(manifest["frames"])
        manifest["updated_at"] = utc_now()
        write_status_effects_markdown(manifest)
        write_manifest(manifest)
        if manifest["frame_count"] != len(sequence):
            fail(f"captured {manifest['frame_count']} frames, expected {len(sequence)}", manifest)
        print(
            "status_led_camera_calibration: "
            f"result={manifest['result']} mode=semantic-preview port={port.device} camera={camera_index} "
            f"frames={manifest['frame_count']} manifest={output_dir / 'manifest.json'}"
        )
        if manifest["result"] != "PASS":
            return 1
        return 0

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
            "data_gpio": item["data_gpio"],
            "firmware_color_order": item["firmware_color_order"],
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
            "verify_mapping": bool(payload.get("verify_mapping")),
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
                "mapping_verification": "populated by -VerifyMapping real capture; records color-order and physical-map residuals",
            },
            "firmware_mapping_contract": FIRMWARE_MAPPING_CONTRACT,
            "mapping_verification": {
                "result": "RECORDED_ONLY",
                "reason": "manifest-only dry run cannot verify camera-visible mapping",
                "status_key_only": True,
                "ec11_edge_untouched": True,
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
        "verify_mapping": bool(payload.get("verify_mapping")),
        "mode": payload["mode"],
        "zones": zones,
        "firmware_mapping_contract": FIRMWARE_MAPPING_CONTRACT,
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
        with open_serial_no_reset(serial, port.device, payload["baud"]) as ser:
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
                        "data_gpio": item["data_gpio"],
                        "firmware_color_order": item["firmware_color_order"],
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
    if payload.get("verify_mapping"):
        manifest["mapping_verification"] = build_mapping_verification(manifest)
        manifest["result"] = manifest["mapping_verification"]["result"]
    manifest["frame_count"] = len(manifest["frames"])
    manifest["updated_at"] = utc_now()
    write_manifest(manifest)

    if manifest["frame_count"] != len(sequence):
        fail(f"captured {manifest['frame_count']} frames, expected {len(sequence)}", manifest)
    if payload.get("verify_mapping") and manifest["mapping_verification"]["result"] != "PASS":
        residuals = manifest["mapping_verification"].get("residuals", [])
        fail("VerifyMapping residuals: " + "; ".join(residuals[:8]), manifest)

    print(
        "status_led_camera_calibration: "
        f"result={manifest['result']} port={port.device} camera={camera_index} "
        f"frames={manifest['frame_count']} manifest={output_dir / 'manifest.json'}"
    )
    if manifest["result"] != "PASS":
        return 1
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
            "verify_mapping": bool(payload.get("verify_mapping")),
            "firmware_mapping_contract": FIRMWARE_MAPPING_CONTRACT,
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
