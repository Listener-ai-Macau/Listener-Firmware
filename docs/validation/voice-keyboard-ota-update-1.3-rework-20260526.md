# Voice Keyboard OTA Update 1.3 Rework Validation

Date: 2026-05-26

Branch: `ai/oai-voice-keyboard-ota-update-1.3`

## Scope

Rework for review findings on `voice-keyboard-ota-update/1.3`: `tools/check_ota_manifest.ps1` accepted schema v2 manifests that omitted required `created_at_utc`, `ble_identity`, `rollback.method`, `rollback.instructions`, and `recovery` fields. A later review found that `tools/package_ota_firmware.ps1` still accepted dirty/dev version strings for stable/beta channels and treated factory package generation failures as non-fatal.

## Validation

- `pwsh -NoProfile -File .\tools\test_ota_manifest_validation.ps1`
  - PASS: valid manifest accepted.
  - PASS: missing `created_at_utc` rejected.
  - PASS: missing `ble_identity` rejected.
  - PASS: missing `rollback.method` rejected.
  - PASS: missing `rollback.instructions` rejected.
  - PASS: rollback object with only `supported=true` rejected.
  - PASS: missing `recovery` rejected.
  - PASS: missing `recovery.factory_reflash` rejected.
  - PASS: missing `recovery.serial_commands` rejected.
- `pwsh -NoProfile -File .\tools\test_ota_package_release_rules.ps1`
  - PASS: stable package rejected `project_version=review-dirty`.
  - PASS: beta package rejected `project_version=0.1.0-dev`.
  - PASS: stable package rejected missing factory artifacts as a hard failure.
  - PASS: stable package generated `firmware_ota.bin`, `ota_manifest.json`,
    and a complete nested factory package.
  - PASS: beta package generated `firmware_ota.bin`, `ota_manifest.json`, and a
    complete nested factory package.
- `pwsh -NoProfile -File .\tools\package_ota_firmware.ps1 -BuildDir C:\Users\Billy\Desktop\listener\voice-keyboard-firmware\build -OutputRoot .\.cache\ota_package_stable_dirty_check -Channel stable`
  - PASS: rejected dirty tree as expected.
- After committing the rework, `pwsh -NoProfile -File .\tools\package_ota_firmware.ps1 -BuildDir C:\Users\Billy\Desktop\listener\voice-keyboard-firmware\build -OutputRoot .\.cache\ota_package_stable_postcommit -Channel stable`
  - PASS: generated stable OTA package with `Git dirty: False`.
  - PASS: generated `ota_manifest.json` passed `tools\check_ota_manifest.ps1`.
  - PASS: manifest firmware hash matched the source build binary.
- `pwsh -NoProfile -File .\tools\package_ota_firmware.ps1 -BuildDir C:\Users\Billy\Desktop\listener\voice-keyboard-firmware\build -OutputRoot .\.cache\ota_package_repro_smoke -Channel internal-test`
  - PASS: generated `firmware_ota.bin`, `ota_manifest.json`, and nested factory package under ignored `.cache`.
  - PASS: generated manifest passed `tools\check_ota_manifest.ps1`.
  - PASS: two generated package runs reported identical OTA SHA256 `0409b1a90c14e2651b3a0adae378a1d6fc89cc1065d776d1a26c16139d9f2bdd` and size `632576`, matching the source build binary.
- `pwsh -NoProfile -File .\tools\ai\repo_features.ps1 -Check`
  - PASS.
- `python -m compileall -q tools`
  - PASS.
- PowerShell parser check for `tools\check_ota_manifest.ps1`, `tools\package_ota_firmware.ps1`, `tools\test_ota_manifest_validation.ps1`, and `tools\test_ota_package_release_rules.ps1`
  - PASS.
- `git diff --check`
  - PASS.

## Notes

The package smoke used the existing local firmware build at `C:\Users\Billy\Desktop\listener\voice-keyboard-firmware\build` to avoid rebuilding unrelated firmware code during this manifest-validator rework. Generated package artifacts stayed under `.cache`, which is ignored by the firmware repo.
