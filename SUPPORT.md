# Support

Use GitHub Issues for reproducible Listener Firmware defects and feature requests. For speech recognition, final text, writing style, cursor insertion, wake phrase, or voiceprint decisions, report the issue in [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type/issues). Use this repository when the failure concerns capture, BLE transport, physical controls, LEDs, battery, power, settings, diagnostics, or OTA.

## A useful firmware report

Include:

- firmware version or build identifier and matching Listener Type version
- hardware revision, power source, and battery state
- BLE connection state and whether audio, HID, settings, or OTA was affected
- exact knob/key action and LED sequence
- steps to reproduce, expected behavior, and actual behavior
- a short, redacted serial excerpt or diagnostic bundle when available

For audio transport defects, include session timing and packet/drop counters. For OTA defects, include package name, reported stage, previous/new versions, and rollback result. Do not upload firmware packages that are not approved for distribution.

## Protect private data

Remove device identifiers, manufacturing data, credentials, recordings, transcripts, local paths, and signing material. Review diagnostic exports before upload.

Report security vulnerabilities privately through GitHub Security Advisories as described in [SECURITY.md](SECURITY.md).
