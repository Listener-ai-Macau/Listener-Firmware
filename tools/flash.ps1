param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [string]$Target = "esp32s3",
    [string]$BuildDir = $env:LISTENER_IDF_BUILD_DIR
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "idf_env.ps1") -Target $Target
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    idf.py -p $Port flash
} else {
    $resolvedBuildDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDir)
    idf.py -B $resolvedBuildDir -p $Port flash
}
