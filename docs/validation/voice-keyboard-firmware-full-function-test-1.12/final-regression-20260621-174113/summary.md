# 1.12 Final Regression 2026-06-21 17:41

Firmware commit: 38bb890
Type commit: be74fcc

PASS:
- Firmware static regression: status LED, BLE Type-ready sync, power manager, charging-awake policy, V2 board profile, device settings, repo feature check, diff check.
- Firmware build: `pwsh -NoProfile -File .\tools\build.ps1`.
- Type regression: embedded BLE tests, device settings tests, `npm run build`, diff check.

Scope:
- LED/Type sync fixes remain covered.
- oai2 plugged low-power setting is merged into Type.
- Firmware idle low-power updates are merged.
- GPIO7 USB_DET is disabled for runtime decisions.
- Release defaults keep USB Serial/JTAG awake while connected; USB light sleep remains opt-in debug profile.

Manual gate still separate:
- 1.12 clean install/OOBE and real-use go/no-go remain human-gated.
