"""Execute the production C power-source filter through plug/unplug sequences.

Uses MSVC on Windows (or cc on POSIX). No device or duplicated policy model.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r"(?:static )?\w+\s+" + name + r"\s*\([^;]*?\)\s*\{", source)
    if not match:
        raise RuntimeError(f"Missing production function: {name}")
    start = source.index("{", match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def main():
    power = (ROOT / "components/power_manager/power_manager.c").read_text(encoding="utf-8")
    board = (ROOT / "components/board/board.c").read_text(encoding="utf-8")
    source_type = re.search(r"typedef struct \{[^}]+\} power_manager_power_source_snapshot_t;", power).group()
    constants = "\n".join(re.findall(r"^#define POWER_MANAGER_(?:CHARGE_FULL|CHARGER_STATUS)[^\n]+", power, re.M))
    code = '''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
typedef struct { bool charging, standby_full, vin_present; } board_v2_charger_pin_decode_t;
typedef struct { bool battery_valid; uint32_t battery_mv; uint8_t battery_level_percent; } power_manager_snapshot_t;
static bool s_charge_full_latched;
static uint64_t s_charge_full_candidate_since_ms, s_charger_status_external_until_ms;
'''
    code += source_type + "\n" + constants + "\n"
    code += function(board, "board_decode_charger_status_pins") + "\n"
    for name in ("power_manager_clamp_u64_to_u32", "power_manager_charge_full_battery_allowed",
                 "power_manager_charge_full_candidate_ms_locked", "power_manager_charger_status_external_locked",
                 "power_manager_apply_charge_state_filter_locked"):
        code += function(power, name) + "\n"
    code += '''
static power_manager_power_source_snapshot_t sample(int chg, int std, bool sof,
    uint64_t now, uint32_t mv, uint8_t percent) {
    /* Deliberately provide the untrusted raw-full flags used by the board adapter. */
    power_manager_power_source_snapshot_t s = {0};
    s.bat_chg_level = chg; s.bat_std_level = std;
    s.usb_serial_jtag_sof_active = sof;
    s.usb_power_present = sof; s.charge_power_present = std == 0 || chg == 0;
    s.charge_full = std == 0; s.external_power_present = true;
    power_manager_snapshot_t battery = {true, mv, percent};
    power_manager_apply_charge_state_filter_locked(&s, now, &battery);
    return s;
}
int main(void) {
    power_manager_power_source_snapshot_t s;
    s = sample(1, 0, false, 100, 3700, 55);
    assert(!s.external_power_present && !s.charge_full);
    s = sample(1, 0, true, 12000, 3700, 55);
    assert(!s.external_power_present && !s.usb_power_present && !s.charge_full);
    s = sample(0, 1, false, 13000, 3800, 65);
    assert(s.external_power_present && s.charging && !s.charge_full);
    s = sample(1, 0, false, 15000, 4150, 100);
    assert(s.external_power_present && !s.charging);
    s = sample(1, 0, false, 26000, 4150, 100);
    assert(s.external_power_present && s.charge_full);
    s = sample(1, 0, true, 27000, 4150, 100);
    assert(s.external_power_present && s.usb_power_present && s.charge_full);
    /* Restarting charge must clear the old full latch. */
    s = sample(0, 1, true, 28000, 4000, 85);
    assert(s.charging && !s.charge_full && !s.charge_full_latched);
    /* Unplug with false STD, including stale SOF, must release after debounce. */
    s = sample(1, 0, true, 30000, 3700, 55);
    assert(!s.external_power_present && !s.usb_power_present && !s.charge_full);
    s = sample(1, 1, false, 32000, 3700, 55);
    assert(!s.external_power_present);
    s = sample(0, 0, false, 34000, 3700, 55);
    assert(!s.external_power_present);
    /* A full latch must also clear when battery evidence disproves it. */
    sample(1, 0, false, 35000, 4150, 100);
    s = sample(1, 0, false, 46000, 4150, 100);
    assert(s.charge_full);
    s = sample(1, 0, false, 48000, 3700, 55);
    assert(!s.charge_full && !s.charge_full_latched && !s.external_power_present);
    puts("PASS: production power-source filter plug/full/recharge/unplug transitions");
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="listener-power-test-") as directory:
        tmp = Path(directory)
        (tmp / "test.c").write_text(code, encoding="utf-8")
        if os.name == "nt":
            vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
            vs = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-property", "installationPath"], text=True).strip()
            vcvars = Path(vs) / "VC/Auxiliary/Build/vcvars64.bat"
            (tmp / "build.cmd").write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\ncl /nologo /std:c11 /W4 /WX test.c /Fe:test.exe\n', encoding="utf-8")
            subprocess.run(["cmd.exe", "/d", "/c", str(tmp / "build.cmd")], cwd=tmp, check=True)
            executable = tmp / "test.exe"
        else:
            executable = tmp / "test"
            subprocess.run([shutil.which("cc") or "cc", "-std=c11", "-Wall", "-Werror", "test.c", "-o", str(executable)], cwd=tmp, check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
