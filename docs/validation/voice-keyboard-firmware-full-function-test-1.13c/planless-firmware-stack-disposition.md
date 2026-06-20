# Planless Firmware Stack Disposition

Plan: `voice-keyboard-firmware-full-function-test`
Step: `1.13c`
Date: 2026-06-20
Owner: `oai1`

## Current Release Candidate

The firmware master worktree intended for the next ship smoke is:

- Repo: `C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware-pwr-shutdown-new-board`
- Branch: `master`
- HEAD: `dd05f2a59679bf9f464b38c4cac718b571c19c4e`
- Origin: `origin/master` at `e97b17b`, local master is ahead by 3 local commits.
- Working tree: clean.
- COM10 board ledger firmware under test: `62cc0e124caa6e07ffbab816c667da2858d8ee7a`

This disposition does not merge or delete any planless branch. It records what may be used for the next ship smoke and what must remain excluded unless it goes through a named workflow path.

## Evidence Commands

- `aiw doctor -RepoRoot C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware-pwr-shutdown-new-board`
- `git status --short --branch`
- `git worktree list --porcelain`
- `git for-each-ref refs/heads` plus `git rev-list master...<branch>` and `git merge-base --is-ancestor <branch> master`
- `aiw worktrees -RepoRoot C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware-pwr-shutdown-new-board`
- `aiw worktrees -RepoRoot C:\Users\Billy\Desktop\Denzic\Listener\Listener-Type`

## Active Worktrees

| Path | Branch or state | Dirty | Ahead/behind master | Disposition |
|---|---:|---:|---:|---|
| `Listener-Firmware-pwr-shutdown-new-board` | `master` | no | master is ahead of origin by 3 | Use as the 1.13b/next ship-smoke candidate after human approval. |
| `Listener-Firmware` | `adhoc/oai2/low-power-battery-sampling` | no | ahead 1 / behind 19 | Preserve as local scratch; exclude from ship smoke unless split into a power/battery workflow step. |
| `Listener-Firmware-vkfft-1.6-oai2-clean` | detached validation worktree | yes | n/a | Preserve until any useful evidence is copied or checkpointed; do not ship from this worktree. |
| `Listener-Firmware-wt-adhoc-codex-open-source-prune` | `adhoc/codex/open-source-prune` | no | ahead 1 / behind 2 | Preserve or review separately; exclude from ship smoke because it is unrelated release hygiene. |
| `Listener-Firmware-wt-adhoc-oai1-hw-rev-io9-io10-no-current-sense` | `adhoc/oai1/hw-rev-io9-io10-no-current-sense` | yes, 79 paths | ahead 5 / behind 37 | Preserve and split into a hardware-revision branch_required step if still desired; exclude from current ship smoke. |

Listener-Type worktree audit reported no AI worktrees or orphan worktree directories.

## Required Planless Stack Coverage

