param()

$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$home_dir = if ($env:USERPROFILE) { $env:USERPROFILE } else { "C:\Users\Billy" }
$prj = "$home_dir\Desktop\listener\voice-keyboard-firmware"
$bld = "$prj\build"
$idf = "$home_dir\esp\esp-idf"
$tools = "$home_dir\.espressif\tools"
$python = "$home_dir\.espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
$gcc_dir = "$tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin"
$ninja = (Get-ChildItem "$tools\ninja" -Recurse -Filter "ninja.exe" | Select-Object -First 1).FullName
$ccache = (Get-ChildItem "$tools\ccache" -Recurse -Filter "ccache.exe" | Select-Object -First 1).DirectoryName
$cmake = (Get-ChildItem "$tools\cmake" -Recurse -Filter "cmake.exe" | Select-Object -First 1).FullName

$env:IDF_PATH = $idf
$env:IDF_TARGET = "esp32s3"
$env:PATH = "$gcc_dir;$ccache;$([System.IO.Path]::GetDirectoryName($ninja));$([System.IO.Path]::GetDirectoryName($python));$([System.IO.Path]::GetDirectoryName($cmake));$env:PATH"

Write-Host "[rebuild] Running cmake to regenerate build files..."
Set-Location $bld
& $cmake .. -G Ninja 2>&1 | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) {
    Write-Error "[rebuild] cmake FAILED"
    exit 1
}

Write-Host "[rebuild] Building with ninja..."
& $ninja 2>&1 | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -eq 0) {
    $bin = "$bld\voice-keyboard-firmware.bin"
    Write-Host "[rebuild] Build OK: $bin ($(Get-Item $bin).Length bytes)"
} else {
    Write-Error "[rebuild] ninja FAILED"
    exit 1
}

# Check partition table
$ptBin = "$bld\partition_table\partition-table.bin"
if (Test-Path $ptBin) {
    Write-Host "[rebuild] Partition table: $ptBin ($(Get-Item $ptBin).Length bytes)"
} else {
    Write-Warning "[rebuild] Partition table binary not found!"
}
