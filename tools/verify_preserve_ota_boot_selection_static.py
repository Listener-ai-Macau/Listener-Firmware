from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
FLASH_SCRIPT = ROOT / "tools" / "flash.ps1"


def require(pattern: str, description: str, text: str) -> None:
    if not re.search(pattern, text, re.DOTALL):
        raise AssertionError(f"missing {description}")


def main() -> int:
    text = FLASH_SCRIPT.read_text(encoding="utf-8")

    require(r"function\s+Get-OtaBootPartition", "OTA boot-slot probe", text)
    require(
        r"Global\\Listener_\$SerialPortName",
        "per-port Global mutex for OTA serial queries and flash actions",
        text,
    )
    require(r'Command\s+"~OTA:STATUS"', "OTA STATUS probe command", text)
    require(r"boot=\(ota_\[01\]\)", "strict ota_0/ota_1 status parser", text)
    require(r"function\s+Invoke-OtaBootInactiveForPreservedFlash", "preserved-flash boot handoff", text)
    require(r'Command\s+"~OTA:TEST_BOOT_INACTIVE"', "OTA boot handoff command", text)
    require(
        r'if\s*\(\$PreserveOtaData\.IsPresent\)\s*\{[\s\S]*?\$bootPartitionBeforeFlash\s*=\s*Get-OtaBootPartition[\s\S]*?"bootloader-flash",\s*"partition-table-flash",\s*"app-flash"',
        "pre-flash slot check before preserved app flash",
        text,
    )
    require(
        r'if\s*\(\$bootPartitionBeforeFlash\s*-eq\s*"ota_1"\)\s*\{[\s\S]*?Invoke-OtaBootInactiveForPreservedFlash',
        "ota_1 to ota_0 handoff after preserved app flash",
        text,
    )
    require(
        r'\$bootPartitionAfterFlash\s*=\s*Get-OtaBootPartition[\s\S]*?\$bootPartitionAfterFlash\s*-ne\s*"ota_0"',
        "post-flash ota_0 verification",
        text,
    )
    if re.search(r"(?i)\berase_flash\b|\berase-flash\b|nvs_flash_erase|nvs_erase_|DEVICE:RESET|device_settings", text):
        raise AssertionError("preserved flash must not erase device settings")

    print("verify_preserve_ota_boot_selection_static: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"verify_preserve_ota_boot_selection_static: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
