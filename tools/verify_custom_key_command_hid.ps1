param(
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$errors = [System.Collections.Generic.List[string]]::new()
$evidence = [System.Collections.Generic.List[string]]::new()

function Read-RepoText {
    param([Parameter(Mandatory = $true)][string]$RelativePath)

    $path = Join-Path $repoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path)) {
        $errors.Add("missing file: $RelativePath")
        return ""
    }
    return Get-Content -LiteralPath $path -Raw
}

function Add-Error {
    param([Parameter(Mandatory = $true)][string]$Message)
    $errors.Add($Message)
}

function Assert-Contains {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if ($Text -notmatch $Pattern) {
        Add-Error "missing ${Description}: $Pattern"
    } else {
        $evidence.Add("PASS: $Description")
    }
}

function Assert-NotContains {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if ($Text -match $Pattern) {
        Add-Error "stale ${Description}: $Pattern"
    } else {
        $evidence.Add("PASS: no $Description")
    }
}

$keyboard = Read-RepoText "components\keyboard\keyboard.c"
$hidHeader = Read-RepoText "components\hid_keyboard\include\hid_keyboard.h"
$bleHid = Read-RepoText "ports\esp32\ble_hid\ble_hid.c"
$boardPins = Read-RepoText "ports\esp32\board_pins\include\board_pins.h"
$physicalVerifier = Read-RepoText "tools\verify_physical_custom_key_hid.ps1"
$generatedHostVerifier = Read-RepoText "tools\verify_generated_custom_key_hid_host.ps1"
$legacyWasdVerifier = Read-RepoText "tools\verify_physical_wasd_hid.ps1"
$featureMap = Read-RepoText "docs\features\firmware-feature-map.md"

$expectedKeys = @(
    @{ Logical = "KEY1"; Gpio = "38"; Single = "F13"; SingleUsage = "HID_KEYBOARD_USAGE_F13"; Double = "F17"; Long = "F21"; Label = "key1.gpio38.f13"; Vk = "0x7C"; Hid = "0x68u" },
    @{ Logical = "KEY2"; Gpio = "39"; Single = "F14"; SingleUsage = "HID_KEYBOARD_USAGE_F14"; Double = "F18"; Long = "F22"; Label = "key2.gpio39.f14"; Vk = "0x7D"; Hid = "0x69u" },
    @{ Logical = "KEY3"; Gpio = "40"; Single = "F15"; SingleUsage = "HID_KEYBOARD_USAGE_F15"; Double = "F19"; Long = "F23"; Label = "key3.gpio40.f15"; Vk = "0x7E"; Hid = "0x6Au" },
    @{ Logical = "KEY4"; Gpio = "41"; Single = "F16"; SingleUsage = "HID_KEYBOARD_USAGE_F16"; Double = "F20"; Long = "F24"; Label = "key4.gpio41.f16"; Vk = "0x7F"; Hid = "0x6Bu" }
)

foreach ($key in $expectedKeys) {
    $logical = $key.Logical
    $gpio = $key.Gpio
    $single = $key.Single
    $singleUsage = $key.SingleUsage
    $double = $key.Double
    $long = $key.Long
    $label = [regex]::Escape($key.Label)
    $vk = [regex]::Escape($key.Vk)
    $hid = [regex]::Escape($key.Hid)
    $keyBlockPattern = "(?s)\.gpio\s*=\s*BOARD_PINS_${logical}_IO,.*?\.single_usage\s*=\s*$singleUsage,.*?\.double_usage\s*=\s*HID_KEYBOARD_USAGE_$double,.*?\.long_usage\s*=\s*HID_KEYBOARD_USAGE_$long,.*?\.logical_name\s*=\s*`"$logical`",.*?\.label\s*=\s*`"$label`""

    Assert-Contains -Text $boardPins -Pattern "BOARD_PINS_${logical}_IO\s+\(GPIO_NUM_${gpio}\)" -Description "$logical board pin GPIO$gpio"
    Assert-Contains -Text $keyboard -Pattern $keyBlockPattern -Description "$logical custom-key struct maps GPIO$gpio to $single/$double/$long"
    Assert-Contains -Text $keyboard -Pattern "custom key fallback queued: logical=%s source=%s usage=F%u gesture=single" -Description "$logical fallback queue log format"
    Assert-Contains -Text $hidHeader -Pattern "#define\s+$singleUsage\s+$hid" -Description "$logical BLE HID usage value $single"
    Assert-Contains -Text $physicalVerifier -Pattern $vk -Description "$logical Windows host VK capture for $single"
    Assert-Contains -Text $physicalVerifier -Pattern "custom key fallback queued: logical=$logical source=$label usage=$single" -Description "$logical physical verifier serial fallback line"
    Assert-Contains -Text $generatedHostVerifier -Pattern $vk -Description "$logical generated host verifier VK capture for $single"
    Assert-Contains -Text $generatedHostVerifier -Pattern "~KEY:${logical}:SINGLE" -Description "$logical generated host verifier command"
    Assert-Contains -Text $generatedHostVerifier -Pattern "custom key fallback queued: logical=\{logical\} source=\{source\} usage=\{usage_name\} gesture=single" -Description "$logical generated host verifier serial fallback template"
}

