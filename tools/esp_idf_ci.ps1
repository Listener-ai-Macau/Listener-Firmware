# esp_idf_ci.ps1 — 从 Git Bash 调用 ESP-IDF 全流程
# 构建/配置入口统一走仓库 wrapper；flash/monitor 只直接调用底层 esptool/monitor 工具。
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
$home_dir = if ($env:USERPROFILE) {
    $env:USERPROFILE
} elseif ($env:HOME) {
    $env:HOME
} else {
    throw "Cannot resolve user home directory from USERPROFILE or HOME."
}
$prj = if ($PSScriptRoot) { Split-Path -Parent $PSScriptRoot } else { (Resolve-Path ".").Path }
$bld = "$prj\build"
$idf = if ($env:ESP_IDF_PATH) { $env:ESP_IDF_PATH } else { "$home_dir\esp\esp-idf" }
$tools = "$home_dir\.espressif\tools"
$python = Get-ChildItem "$home_dir\.espressif\python_env" -Recurse -Filter "python.exe" -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match "\\Scripts\\python\.exe$" } |
    Sort-Object FullName -Descending |
    Select-Object -ExpandProperty FullName -First 1
if (-not $python) { $python = "python" }
$gcc = Get-ChildItem "$tools\xtensa-esp-elf" -Recurse -Filter "xtensa-esp32s3-elf-gcc.exe" -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if (-not $gcc) { throw "[ci] xtensa-esp32s3-elf-gcc.exe not found under $tools. Run tools\setup_windows.ps1 first." }
$gcc_dir = $gcc.DirectoryName
$esptool = "$idf\components\esptool_py\esptool\esptool.py"
$monitor = "$idf\tools\idf_monitor.py"

$ninja = (Get-ChildItem "$tools\ninja" -Recurse -Filter "ninja.exe" -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
if (-not $ninja) { throw "[ci] ninja.exe not found under $tools. Run tools\setup_windows.ps1 first." }
$ccache = (Get-ChildItem "$tools\ccache" -Recurse -Filter "ccache.exe" -ErrorAction SilentlyContinue | Select-Object -First 1).DirectoryName

$env:IDF_PATH = $idf
$env:IDF_TARGET = $Target
$pathParts = @($gcc_dir, $ccache, [System.IO.Path]::GetDirectoryName($ninja), [System.IO.Path]::GetDirectoryName($python)) |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
$env:PATH = "$($pathParts -join ';');$env:PATH"

function Get-IdfPartitionTable {
    param([Parameter(Mandatory = $true)][string]$PartitionBin)

    $genPart = Join-Path $idf "components\partition_table\gen_esp32part.py"
    if (-not (Test-Path $genPart)) {
        throw "[ci] Missing ESP-IDF partition parser: $genPart"
    }

    $output = @(& $python $genPart $PartitionBin 2>&1)
    if ($LASTEXITCODE -ne 0) {
        $text = ($output -join "`n")
        throw "[ci] Failed to parse partition table $PartitionBin`n$text"
    }

    $entries = @()
    foreach ($line in $output) {
        $trimmed = ([string]$line).Trim()
        if (-not $trimmed -or $trimmed.StartsWith("#") -or -not $trimmed.Contains(",")) {
            continue
        }

        $parts = @($trimmed -split ",")
        if ($parts.Count -lt 5 -or $parts[3].Trim() -notmatch "^0x[0-9A-Fa-f]+$") {
            continue
        }

        $entries += [pscustomobject]@{
            Name = $parts[0].Trim()
            Type = $parts[1].Trim()
            SubType = $parts[2].Trim()
            Offset = $parts[3].Trim()
            Size = $parts[4].Trim()
        }
    }

    return $entries
}

function Get-PartitionEntry {
    param(
        [Parameter(Mandatory = $true)]$Entries,
        [Parameter(Mandatory = $true)][string]$Name
    )
    return @($Entries | Where-Object { $_.Name -eq $Name } | Select-Object -First 1)[0]
}

function Write-OtaPartitionEvidence {
    param([Parameter(Mandatory = $true)]$Entries)
    foreach ($name in @("otadata", "ota_0", "ota_1")) {
        $entry = Get-PartitionEntry -Entries $Entries -Name $name
        if ($null -eq $entry) {
            Write-Host "[ci] partition ${name}: missing"
            continue
        }
        Write-Host "[ci] partition $($entry.Name): type=$($entry.Type) subtype=$($entry.SubType) offset=$($entry.Offset) size=$($entry.Size)"
    }
}

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

            throw "[ci] Missing tools\build.ps1; cannot run repo-approved build wrapper."
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
        try {
            $partitionEntries = Get-IdfPartitionTable -PartitionBin $partition
            Write-OtaPartitionEvidence -Entries $partitionEntries
            $ota0 = Get-PartitionEntry -Entries $partitionEntries -Name "ota_0"
            if ($null -eq $ota0) {
                throw "[ci] partition table does not contain ota_0; cannot derive app flash offset"
            }
            $appOffset = $ota0.Offset
        } catch {
            Write-Error $_
            exit 1
        }
        Write-Host "[ci] Flashing to $Port ..."
        Write-Host "[ci] write_flash plan: bootloader=0x0 partition_table=0x8000 app($($ota0.Name))=$appOffset"
        & $python $esptool --chip esp32s3 -p $Port -b $Baud --before=default_reset --after=hard_reset write_flash `
            0x0 $bootloader `
            0x8000 $partition `
            $appOffset $bin 2>&1 | ForEach-Object { Write-Host $_ }
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
        & pwsh -NoProfile -File (Join-Path $prj "tools\idf.ps1") set-target $Target 2>&1 | ForEach-Object { Write-Host $_ }
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
        & pwsh -NoProfile -File (Join-Path $prj "tools\idf.ps1") build 2>&1 | ForEach-Object { Write-Host $_ }
        exit $LASTEXITCODE
    }

    default {
        Write-Error "Unknown command: $Command"
        Write-Host "Usage: pwsh -NoProfile -File tools/esp_idf_ci.ps1 <command> [-Port COM3] [-Target esp32s3]"
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
