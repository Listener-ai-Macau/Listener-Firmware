param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$Baud = 115200,
    [int]$GuardMilliseconds = 86400000,
    [int]$WaitMilliseconds = 1200,
    [switch]$NoRestore,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Command
)

$ErrorActionPreference = "Stop"

function Resolve-GuardSerialPort {
    param([Parameter(Mandatory = $true)][string]$RequestedPort)

    if ($RequestedPort -notmatch '^(?i:COMx)$') {
        return $RequestedPort
    }

    $ports = @([System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object -Unique)
    if ($ports.Count -eq 1) {
        Write-Host "Resolved COMx to '$($ports[0])' from the only present serial port."
        return [string]$ports[0]
    }

    throw "COMx requires exactly one present serial port. Serial ports: $($ports -join ', ')"
}

function New-GuardSerialPort {
    param(
        [Parameter(Mandatory = $true)][string]$SerialPortName,
        [Parameter(Mandatory = $true)][int]$SerialBaud
    )

    $serialPort = [System.IO.Ports.SerialPort]::new($SerialPortName, $SerialBaud)
    $serialPort.ReadTimeout = 100
    $serialPort.WriteTimeout = 1000
    $serialPort.DtrEnable = $false
    $serialPort.RtsEnable = $false
    $serialPort.NewLine = "`n"
    return $serialPort
}

function Read-SerialUntil {
    param(
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$SerialPort,
        [Parameter(Mandatory = $true)][datetime]$Deadline
    )

    $lines = New-Object System.Collections.Generic.List[string]
    while ((Get-Date) -lt $Deadline) {
        try {
            $line = $SerialPort.ReadLine().Trim()
            if (-not [string]::IsNullOrWhiteSpace($line)) {
                $lines.Add($line)
            }
        } catch [System.TimeoutException] {
        }
    }
    return @($lines)
}

function Invoke-DeviceCommand {
    param(
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$SerialPort,
        [Parameter(Mandatory = $true)][string]$DeviceCommand,
        [Parameter(Mandatory = $true)][int]$WaitMs
    )

    $SerialPort.DiscardInBuffer()
    $SerialPort.DiscardOutBuffer()
    $SerialPort.Write("$DeviceCommand`n")
    return @(Read-SerialUntil -SerialPort $SerialPort -Deadline (Get-Date).AddMilliseconds($WaitMs))
}

function Get-AutoShutdownMs {
    param([string[]]$Lines)

    foreach ($line in @($Lines | Select-Object -Last 8)) {
        $match = [regex]::Match($line, '\bauto_shutdown_ms=(\d+)\b')
        if ($match.Success) {
            return [uint32]$match.Groups[1].Value
        }
    }
    return $null
}

if ($GuardMilliseconds -lt 60000 -or $GuardMilliseconds -gt 86400000) {
    throw "-GuardMilliseconds must be in the firmware DEVICE range 60000..86400000."
}

if ($Command.Count -gt 0 -and $Command[0] -eq "--") {
    $Command = @($Command | Select-Object -Skip 1)
}

$resolvedPort = Resolve-GuardSerialPort -RequestedPort $Port
$originalAutoShutdownMs = $null
$restoreAfterCommand = -not $NoRestore -and $Command.Count -gt 0

$serialPort = New-GuardSerialPort -SerialPortName $resolvedPort -SerialBaud $Baud
try {
    $serialPort.Open()
    $beforeLines = @(Invoke-DeviceCommand -SerialPort $serialPort -DeviceCommand "~DEVICE:SETTINGS" -WaitMs $WaitMilliseconds)
    $originalAutoShutdownMs = Get-AutoShutdownMs -Lines $beforeLines
    if ($null -eq $originalAutoShutdownMs) {
        throw "Could not read auto_shutdown_ms from ~DEVICE:SETTINGS. Lines: $($beforeLines -join ' | ')"
    }

    $setLines = @(Invoke-DeviceCommand `
        -SerialPort $serialPort `
        -DeviceCommand ("~DEVICE:SET auto_shutdown_ms={0}" -f $GuardMilliseconds) `
        -WaitMs $WaitMilliseconds)
    $guardedAutoShutdownMs = Get-AutoShutdownMs -Lines $setLines
    if ($guardedAutoShutdownMs -ne [uint32]$GuardMilliseconds) {
        throw "Auto-shutdown guard did not take effect. Expected $GuardMilliseconds, got $guardedAutoShutdownMs. Lines: $($setLines -join ' | ')"
    }

    Write-Host "Auto-shutdown guard active on ${resolvedPort}: original=$originalAutoShutdownMs guarded=$guardedAutoShutdownMs"
} finally {
    if ($serialPort.IsOpen) {
        $serialPort.Close()
    }
    $serialPort.Dispose()
}

try {
    if ($Command.Count -gt 0) {
        $file = $Command[0]
        $arguments = @($Command | Select-Object -Skip 1)
        $global:LASTEXITCODE = 0
        & $file @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Guarded command failed with exit code $LASTEXITCODE"
        }
    } else {
        Write-Host "No guarded command was provided; auto_shutdown_ms remains guarded until restored or changed."
    }
} finally {
    if ($restoreAfterCommand -and $null -ne $originalAutoShutdownMs) {
        $restorePort = New-GuardSerialPort -SerialPortName $resolvedPort -SerialBaud $Baud
        try {
            $restorePort.Open()
            $restoreLines = @(Invoke-DeviceCommand `
                -SerialPort $restorePort `
                -DeviceCommand ("~DEVICE:SET auto_shutdown_ms={0}" -f $originalAutoShutdownMs) `
                -WaitMs $WaitMilliseconds)
            $restoredAutoShutdownMs = Get-AutoShutdownMs -Lines $restoreLines
            if ($restoredAutoShutdownMs -ne [uint32]$originalAutoShutdownMs) {
                Write-Warning "Auto-shutdown restore did not verify. Expected $originalAutoShutdownMs, got $restoredAutoShutdownMs. Lines: $($restoreLines -join ' | ')"
            } else {
                Write-Host "Auto-shutdown guard restored on ${resolvedPort}: auto_shutdown_ms=$restoredAutoShutdownMs"
            }
        } catch {
            Write-Warning "Auto-shutdown restore failed on ${resolvedPort}: $($_.Exception.Message)"
        } finally {
            if ($restorePort.IsOpen) {
                $restorePort.Close()
            }
            $restorePort.Dispose()
        }
    }
}
