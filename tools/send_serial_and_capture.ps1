[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [string[]]$Command = @(),
    [string]$CommandList = "",
    [int]$Baud = 115200,
    [int]$InitialReadMs = 800,
    [int]$CommandReadMs = 1200,
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$lines = [System.Collections.Generic.List[string]]::new()
$commandsToRun = @($Command | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
if (-not [string]::IsNullOrWhiteSpace($CommandList)) {
    $commandsToRun += @($CommandList -split ";;" | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}
if ($commandsToRun.Count -eq 0) {
    throw "At least one -Command or -CommandList entry is required."
}

function Add-Line {
    param([Parameter(Mandatory = $true)][string]$Text)
    Write-Host $Text
    $lines.Add($Text) | Out-Null
}

function Read-SerialFor {
    param(
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$Serial,
        [Parameter(Mandatory = $true)][int]$Milliseconds
    )
    $deadline = (Get-Date).AddMilliseconds($Milliseconds)
    while ((Get-Date) -lt $deadline) {
        try {
            $text = $Serial.ReadExisting()
            if (-not [string]::IsNullOrEmpty($text)) {
                foreach ($line in ($text -split "`r?`n")) {
                    if (-not [string]::IsNullOrWhiteSpace($line)) {
                        Add-Line $line
                    }
                }
            }
        } catch {
            Add-Line ("READ_ERROR {0}" -f $_.Exception.Message)
            break
        }
        Start-Sleep -Milliseconds 100
    }
}

$serial = [System.IO.Ports.SerialPort]::new($Port, $Baud)
$serial.ReadTimeout = 200
$serial.WriteTimeout = 1000
$serial.DtrEnable = $false
$serial.RtsEnable = $false

try {
    $serial.Open()
    Add-Line ("serial_opened port={0} baud={1} dtr=0 rts=0" -f $Port, $Baud)
    Read-SerialFor -Serial $serial -Milliseconds $InitialReadMs
    foreach ($cmd in $commandsToRun) {
        Add-Line ("> {0}" -f $cmd)
        $serial.Write(("{0}`n" -f $cmd))
        Read-SerialFor -Serial $serial -Milliseconds $CommandReadMs
    }
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
    Add-Line "serial_closed"
}

if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $parent = Split-Path -Parent $OutputPath
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    $lines | Set-Content -LiteralPath $OutputPath -Encoding utf8
    Write-Host ("transcript={0}" -f (Resolve-Path -LiteralPath $OutputPath).Path)
}
