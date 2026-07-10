# build_firmware.ps1 - Build firmware from any shell (including Claude Code's MSys bash)
# Usage: pwsh -NoProfile -File tools/build_firmware.ps1 [-Flash] [-Monitor] [-Clean]
param(
    [switch]$Flash,
    [switch]$Monitor,
    [switch]$Clean,
    [string]$Port = "COM5",
    [string]$IdfPath = $env:ESP_IDF_PATH
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

if ([string]::IsNullOrWhiteSpace($IdfPath)) {
    $IdfPath = Join-Path $HOME "esp\esp-idf"
}
$exportPs1 = Join-Path $IdfPath "export.ps1"
if (-not (Test-Path -LiteralPath $exportPs1)) {
    throw "ESP-IDF export.ps1 not found at $exportPs1. Set ESP_IDF_PATH or pass -IdfPath."
}

# Launch every IDF action through the repo wrapper so this helper follows the
# same shell contract as the rest of the repo.
$idfWrapper = Join-Path $PSScriptRoot "idf.ps1"
if (-not (Test-Path -LiteralPath $idfWrapper -PathType Leaf)) {
    throw "Missing IDF wrapper at $idfWrapper."
}
$escapedIdfWrapper = $idfWrapper.Replace("'", "''")
$actions = @()
if ($Clean) { $actions += "& '$escapedIdfWrapper' fullclean" }
$actions += "& '$escapedIdfWrapper' build"
if ($Flash) { $actions += "& '$escapedIdfWrapper' -p $Port flash" }
if ($Monitor) { $actions += "& '$escapedIdfWrapper' -p $Port monitor" }

$cmdBlock = $actions -join "; "

# Launch a clean pwsh child process without MSys contamination
$escapedFirmwareRoot = $FirmwareRoot.Replace("'", "''")
$escapedIdfPath = $IdfPath.Replace("'", "''")
$escapedExportPs1 = $exportPs1.Replace("'", "''")
$childScript = "`$env:IDF_PATH = '$escapedIdfPath'; Set-Location -LiteralPath '$escapedFirmwareRoot'; . '$escapedExportPs1'; $cmdBlock"
$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = "pwsh.exe"
[void]$psi.ArgumentList.Add("-NoProfile")
[void]$psi.ArgumentList.Add("-Command")
[void]$psi.ArgumentList.Add($childScript)
$psi.EnvironmentVariables["PATH"] = $cleanPath
$psi.EnvironmentVariables["IDF_PATH"] = $IdfPath
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
