param(
    [Parameter(Mandatory = $true)]
    [string]$Port
)

$ErrorActionPreference = "Stop"

$project_root = Split-Path -Parent $PSScriptRoot
$idf_path = Join-Path (Split-Path -Parent $project_root) "esp-idf"
$python_path = "C:\Users\Billy\AppData\Local\Programs\Python\Python311"

$env:PATH = "$python_path;$env:PATH"
$env:IDF_TARGET = "esp32c3"
. (Join-Path $idf_path "export.ps1")

Set-Location $project_root
idf.py -p $Port monitor
