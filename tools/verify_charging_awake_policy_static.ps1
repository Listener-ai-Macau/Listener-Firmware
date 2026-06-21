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

Assert-Contains $powerHeader 'POWER_MANAGER_BLOCKER_EXTERNAL_POWER\s*=\s*1u\s*<<\s*7' 'external power blocker bit'
Assert-Contains $powerHeader 'shutdown_blockers' 'snapshot shutdown blocker field'
Assert-Contains $powerHeader 'usb_det_level' 'raw USB detect level in snapshot'
Assert-Contains $powerHeader 'bat_chg_level' 'raw charger level in snapshot'
Assert-Contains $powerHeader 'bat_std_level' 'raw charge-full level in snapshot'
Assert-Contains $powerHeader 'external_power_present' 'interpreted external power status'
Assert-Contains $powerHeader 'usb_power_present' 'interpreted USB power status'
Assert-Contains $powerHeader 'charging' 'interpreted charging status'
Assert-Contains $powerHeader 'charge_full' 'interpreted charge-full status'
Assert-Contains $powerHeader 'automatic_shutdown_blocked_by_external_power' 'observable automatic shutdown block status'

Assert-Contains $cmake 'REQUIRES\s+battery_monitor\s+board\s+device_settings\s+diag_log\s+board_pins\s+watchdog_platform' 'board and device settings dependency for power input snapshot and configurable timeout'

Assert-Contains $powerManager '#include "board\.h"' 'board power input include'
Assert-Contains $powerManager 'board_get_v2_power_input_snapshot\(&board_snapshot\)' 'board power input snapshot read'
Assert-Contains $powerManager 'bool\s+usb_power_present\s*=\s*board_snapshot\.usb_det_level\s*>\s*0' 'USB_Det interpreted state'
Assert-Contains $powerManager 'bool\s+raw_charging\s*=\s*board_snapshot\.bat_chg_level\s*==\s*0' 'active-low charging fallback interpretation'
Assert-Contains $powerManager 'bool\s+external_power_present\s*=\s*usb_power_present\s*\|\|\s*raw_charging' 'external power follows USB_Det or active charging'
Assert-Contains $powerManager '\.usb_power_present\s*=\s*usb_power_present' 'USB_Det copied into power source snapshot'
Assert-Contains $powerManager '\.external_power_present\s*=\s*external_power_present' 'external power copied into power source snapshot'
Assert-Contains $powerManager '\.charging\s*=\s*raw_charging' 'active-low charging interpretation'
Assert-Contains $powerManager '\.charge_full\s*=\s*usb_power_present\s*&&\s*board_snapshot\.bat_std_level\s*==\s*0' 'USB-gated raw charge-full observation before debounce'
Assert-Contains $powerManager 'POWER_MANAGER_CHARGE_FULL_DEBOUNCE_MS\s+10000U' 'charge-full debounce window'
Assert-Contains $powerManager 'POWER_MANAGER_CHARGE_FULL_MIN_MV\s+4050U' 'charge-full minimum voltage guard'
Assert-Contains $powerManager 'POWER_MANAGER_CHARGE_FULL_MIN_PERCENT\s+88U' 'charge-full minimum percent guard'
Assert-Contains $powerManager 'power_manager_apply_charge_state_filter_locked' 'central charge-state filter'
Assert-Contains $powerManager 'raw_full\s*&&[\s\S]*!raw_charging\s*&&[\s\S]*power_manager_charge_full_battery_allowed' 'charge-full candidate requires raw full, not charging, and near-full battery'
Assert-Contains $powerManager 'source->charge_full\s*=\s*source->usb_power_present\s*&&\s*s_charge_full_latched' 'interpreted charge-full uses USB-gated debounce latch'
Assert-Contains $powerManager 'state_changed[\s\S]*s_charge_full\s*!=\s*source->charge_full[\s\S]*raw_status_changed' 'raw charger pin changes are tracked separately from interpreted power-state changes'
Assert-Contains $powerManager 'Treat active CHG as external power[\s\S]*quiet USB_DET divider[\s\S]*Do not use STD/full by itself' 'active charging fallback preserves battery-only full shutdown'

