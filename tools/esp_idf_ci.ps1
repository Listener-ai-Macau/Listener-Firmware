# esp_idf_ci.ps1 — 从 Git Bash 调用 ESP-IDF 全流程
# 绕过 idf.py 的 MSys 检测，直接调底层工具
param(
    [Parameter(Position = 0)]
    [string]$Command = "build",
    [string]$Port = "COM3",
    [string]$Target = "esp32s3",
    [string]$Baud = "460800"
)

$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

# ---- 路径配置 ----
$home_dir = if ($env:USERPROFILE) { $env:USERPROFILE } else { "C:\Users\Billy" }
$prj = if ($PSScriptRoot) { Split-Path -Parent $PSScriptRoot } else { "$home_dir\Desktop\listener\voice-keyboard-firmware" }
$bld = "$prj\build"
$idf = if ($env:ESP_IDF_PATH) { $env:ESP_IDF_PATH } else { "$home_dir\esp\esp-idf" }
$tools = "$home_dir\.espressif\tools"
$python = "$home_dir\.espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
$gcc_dir = "$tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin"
$esptool = "$idf\components\esptool_py\esptool\esptool.py"
$monitor = "$idf\tools\idf_monitor.py"

$ninja = (Get-ChildItem "$tools\ninja" -Recurse -Filter "ninja.exe" | Select-Object -First 1).FullName
$ccache = (Get-ChildItem "$tools\ccache" -Recurse -Filter "ccache.exe" | Select-Object -First 1).DirectoryName

$env:IDF_PATH = $idf
$env:IDF_TARGET = $Target
$env:PATH = "$gcc_dir;$ccache;$([System.IO.Path]::GetDirectoryName($ninja));$([System.IO.Path]::GetDirectoryName($python));$env:PATH"

# ---- 命令分发 ----
switch ($Command) {
    "build" {
        Write-Host "[ci] Building ..."
        if (-not (Test-Path "$bld\build.ninja")) {
            $buildScript = Join-Path $prj "tools\build.ps1"
            if (Test-Path $buildScript) {
                Write-Host "[ci] build.ninja missing; running full ESP-IDF configure/build via tools\build.ps1 ..."
                & pwsh -NoProfile -File $buildScript -Target $Target 2>&1 | ForEach-Object { Write-Host $_ }
                exit $LASTEXITCODE
            }

            Write-Host "[ci] build.ninja missing; running full ESP-IDF configure/build via idf.py ..."
            Set-Location $prj
            & $python "$idf\tools\idf.py" build 2>&1 | ForEach-Object { Write-Host $_ }
            exit $LASTEXITCODE
        }

        Set-Location $bld
        & $ninja 2>&1 | ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE -eq 0) {
            $bin = "$bld\voice-keyboard-firmware.bin"
            Write-Host "[ci] Build OK: $bin ($((Get-Item $bin).Length) bytes)"
        } else {
            Write-Error "[ci] Build FAILED (exit $LASTEXITCODE)"
        }
        exit $LASTEXITCODE
    }

    "flash" {
        $bin = "$bld\voice-keyboard-firmware.bin"
        $bootloader = "$bld\bootloader\bootloader.bin"
        $partition = "$bld\partition_table\partition-table.bin"
        foreach ($f in @($bin, $bootloader, $partition)) {
            if (-not (Test-Path $f)) {
                Write-Error "[ci] Missing: $f — run build first"
                exit 1
            }
        }
        Write-Host "[ci] Flashing to $Port ..."
        & $python $esptool --chip esp32s3 -p $Port -b $Baud --before=default_reset --after=hard_reset write_flash `
            0x0 $bootloader `
            0x8000 $partition `
            0x10000 $bin 2>&1 | ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE -eq 0) { Write-Host "[ci] Flash OK" } else { Write-Error "[ci] Flash FAILED" }
        exit $LASTEXITCODE
    }

    "erase-flash" {
        Write-Host "[ci] Erasing flash on $Port ..."
        & $python $esptool --chip esp32s3 -p $Port -b $Baud erase_flash 2>&1 | ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE -eq 0) { Write-Host "[ci] Erase OK" } else { Write-Error "[ci] Erase FAILED" }
        exit $LASTEXITCODE
    }

    "monitor" {
        $elf = "$bld\voice-keyboard-firmware.elf"
        if (-not (Test-Path $elf)) {
            Write-Error "[ci] Missing: $elf — run build first"
            exit 1
        }
        Write-Host "[ci] Starting monitor on $Port (Ctrl+] to exit) ..."
        & $python $monitor -p $Port $elf 2>&1 | ForEach-Object { Write-Host $_ }
        exit $LASTEXITCODE
    }

    "set-target" {
        Write-Host "[ci] Setting target to $Target ..."
        Set-Location $prj
        $env:IDF_TARGET = $Target
        & $python "$idf\tools\idf.py" set-target $Target 2>&1 | ForEach-Object { Write-Host $_ }
        exit $LASTEXITCODE
    }

    "size" {
        $elf = "$bld\voice-keyboard-firmware.elf"
        if (-not (Test-Path $elf)) {
            Write-Error "[ci] Missing: $elf — run build first"
            exit 1
        }
        & "$gcc_dir\xtensa-esp32s3-elf-size.exe" $elf 2>&1 | ForEach-Object { Write-Host $_ }
        exit 0
    }

    "reconfigure" {
        Write-Host "[ci] Reconfiguring CMake ..."
        if (Test-Path "$bld\build.ninja") {
            Set-Location $bld
            & $ninja -t clean 2>&1 | ForEach-Object { Write-Host $_ }
            Remove-Item "$bld\build.ninja" -ErrorAction SilentlyContinue
        }
        Set-Location $prj
        & $python "$idf\tools\idf.py" build 2>&1 | ForEach-Object { Write-Host $_ }
        exit $LASTEXITCODE
    }

    default {
        Write-Error "Unknown command: $Command"
        Write-Host "Usage: pwsh -File tools/esp_idf_ci.ps1 <command> [-Port COM3] [-Target esp32s3]"
        Write-Host ""
        Write-Host "Commands:"
        Write-Host "  build         Incremental build (ninja)"
        Write-Host "  flash         Flash firmware to device"
        Write-Host "  erase-flash   Erase entire flash"
        Write-Host "  monitor       Serial monitor"
        Write-Host "  set-target    Change build target"
        Write-Host "  size          Show firmware size"
        Write-Host "  reconfigure   Clean + full CMake reconfigure"
        exit 1
    }
}
