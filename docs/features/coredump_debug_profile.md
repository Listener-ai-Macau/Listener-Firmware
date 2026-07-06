# Core Dump Debug Profile

This profile is for Codex-side crash triage. It does not change the default
production firmware image or partition table.

## Evidence Command

Use the repository wrapper so the ESP-IDF Python environment is always exported:

```powershell
pwsh -NoProfile -File .\tools\collect_coredump_debug.ps1 -Port COM3 -AllowNoCoreDump
```

For a saved UART/base64 core dump body:

```powershell
pwsh -NoProfile -File .\tools\collect_coredump_debug.ps1 -CoreFile .\path\core.txt
```

The script writes `summary.json`, stdout, and stderr under
`.artifacts\coredump-debug\<timestamp>`. A crash investigation is actionable only
when the summary reports `PASS` with backtrace/task/register evidence, or
`NO_COREDUMP` when explicitly allowed by a smoke check.

## Source Basis

ESP-IDF core dump analysis is handled through `idf.py coredump-info` and
`idf.py coredump-debug`; those commands are wrapped by `tools\idf.ps1` here so
agents do not accidentally run a non-exported shell.

The production image can stay coredump-disabled unless a debug build profile is
explicitly requested. If a future bench profile enables flash coredumps, it must
add a reviewed `data,coredump` partition and keep release package checks from
publishing that profile as production firmware.