| Stack named by 1.13c | Observed local state | Disposition |
|---|---|---|
| `adhoc/oai1/pwr-hold-shutdown-failsafe` | No active branch with this exact name. Related local scratch exists as `human/scratch/voice-keyboard-firmware/pwr-hold-shutdown-failsafe-dirty` at `29b4b0a`, ahead 7 / behind 60. | Preserve as historical scratch only. Do not include in 1.12/ship smoke without a new power-off workflow step. |
| `adhoc/oai1/hw-rev-io9-io10-no-current-sense` | Active dirty worktree, ahead 5 / behind 37, 79 dirty paths. Dirty sample: `components/battery_monitor/battery_monitor.c`, `components/board/board.c`, `components/device_settings/CMakeLists.txt`. | Preserve. Needs a dedicated hardware-revision/current-sense step before any product merge. |
| `adhoc/oai1/ble-recovery-stable-identity` | Local branch at `aed7320`, ahead 14 / behind 61, no active worktree. | Preserve or split into a BLE identity/recovery step. Exclude from ship smoke. |
| `adhoc/oai1/shutdown-trace-flash-guard` | Local branch at `de7a813`, ahead 4 / behind 61, no active worktree. | Preserve as diagnostics scratch. Exclude unless split into a diagnostics/flash-guard workflow step. |
| `adhoc/oai2/low-power-battery-sampling` | Active clean worktree at `Listener-Firmware`, ahead 1 / behind 19. | Preserve as local scratch. Exclude from ship smoke unless explicitly routed through a power/battery workflow step. |
| LED stacks handled by `1.13a`/`1.13b` | `adhoc/oai1/led-true-state-accents` and `adhoc/oai1/status-led-recording-effect` are merged into master. `adhoc/oai1/led-flicker-guard-shutdown-confirm` remains ahead 5 / behind 18 but is superseded by accepted 1.13a/1.13b master work. `adhoc/oai3/rec-ai-ok-led-redesign` remains ahead 2 / behind 100 and is superseded by the DMA status rail design. | Use only master commits through `dd05f2a` for ship smoke. Preserve old LED branches as scratch until human approves cleanup; do not fold them silently. |

## Other Non-Default Branch Groups

| Group | Branches | Disposition |
|---|---|---|
| Merged local ad-hoc branches | `adhoc/codex/device-power-split`, `adhoc/oai1/ble-flash-diagnostics`, `adhoc/oai1/ble-gatt-probe-datareader`, `adhoc/oai1/led-true-state-accents`, `adhoc/oai1/pwr-hold-force-high`, `adhoc/oai1/restore-ok-green-pulse`, `adhoc/oai1/status-led-recording-effect`, `adhoc/oai2/ble-central-owned-params`, `adhoc/oai2/ble-repair-cache-fix`, `feature/voice-keyboard-firmware-full-function-test` | Safe cleanup candidates after human approval; they are already ancestors of current master. |
| Unmerged AI step branches | `ai/oai1-firmware-power-off-replace-deep-sleep-1.2`, `ai/oai3-firmware-charging-awake-policy-1.2` | Do not delete automatically. They correspond to accepted/cancelled historical workflow state and need separate handoff/finalize decisions. |
| Unmerged preserve branches | `aiw/preserve/voice-keyboard-firmware/...` | Keep as local preservation snapshots. They are not release sources and should not be merged without a named workflow step. |
| Human scratch branches | `human/scratch/listener-firmware/...`, `human/scratch/voice-keyboard-firmware/...` | Keep as local checkpoint evidence. They are not release sources and should not be deleted or merged silently. |
| Other unmerged ad-hoc branches | `adhoc/codex/open-source-prune`, `adhoc/oai1/ble-recovery-stable-identity`, `adhoc/oai1/led-flicker-guard-shutdown-confirm`, `adhoc/oai1/shutdown-trace-flash-guard`, `adhoc/oai2/low-power-battery-sampling`, `adhoc/oai3/rec-ai-ok-led-redesign`, `adhoc/tai1/invalid-serial-idle-fix` | Preserve and exclude from ship smoke unless the human chooses a follow-up workflow step. |

## Release Decision Proposed For Human Approval

1. Proceed toward the next ship smoke from firmware `master` at `dd05f2a59679bf9f464b38c4cac718b571c19c4e`.
2. Do not rely on any unmerged planless ad-hoc, preserve, or human scratch branch for 1.12/ship-smoke behavior.
3. Keep dirty worktrees and unmerged branches intact until their owner/product disposition is explicitly approved.
4. After approval, cleanup may start only with merged clean branches and clean disposable worktrees. Dirty or unmerged stacks require a checkpoint or a named follow-up step before deletion.

## Human Approval Slot

Pending human/product owner decision:

- Approve this exclusion matrix for the next ship smoke.
- Optionally approve cleanup of merged clean local branches listed above.
- Optionally choose follow-up workflow steps for the preserved dirty/unmerged stacks.
