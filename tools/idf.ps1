$ErrorActionPreference = "Stop"

$project_root = Split-Path -Parent $PSScriptRoot
$target = if ([string]::IsNullOrWhiteSpace($env:IDF_TARGET)) { "esp32s3" } else { $env:IDF_TARGET }

. (Join-Path $PSScriptRoot "idf_env.ps1") -Target $target

if ($args.Count -eq 0) {
    Write-Host "Usage: pwsh -NoProfile -File .\tools\idf.ps1 <idf.py args>"
    Write-Host "Example: pwsh -NoProfile -File .\tools\idf.ps1 build"
    exit 2
}

Set-Location $project_root
& idf.py @args
exit $LASTEXITCODE