Assert-Contains -Text $keyboard -Pattern "ble_hid_send_keyboard_usage_async\(usage,\s*source_label\)" -Description "custom key path sends HID usages instead of text"
Assert-Contains -Text $keyboard -Pattern "KEY:PENDING|~KEY:PENDING" -Description "custom key pending HID diagnostic command"
Assert-Contains -Text $keyboard -Pattern "~KEY:STATUS custom_keys=KEY1:F13/F17/F21" -Description "custom key status diagnostic command"
Assert-Contains -Text $keyboard -Pattern "task_priority=%u audio_preempt_safe=1" -Description "custom key status reports audio-safe scan priority"
Assert-Contains -Text $keyboard -Pattern "ble_hid_send_keyboard_usage_pending_test_async" -Description "custom key pending diagnostic uses BLE HID pending test path"
Assert-Contains -Text $keyboard -Pattern "DIAG_KBD_CUSTOM_KEY" -Description "custom key diagnostic event logging"
Assert-Contains -Text $keyboard -Pattern "KEYBOARD_CUSTOM_DEBOUNCE_MS\s+20" -Description "custom key debounce tuned for physical buttons"
Assert-Contains -Text $bleHid -Pattern "xQueueSend\(s_usage_queue,\s*&?event,\s*0\)" -Description "BLE HID usage queue dispatch"
Assert-Contains -Text $bleHid -Pattern "ble_hid_usage_task" -Description "BLE HID usage worker serializes queued key reports"
Assert-Contains -Text $bleHid -Pattern "ble_hid_signal_usage_task" -Description "BLE HID usage enqueue wakes worker task"
Assert-Contains -Text $bleHid -Pattern "HID usage queued: usage=0x%02X" -Description "keyboard usage queued handoff log"
Assert-Contains -Text $bleHid -Pattern "s_usage_transport_test_blocked" -Description "BLE HID pending self-test can force transport-not-ready branch"
Assert-Contains -Text $bleHid -Pattern "pending HID usage self-test releasing transport block" -Description "BLE HID pending self-test drain log"
Assert-Contains -Text $featureMap -Pattern "F13-F16" -Description "feature map documents F13-F16 fallback"
Assert-Contains -Text $featureMap -Pattern "~KEY:PENDING:KEY1:SINGLE" -Description "feature map documents pending HID diagnostic command"
Assert-Contains -Text $featureMap -Pattern "until Listener-Type consumes custom key actions" -Description "feature map documents desktop custom action contract"
Assert-Contains -Text $legacyWasdVerifier -Pattern "legacy wrapper" -Description "WASD verifier is marked legacy"
Assert-NotContains -Text $bleHid -Pattern "HID usage dispatched immediately|immediate HID usage dispatch failed|hid_usage_dispatch`"" -Description "custom-key synchronous HID usage fast path"
Assert-NotContains -Text $keyboard -Pattern "WASD key|HID_KEYBOARD_USAGE_(A|D|S|W)\b|ble_hid_send_ascii_async" -Description "active custom-key WASD or text fallback"
Assert-NotContains -Text $featureMap -Pattern "\bWASD\b" -Description "active feature map WASD fallback wording"

$status = if ($errors.Count -eq 0) { "PASS" } else { "FAIL" }
$artifactLines = [System.Collections.Generic.List[string]]::new()
$artifactLines.Add("# Custom key command/HID contract validation")
$artifactLines.Add("")
$artifactLines.Add("status=$status")
$artifactLines.Add("scope=static source contract plus physical verifier expectations")
$artifactLines.Add("")
$artifactLines.Add("## Evidence")
foreach ($line in $evidence) {
    $artifactLines.Add("- $line")
}

if ($errors.Count -gt 0) {
    $artifactLines.Add("")
    $artifactLines.Add("## Errors")
    foreach ($errorItem in $errors) {
        $artifactLines.Add("- $errorItem")
    }
}

$artifactText = ($artifactLines -join "`n") + "`n"
if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $resolvedOutputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedOutputPath) | Out-Null
    Set-Content -LiteralPath $resolvedOutputPath -Value $artifactText -Encoding UTF8
    Write-Output "artifact=$resolvedOutputPath"
}

if ($errors.Count -gt 0) {
    Write-Output "FAIL: custom key command/HID contract validation failed"
    foreach ($errorItem in $errors) {
        Write-Output " - $errorItem"
    }
    exit 1
}

Write-Output "PASS: custom key command/HID contract validates KEY1-KEY4 F13-F16 fallback, F17-F24 gestures, stable diagnostics, and no WASD/text fallback."