Assert-Contains $powerManager 's_power_source_initialized\s*&&\s*state_changed\s*&&\s*external_changed[\s\S]*s_last_user_activity_ms\s*=\s*now_ms[\s\S]*s_last_radio_activity_ms\s*=\s*now_ms' 'plug/unplug idle reset'
Assert-Contains $powerManager 'reason\s*==\s*POWER_MANAGER_SHUTDOWN_REASON_LONG_IDLE[\s\S]*source->external_power_present[\s\S]*shutdown_blockers\s*\|=\s*POWER_MANAGER_BLOCKER_EXTERNAL_POWER' 'external power blocks automatic long-idle hardware shutdown'
Assert-Contains $powerManager 'reason\s*==\s*POWER_MANAGER_SHUTDOWN_REASON_LOW_BATTERY[\s\S]*source->usb_power_present[\s\S]*source->external_power_present[\s\S]*source->charging[\s\S]*source->charge_full[\s\S]*shutdown_blockers\s*\|=\s*POWER_MANAGER_BLOCKER_EXTERNAL_POWER' 'USB/charging/full blocks automatic low-battery hardware shutdown'
Assert-Contains $powerManager 'POWER_MANAGER_LOW_BATTERY_SHUTDOWN_MAX_MV\s+2800U' 'critical low-battery shutdown requires product-empty voltage'
Assert-Contains $powerManager 'POWER_MANAGER_LOW_BATTERY_CONFIRM_MS\s+5000U' 'critical low-battery shutdown requires a continuous low-battery confirmation window'
Assert-Contains $powerManager 'POWER_MANAGER_LOW_BATTERY_BOOT_GRACE_MS\s+15000U' 'critical low-battery shutdown has boot grace against startup transients'
Assert-Contains $powerManager 'power_manager_low_battery_shutdown_confirmed_locked[\s\S]*battery_snapshot->battery_level_percent\s*>\s*POWER_MANAGER_BATTERY_CRITICAL_PERCENT[\s\S]*power_manager_low_battery_shutdown_allowed\(source\)[\s\S]*s_low_battery_critical_since_ms[\s\S]*POWER_MANAGER_LOW_BATTERY_CONFIRM_MS[\s\S]*power_manager_user_idle_ms_locked\(now_ms\)' 'critical low-battery shutdown uses explicit battery-only allow gate, continuous confirmation, and startup-transient guard'
Assert-Contains $powerManager 'low_battery_shutdown_confirmed[\s\S]*power_manager_enter_hardware_shutdown\(POWER_MANAGER_SHUTDOWN_REASON_LOW_BATTERY\)' 'critical low-battery shutdown uses confirmed helper result'
Assert-Contains $powerManager 'source->external_power_present[\s\S]*s_blockers\s*\|=\s*POWER_MANAGER_BLOCKER_EXTERNAL_POWER[\s\S]*s_blockers\s*&=\s*~\(uint32_t\)POWER_MANAGER_BLOCKER_EXTERNAL_POWER' 'external power sets and clears blocker'
Assert-Contains $powerManager 'power_manager_awake_blockers\(s_blockers\)\s*!=\s*0[\s\S]*return\s+POWER_MANAGER_STATE_ACTIVE' 'external power does not block connected/disconnected awake idle'
Assert-Contains $powerManager 'power_manager_audio_idle_blockers\(uint32_t blockers\)[\s\S]*return\s+power_manager_awake_blockers\(blockers\)' 'audio idle uses same non-external blocker mask'
Assert-Contains $powerManager 'power_manager_without_external_power_blocker\(s_blockers\)\s*==\s*0[\s\S]*s_external_power_present[\s\S]*hardware_shutdown_ms' 'automatic shutdown blocked status ignores external blocker itself'
Assert-Contains $powerManager 'reason\s*==\s*POWER_MANAGER_SHUTDOWN_REASON_LONG_IDLE[\s\S]*\?\s*snapshot\.blockers[\s\S]*:\s*power_manager_without_external_power_blocker\(snapshot\.blockers\)' 'manual shutdown entry gate ignores external-power awake blocker'
Assert-Contains $powerManager 'strcmp\(command,\s*"SHUTDOWN"\)[\s\S]*power_manager_enter_hardware_shutdown\(POWER_MANAGER_SHUTDOWN_REASON_MANUAL_COMMAND\)' 'manual hardware shutdown command remains explicit'
Assert-Contains $powerManager 'final_shutdown_blockers\s*=\s*power_manager_shutdown_blockers_for_source\([\s\S]*reason\)' 'final shutdown gate rechecks power source'

Assert-Contains $powerManager '~POWER:STATUS[\s\S]*shutdown_blockers=0x%08[\s\S]*external_power_present=%u[\s\S]*usb_power_present=%u[\s\S]*charging=%u[\s\S]*charge_full=%u[\s\S]*usb_det_level=%s[\s\S]*bat_chg_level=%s[\s\S]*bat_std_level=%s' 'POWER:STATUS raw and interpreted charging fields'
Assert-Contains $powerManager 'DIAG_POWER_SLEEP_BLOCKED[\s\S]*POWER_MANAGER_SHUTDOWN_REASON_LONG_IDLE[\s\S]*power_manager_encode_power_source_flags\(&power_source,\s*true\)' 'automatic shutdown block diag with interpreted power-source flags'
Assert-Contains $powerManager 'DIAG_POWER_EXTERNAL_POWER' 'dedicated external-power diag event'
Assert-Contains $diagEvents 'DIAG_POWER_EXTERNAL_POWER\s+9\s+/\*\s*a1=flags,\s*a2=raw_levels,\s*a3=idle_ms,\s*a4=shutdown_blockers\s+\*/' 'external-power diag event contract'

Write-Host "PASS: charging-awake policy static checks cover raw/interpreted power inputs, plugged idle behavior, automatic long-idle/low-battery shutdown blocking, manual shutdown override, plug/unplug idle reset, and diagnostics."
