# build_firmware.ps1 - Build firmware from any shell (including Claude Code's MSys bash)
# Usage: powershell -File tools/build_firmware.ps1 [-Flash] [-Monitor] [-Clean]
param(
    [switch]$Flash,
    [switch]$Monitor,
    [switch]$Clean,
    [string]$Port = "COM5"
)

$ErrorActionPreference = "Stop"
$FirmwareRoot = $PSScriptRoot
while (-not (Test-Path (Join-Path $FirmwareRoot "CMakeLists.txt"))) {
    $parent = Split-Path $FirmwareRoot -Parent
    if ($parent -eq $FirmwareRoot) { throw "Cannot find firmware CMakeLists.txt" }
    $FirmwareRoot = $parent
}

# Strip MSys/Mingw/Git from PATH to prevent ESP-IDF rejection
$cleanPath = ($env:PATH -split ";" | Where-Object {
    $_ -notmatch "msys|mingw|git\\bin|git\\cmd|git\\usr"
}) -join ";"

# Build the esp-idf activation command
$exportPs1 = "C:\Users\Billy\esp\esp-idf\export.ps1"

$actions = @()
if ($Clean) { $actions += "idf.py fullclean" }
$actions += "idf.py build"
if ($Flash) { $actions += "idf.py -p $Port flash" }
if ($Monitor) { $actions += "idf.py -p $Port monitor" }

$cmdBlock = $actions -join "; "

# Launch a clean PowerShell child process without MSys contamination
$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = "powershell.exe"
$psi.Arguments = "-NoProfile -ExecutionPolicy Bypass -Command `"$env:IDF_PATH = 'C:\Users\Billy\esp\esp-idf'; Set-Location '$FirmwareRoot'; . '$exportPs1'; $cmdBlock`""
$psi.EnvironmentVariables["PATH"] = $cleanPath
$psi.EnvironmentVariables["IDF_PATH"] = "C:\Users\Billy\esp\esp-idf"
# Remove MSys-related env vars
$psi.EnvironmentVariables.Remove("MSYSTEM") | Out-Null
$psi.EnvironmentVariables.Remove("MSYS") | Out-Null
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.WorkingDirectory = $FirmwareRoot

Write-Output "Building firmware from: $FirmwareRoot"
Write-Output "Actions: $($actions -join ', ')"

$proc = [System.Diagnostics.Process]::Start($psi)

# Stream output
$stdoutTask = $proc.StandardOutput.ReadToEndAsync()
$stderrTask = $proc.StandardError.ReadToEndAsync()

$proc.WaitForExit()

$stdout = $stdoutTask.Result
$stderr = $stderrTask.Result

if ($stdout) { Write-Output $stdout }

if ($proc.ExitCode -eq 0) {
    Write-Output "Build succeeded."
} else {
    if ($stderr) { Write-Output $stderr }
    Write-Output "Build failed with exit code $($proc.ExitCode)."
    exit $proc.ExitCode
}
