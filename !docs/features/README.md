# Features

This directory stores concise feature and subsystem summaries for future AI and human recovery.

## Purpose

After important work is completed, add a document here so a future AI can quickly understand:

- what exists
- where the code lives
- how to verify it
- what constraints matter

This directory is the preferred long-term landing place for completed important work.

When an active plan in `!docs/plans/` has been fully executed, move the stable results here as a concise summary, then remove the completed plan unless there is a clear reason to keep it.

If a feature or subsystem is no longer on the current main path but still worth remembering, keep it here and mark its status clearly, for example:

- currently used
- currently not used
- fallback only
- historical background

## Suggested Contents

- feature summary
- key file locations
- build or runtime verification steps
- integration assumptions
- known risks or future work
- current usage status

## Naming

Use descriptive `snake_case` filenames such as:

- `ble_hid_bringup.md`
- `esp32_toolchain_setup.md`
