param()

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $PSCommandPath
$pythonScript = Join-Path $scriptDir "verify_power_manager_static.py"
python $pythonScript
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
