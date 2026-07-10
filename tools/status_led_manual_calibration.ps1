param(
    [string]$Port = "COMx",
    [string]$Zone = "",
    [string]$Led = "",
    [string]$Color = "",
    [string]$Zones = "status,key",
    [string]$OutputDir = ".cache\validation\voice-keyboard-camera-status-key-led-tuning-1.2",
    [int]$Percent = 100,
    [int]$Baud = 115200,
    [int]$ReadbackMs = 500,
    [int]$BootWaitMs = 3000,
    [int]$StepMs = 500,
    [switch]$RunSequence,
    [switch]$ManifestOnly
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDirPath = if ([System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir
} else {
    Join-Path $repoRoot $OutputDir
}
$resolvedOutputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($outputDirPath)
New-Item -ItemType Directory -Force -Path $resolvedOutputDir | Out-Null

$pythonPath = (Get-Command python -ErrorAction Stop).Path
$payload = [ordered]@{
    port = $Port
    zone = $Zone
    led = $Led
    color = $Color
    zones = $Zones
    output_dir = $resolvedOutputDir
    baud = $Baud
    readback_ms = $ReadbackMs
    boot_wait_ms = $BootWaitMs
    step_ms = $StepMs
    percent = $Percent
    run_sequence = $RunSequence.IsPresent
    manifest_only = $ManifestOnly.IsPresent
}
$payloadJson = $payload | ConvertTo-Json -Depth 8 -Compress
$payloadBase64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($payloadJson))

@"
import base64
import json
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

payload = json.loads(base64.b64decode("$payloadBase64").decode("utf-8"))

PLAN = "voice-keyboard-camera-status-key-led-tuning"
STEP = "1.2"
CONTRACT = {
    "status": {
        "data_gpio": 1,
        "led_refs": "LED1..LED6",
        "first_led": 1,
        "led_count": 6,
        "labels": ["PWR", "BLE", "REC", "AI", "OK", "WARN"],
    },
    "key": {
        "data_gpio": 13,
        "led_refs": "LED11..LED14",
        "first_led": 11,
        "led_count": 4,
        "labels": ["KEY1", "KEY2", "KEY3", "KEY4"],
    },
}
COLORS = ["red", "green", "blue", "white", "off"]


def utc_now():
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def output_dir():
    path = Path(payload["output_dir"])
    path.mkdir(parents=True, exist_ok=True)
    return path


def parse_zones():
    zones = [z.strip().lower() for z in str(payload["zones"]).split(",") if z.strip()]
    unknown = [z for z in zones if z not in CONTRACT]
    if unknown:
        raise RuntimeError(f"unsupported manual calibration zones {unknown}; use status,key")
    if not zones:
        raise RuntimeError("at least one zone is required")
    return zones


def led_ref(zone, text):
    contract = CONTRACT[zone]
    raw = str(text).strip()
    cursor = raw[3:] if raw.upper().startswith("LED") else raw
    value = int(cursor)
    first_led = int(contract["first_led"])
    count = int(contract["led_count"])
    if 1 <= value <= count:
        value = first_led + value - 1
    if not (first_led <= value < first_led + count):
        raise RuntimeError(f"{raw} is not in {zone} LED range")
    return f"LED{value}", value - first_led


def make_command_plan():
    plan = []
    index = 1
    for zone in parse_zones():
        contract = CONTRACT[zone]
        for local_index, label in enumerate(contract["labels"]):
            led = f"LED{contract['first_led'] + local_index}"
            for color in COLORS:
                percent = 0 if color == "off" else int(payload["percent"])
                command = f"~LED:TEST:PIXEL {zone} {led} {color} {percent}"
                plan.append({
                    "index": index,
                    "zone": zone,
                    "led": led,
                    "local_index": local_index,
                    "label": label,
                    "data_gpio": contract["data_gpio"],
                    "color": color,
                    "expected": color,
                    "brightness_percent": percent,
                    "command": command,
                })
                index += 1
    return plan


def write_template(command_plan):
    out = output_dir()
    template_json = out / "manual-feedback-template.json"
    template_md = out / "manual-feedback-template.md"
    observations = []
    for row in command_plan:
        observations.append({
            "zone": row["zone"],
            "led": row["led"],
            "label": row["label"],
            "commanded_color": row["color"],
            "observed_color": "",
            "result": "",
            "note": "",
        })
    template_json.write_text(json.dumps({
        "schema_version": 1,
        "plan": PLAN,
        "step_id": STEP,
        "instructions": "Fill observed_color/result/note from human-eye LED feedback. result is PASS when the commanded RGBW/off state matches the observed state.",
        "observations": observations,
    }, indent=2), encoding="utf-8")
    lines = [
        "# Manual status/key LED RGBW/off feedback",
        "",
        "Record one row per command. Use PASS only when the requested RGBW/off state is what the human eye sees.",
        "",
        "| zone | led | label | command | observed_color | result | note |",
        "|---|---|---|---|---|---|---|",
    ]
    for row in command_plan:
        lines.append(
            f"| {row['zone']} | {row['led']} | {row['label']} | `{row['command']}` |  |  |  |"
        )
    template_md.write_text("\n".join(lines) + "\n", encoding="utf-8")
    manifest = {
        "schema_version": 1,
        "plan": PLAN,
        "step_id": STEP,
        "generated_at": utc_now(),
        "updated_at": utc_now(),
        "result": "MANUAL_PENDING",
        "manual_observation": True,
        "status_key_only": True,
        "ec11_edge_untouched": True,
        "command_plan": command_plan,
        "manual_feedback_template_json": str(template_json),
        "manual_feedback_template_md": str(template_md),
    }
    (out / "manual-manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"manual_calibration: result=MANUAL_PENDING commands={len(command_plan)} template={template_md}")


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
    return b"".join(chunks).decode("utf-8", errors="replace")


def drain_serial(ser, readback_ms):
    deadline = time.time() + (readback_ms / 1000.0)
    chunks = []
    while time.time() < deadline:
        data = ser.read(4096)
        if data:
            chunks.append(data)
        else:
            time.sleep(0.02)
    return b"".join(chunks).decode("utf-8", errors="replace")


def looks_like_boot(text):
    lowered = (text or "").lower()
    return "esp-rom:" in lowered or "boot:" in lowered or "rst:" in lowered


def open_serial_without_reset(serial_module, port_device):
    ser = serial_module.Serial()
    ser.port = port_device
    ser.baudrate = int(payload["baud"])
    ser.timeout = 0.08
    ser.dsrdtr = False
    ser.rtscts = False
    ser.dtr = False
    ser.rts = False
    ser.open()
    ser.dtr = False
    ser.rts = False
    return ser


def run_single():
    import serial

    zone = str(payload["zone"]).strip().lower()
    color = str(payload["color"]).strip().lower()
    if zone not in CONTRACT:
        raise RuntimeError("single manual command requires -Zone status|key")
    if color not in COLORS:
        raise RuntimeError("single manual command requires -Color red|green|blue|white|off")
    led, local_index = led_ref(zone, payload["led"])
    percent = 0 if color == "off" else int(payload["percent"])
    command = f"~LED:TEST:PIXEL {zone} {led} {color} {percent}"
    port, ports = resolve_esp32_port(str(payload["port"]))
    out = output_dir()
    record = {
        "at": utc_now(),
        "plan": PLAN,
        "step_id": STEP,
        "manual_observation": True,
        "status_key_only": True,
        "ec11_edge_untouched": True,
        "requested_port": payload["port"],
        "resolved_port": port.device,
        "all_serial_ports": [
            {"device": p.device, "description": p.description, "hwid": p.hwid}
            for p in ports
        ],
        "zone": zone,
        "led": led,
        "local_index": local_index,
        "label": CONTRACT[zone]["labels"][local_index],
        "commanded_color": color,
        "expected_observed_color": color,
        "brightness_percent": percent,
        "command": command,
    }
    with open_serial_without_reset(serial, port.device) as ser:
        initial_output = drain_serial(ser, int(payload["readback_ms"]))
        record["serial_initial_output"] = initial_output
        if looks_like_boot(initial_output):
            time.sleep(int(payload["boot_wait_ms"]) / 1000.0)
            record["serial_initial_after_boot_wait"] = drain_serial(ser, int(payload["readback_ms"]))
        preclear_response = send_command(ser, "~LED:OFF", int(payload["readback_ms"]))
        record["preclear_command"] = "~LED:OFF"
        record["preclear_response"] = preclear_response
        if looks_like_boot(preclear_response):
            time.sleep(int(payload["boot_wait_ms"]) / 1000.0)
            record["preclear_after_boot_wait_response"] = send_command(ser, "~LED:OFF", int(payload["readback_ms"]))
        time.sleep(0.1)
        response = send_command(ser, command, int(payload["readback_ms"]))
        if looks_like_boot(response):
            time.sleep(int(payload["boot_wait_ms"]) / 1000.0)
            record["serial_response_after_boot_wait"] = send_command(ser, command, int(payload["readback_ms"]))
        record["serial_response"] = response
    session_path = out / "manual-session.jsonl"
    with session_path.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(record, ensure_ascii=True) + "\n")
    print(
        "manual_calibration: command_sent "
        f"port={port.device} zone={zone} led={led} color={color} percent={percent} "
        f"session={session_path}"
    )
    print(
        "manual_feedback_request: tell Codex what you see for "
        f"{zone} {led} {record['label']} commanded={color}; expected={color}."
    )


def run_sequence(command_plan):
    import serial

    port, ports = resolve_esp32_port(str(payload["port"]))
    out = output_dir()
    session_path = out / "manual-session.jsonl"
    started_at = utc_now()
    sequence_id = f"{PLAN}-{STEP}-{started_at}"
    records = []
    with open_serial_without_reset(serial, port.device) as ser:
        initial_output = drain_serial(ser, int(payload["readback_ms"]))
        if looks_like_boot(initial_output):
            time.sleep(int(payload["boot_wait_ms"]) / 1000.0)
            initial_output += drain_serial(ser, int(payload["readback_ms"]))
        preclear_response = send_command(ser, "~LED:OFF", int(payload["readback_ms"]))
        if looks_like_boot(preclear_response):
            time.sleep(int(payload["boot_wait_ms"]) / 1000.0)
            preclear_response += send_command(ser, "~LED:OFF", int(payload["readback_ms"]))
        for row in command_plan:
            response = send_command(ser, row["command"], int(payload["readback_ms"]))
            if looks_like_boot(response):
                time.sleep(int(payload["boot_wait_ms"]) / 1000.0)
                response += send_command(ser, row["command"], int(payload["readback_ms"]))
            record = {
                "at": utc_now(),
                "plan": PLAN,
                "step_id": STEP,
                "sequence_id": sequence_id,
                "manual_observation": True,
                "status_key_only": True,
                "ec11_edge_untouched": True,
                "requested_port": payload["port"],
                "resolved_port": port.device,
                "all_serial_ports": [
                    {"device": p.device, "description": p.description, "hwid": p.hwid}
                    for p in ports
                ],
                "sequence_index": row["index"],
                "zone": row["zone"],
                "led": row["led"],
                "local_index": row["local_index"],
                "label": row["label"],
                "commanded_color": row["color"],
                "expected_observed_color": row["expected"],
                "brightness_percent": row["brightness_percent"],
                "command": row["command"],
                "serial_response": response,
            }
            records.append(record)
            print(
                "manual_sequence: "
                f"{row['index']:02d}/{len(command_plan):02d} {row['zone']} {row['led']} "
                f"{row['label']} {row['color']} {row['brightness_percent']}%"
            )
            time.sleep(max(0, int(payload["step_ms"])) / 1000.0)
        final_response = send_command(ser, "~LED:OFF", int(payload["readback_ms"]))

    with session_path.open("a", encoding="utf-8") as handle:
        for record in records:
            handle.write(json.dumps(record, ensure_ascii=True) + "\n")
    summary = {
        "schema_version": 1,
        "plan": PLAN,
        "step_id": STEP,
        "generated_at": started_at,
        "updated_at": utc_now(),
        "result": "MANUAL_SEQUENCE_SENT",
        "manual_observation": True,
        "status_key_only": True,
        "ec11_edge_untouched": True,
        "sequence_id": sequence_id,
        "resolved_port": port.device,
        "commands_sent": len(command_plan),
        "brightness_policy": "active RGBW commands use 100 percent brightness; off uses 0 percent",
        "command_plan": command_plan,
        "session_log": str(session_path),
        "final_clear_command": "~LED:OFF",
        "final_clear_response": final_response,
    }
    (out / "manual-sequence-summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(
        "manual_calibration: sequence_sent "
        f"port={port.device} commands={len(command_plan)} step_ms={payload['step_ms']} "
        f"summary={out / 'manual-sequence-summary.json'}"
    )


def main():
    command_plan = make_command_plan()
    single = bool(str(payload["zone"]).strip() and str(payload["led"]).strip() and str(payload["color"]).strip())
    if payload.get("run_sequence"):
        run_sequence(command_plan)
        return 0
    if payload.get("manifest_only") or not single:
        write_template(command_plan)
        return 0
    run_single()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        out = output_dir()
        failure = {
            "schema_version": 1,
            "plan": PLAN,
            "step_id": STEP,
            "generated_at": utc_now(),
            "updated_at": utc_now(),
            "result": "FAIL",
            "manual_observation": True,
            "exact_failures": [str(exc)],
        }
        (out / "manual-manifest.json").write_text(json.dumps(failure, indent=2), encoding="utf-8")
        raise
"@ | & $pythonPath -

if ($LASTEXITCODE -ne 0) {
    throw "status_led_manual_calibration failed with exit code $LASTEXITCODE"
}
