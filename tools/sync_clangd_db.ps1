param(
    [string]$BuildDir
)

$ErrorActionPreference = "Stop"

$project_root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $project_root "build"
}
$source_db = Join-Path $BuildDir "compile_commands.json"
$clangd_dir = Join-Path $project_root ".cache\\clangd"
$output_db = Join-Path $clangd_dir "compile_commands.json"

if (-not (Test-Path $source_db)) {
    throw "missing compilation database: $source_db"
}

New-Item -ItemType Directory -Force -Path $clangd_dir | Out-Null

$entries = Get-Content -Raw -Path $source_db | ConvertFrom-Json
$flags_to_remove = @(
    "-fno-shrink-wrap",
    "-fstrict-volatile-bitfields",
    "-fno-tree-switch-conversion"
)

foreach ($entry in $entries) {
    if ($null -ne $entry.command) {
        foreach ($flag in $flags_to_remove) {
            $escaped_flag = [regex]::Escape($flag)
            $entry.command = $entry.command -replace "(?<!\S)$escaped_flag(?=\s|$)", ""
        }

        $entry.command = ($entry.command -replace "\s{2,}", " ").Trim()
    }

    if ($null -ne $entry.arguments) {
        $entry.arguments = @($entry.arguments | Where-Object { $_ -notin $flags_to_remove })
    }
}

$entries | ConvertTo-Json -Depth 8 | Set-Content -Path $output_db -Encoding utf8
Write-Host "clangd compilation database updated: $output_db"
