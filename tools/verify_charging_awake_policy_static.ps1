param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"

function Read-RepoFile {
    param([string]$RelativePath)
    $path = Join-Path $RepoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing required file: $RelativePath"
    }
    return Get-Content -LiteralPath $path -Raw
}

function Assert-Contains {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$Label
    )
    if ($Text -notmatch $Pattern) {
        throw "Missing $Label ($Pattern)"
    }
}

$powerManager = Read-RepoFile "components/power_manager/power_manager.c"
$powerHeader = Read-RepoFile "components/power_manager/include/power_manager.h"
$diagEvents = Read-RepoFile "components/diag_log/include/diag_log_events.h"
$cmake = Read-RepoFile "components/power_manager/CMakeLists.txt"

Assert-Contains $powerHeader 'POWER_MANAGER_BLOCKER_EXTERNAL_POWER\s*=\s*1u\s*<<\s*7' 'external power sleep-only blocker bit'
Assert-Contains $powerHeader 'sleep_blockers' 'snapshot sleep blocker field'
Assert-Contains $powerHeader 'usb_det_level' 'raw USB detect level in snapshot'
Assert-Contains $powerHeader 'bat_chg_level' 'raw charger level in snapshot'
Assert-Contains $powerHeader 'bat_std_level' 'raw charge-full level in snapshot'
Assert-Contains $powerHeader 'external_power_present' 'interpreted external power status'
Assert-Contains $powerHeader 'usb_power_present' 'interpreted USB power status'
Assert-Contains $powerHeader 'charging' 'interpreted charging status'
Assert-Contains $powerHeader 'charge_full' 'interpreted charge-full status'
Assert-Contains $powerHeader 'automatic_sleep_blocked_by_external_power' 'observable automatic sleep block status'

Assert-Contains $cmake 'REQUIRES\s+battery_monitor\s+board\s+diag_log\s+board_pins\s+watchdog_platform' 'board dependency for power input snapshot'

Assert-Contains $powerManager '#include "board\.h"' 'board power input include'
Assert-Contains $powerManager 'board_get_v2_power_input_snapshot\(&board_snapshot\)' 'board power input snapshot read'
Assert-Contains $powerManager '\.usb_power_present\s*=\s*board_snapshot\.usb_det_level\s*>\s*0' 'USB_Det interpreted state'
Assert-Contains $powerManager '\.charging\s*=\s*board_snapshot\.bat_chg_level\s*==\s*0' 'active-low charging interpretation'
Assert-Contains $powerManager '\.charge_full\s*=\s*board_snapshot\.bat_std_level\s*==\s*0' 'active-low charge-full interpretation'
Assert-Contains $powerManager 'out_source->external_power_present\s*=\s*[\s\S]*out_source->usb_power_present[\s\S]*out_source->charging[\s\S]*out_source->charge_full' 'external power derived from USB/charging/full'

Assert-Contains $powerManager 's_power_source_initialized\s*&&\s*external_changed[\s\S]*s_last_user_activity_ms\s*=\s*now_ms[\s\S]*s_last_radio_activity_ms\s*=\s*now_ms' 'plug/unplug idle reset'
Assert-Contains $powerManager 'reason\s*==\s*POWER_MANAGER_SLEEP_REASON_OVERNIGHT_IDLE[\s\S]*source->external_power_present[\s\S]*sleep_blockers\s*\|=\s*POWER_MANAGER_BLOCKER_EXTERNAL_POWER' 'external power blocks only automatic overnight sleep'
Assert-Contains $powerManager 'return\s+s_external_power_present\s*\?\s*power_manager_awake_idle_state_locked\(radio_idle_ms\)\s*:\s*POWER_MANAGER_STATE_OVERNIGHT_SLEEP' 'automatic overnight sleep falls back to awake idle when powered'
Assert-Contains $powerManager 'power_manager_awake_idle_state_locked[\s\S]*POWER_MANAGER_STATE_CONNECTED_IDLE[\s\S]*POWER_MANAGER_STATE_DISCONNECTED_IDLE' 'plugged low-power awake idle remains reachable'
Assert-Contains $powerManager 'strcmp\(command,\s*"SLEEP"\)[\s\S]*power_manager_enter_sleep\(POWER_MANAGER_SLEEP_REASON_MANUAL_COMMAND\)' 'manual debug sleep command remains explicit'
Assert-Contains $powerManager 'final_sleep_blockers\s*=\s*power_manager_sleep_blockers_for_source\([\s\S]*reason\)' 'final sleep gate rechecks power source'

Assert-Contains $powerManager '~POWER:STATUS[\s\S]*sleep_blockers=0x%08[\s\S]*external_power_present=%u[\s\S]*usb_power_present=%u[\s\S]*charging=%u[\s\S]*charge_full=%u[\s\S]*usb_det_level=%s[\s\S]*bat_chg_level=%s[\s\S]*bat_std_level=%s' 'POWER:STATUS raw and interpreted charging fields'
Assert-Contains $powerManager 'DIAG_POWER_SLEEP_BLOCKED[\s\S]*POWER_MANAGER_SLEEP_REASON_OVERNIGHT_IDLE[\s\S]*power_manager_encode_power_source_flags\(&power_source,\s*true\)' 'automatic sleep block diag with interpreted power-source flags'
Assert-Contains $powerManager 'DIAG_POWER_EXTERNAL_POWER' 'dedicated external-power diag event'
Assert-Contains $diagEvents 'DIAG_POWER_EXTERNAL_POWER\s+9\s+/\*\s*a1=flags,\s*a2=raw_levels,\s*a3=idle_ms,\s*a4=sleep_blockers\s+\*/' 'external-power diag event contract'

if ($powerManager -match 's_blockers\s*\|=\s*POWER_MANAGER_BLOCKER_EXTERNAL_POWER') {
    throw "External power must not be added to s_blockers; it must remain a sleep-only blocker so plugged idle states stay reachable."
}

Write-Host "PASS: charging-awake policy static checks cover raw/interpreted power inputs, automatic sleep blocking, manual sleep override, plug/unplug idle reset, and diagnostics."
