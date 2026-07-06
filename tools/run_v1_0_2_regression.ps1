[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Port = "COM3",
    [string]$DeviceName = "listener",
    [string]$BluetoothAddress = "",
    [string]$OutputDir = "",
    [switch]$SkipBuild,
    [switch]$SkipFlash,
    [switch]$SkipHardware,
    [switch]$SkipOtaProbe,
    [switch]$RunAudioBleProductMatrix,
    [switch]$PackageOtaDevelopment
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repoRoot ".artifacts\v1.0.2-regression\$stamp"
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$results = [System.Collections.Generic.List[object]]::new()

function ConvertTo-SafeName {
    param([Parameter(Mandatory = $true)][string]$Name)
    return ($Name -replace '[^A-Za-z0-9_.-]+', '_').Trim('_')
}

function Invoke-External {
    param(
        [Parameter(Mandatory = $true)][string]$File,
        [string[]]$Args = @(),
        [string]$WorkingDirectory = ""
    )

    if (-not [string]::IsNullOrWhiteSpace($WorkingDirectory)) {
        Push-Location $WorkingDirectory
    }
    try {
        Write-Host ("> {0} {1}" -f $File, ($Args -join " "))
        & $File @Args
        $exit = $LASTEXITCODE
        if ($null -ne $exit -and $exit -ne 0) {
            throw "$File exited with code $exit"
        }
    } finally {
        if (-not [string]::IsNullOrWhiteSpace($WorkingDirectory)) {
            Pop-Location
        }
    }
}

function Invoke-Gate {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][scriptblock]$Block
    )

    $safe = ConvertTo-SafeName $Name
    $logPath = Join-Path $OutputDir "$safe.log"
    Write-Host ""
    Write-Host "=== $Name ==="
    $started = Get-Date
    Start-Transcript -Path $logPath -Force | Out-Null
    try {
        & $Block
        $elapsed = [int]((Get-Date) - $started).TotalSeconds
        $results.Add([pscustomobject]@{
            name = $Name
            status = "PASS"
            seconds = $elapsed
            log = $logPath
        }) | Out-Null
        Write-Host "[PASS] $Name ($elapsed s)"
    } catch {
        $elapsed = [int]((Get-Date) - $started).TotalSeconds
        $results.Add([pscustomobject]@{
            name = $Name
            status = "FAIL"
            seconds = $elapsed
            log = $logPath
            error = $_.Exception.Message
        }) | Out-Null
        Write-Host "[FAIL] $Name ($elapsed s): $($_.Exception.Message)"
        throw
    } finally {
        Stop-Transcript | Out-Null
    }
}

try {
    Invoke-Gate "static release contracts" {
        Invoke-External "pwsh" @("-NoProfile", "-File", (Join-Path $PSScriptRoot "release_check.ps1")) $repoRoot
        Invoke-External "python" @((Join-Path $PSScriptRoot "verify_voice_recording_control_fsm.py")) $repoRoot
        Invoke-External "python" @((Join-Path $PSScriptRoot "verify_ec11_input_contract.py")) $repoRoot
        Invoke-External "python" @((Join-Path $PSScriptRoot "verify_ble_audio_transport_model.py")) $repoRoot
        Invoke-External "pwsh" @("-NoProfile", "-File", (Join-Path $PSScriptRoot "verify_ble_audio_backpressure_static.ps1")) $repoRoot
    }

    if (-not $SkipBuild.IsPresent) {
        Invoke-Gate "firmware build" {
            Invoke-External "pwsh" @("-NoProfile", "-File", (Join-Path $PSScriptRoot "build.ps1")) $repoRoot
        }
    }

    if ($PackageOtaDevelopment.IsPresent) {
        Invoke-Gate "development OTA package" {
            Invoke-External "pwsh" @(
                "-NoProfile",
                "-File", (Join-Path $PSScriptRoot "package_ota_firmware.ps1"),
                "-Channel", "development"
            ) $repoRoot
        }
    }

    if (-not $SkipHardware.IsPresent) {
        if (-not $SkipFlash.IsPresent) {
            Invoke-Gate "wired flash" {
                Invoke-External "pwsh" @(
                    "-NoProfile",
                    "-File", (Join-Path $PSScriptRoot "flash.ps1"),
                    "-Port", $Port
                ) $repoRoot
            }
        }

        Invoke-Gate "serial status snapshot" {
            $serialLog = Join-Path $OutputDir "serial-status.log"
            Invoke-External "pwsh" @(
                "-NoProfile",
                "-File", (Join-Path $PSScriptRoot "send_serial_and_capture.ps1"),
                "-Port", $Port,
                "-CommandList", "~POWER:STATUS;;~LED:STATUS;;~OTA:STATUS",
                "-OutputPath", $serialLog
            ) $repoRoot
        }

        Invoke-Gate "KEY3 gradient stays on SPI DMA" {
            $keyLog = Join-Path $OutputDir "key3-gradient-spi-dma.log"
            Invoke-External "pwsh" @(
                "-NoProfile",
                "-File", (Join-Path $PSScriptRoot "send_serial_and_capture.ps1"),
                "-Port", $Port,
                "-CommandList", "~LED:STATUS;;~KEY:KEY3:SINGLE;;~LED:STATUS",
                "-InitialReadMs", "500",
                "-CommandReadMs", "2200",
                "-OutputPath", $keyLog
            ) $repoRoot
            $keyLogText = Get-Content -Raw -LiteralPath $keyLog
            if ($keyLogText -notmatch "~KEY:GENERATED logical=KEY3 gesture=single result=ESP_OK") {
                throw "KEY3 generated single gesture did not complete"
            }
            if ($keyLogText -match "strip key non-DMA one-shot") {
                throw "KEY3 gradient regressed through key non-DMA one-shot transport"
            }
            if (
                $keyLogText -notmatch "spi_dma_actual=status:0,ec11:1,key:1,edge:0" -and
                $keyLogText -notmatch "key:gpio13:count4:orderGRB:transportspi3:avail1:dma_req1:dma1"
            ) {
                throw "KEY strip did not report active SPI3 DMA after KEY3 gradient"
            }
        }

        if (-not $SkipOtaProbe.IsPresent) {
            Invoke-Gate "OTA v2 GATT probe" {
                $args = @(
                    "-NoProfile",
                    "-File", (Join-Path $PSScriptRoot "probe_ble_ota_gatt.ps1"),
                    "-DeviceName", $DeviceName,
                    "-TimeoutSeconds", "20"
                )
                if (-not [string]::IsNullOrWhiteSpace($BluetoothAddress)) {
                    $args += @("-BluetoothAddress", $BluetoothAddress)
                }
                Invoke-External "pwsh" $args $repoRoot
            }
        }

        if ($RunAudioBleProductMatrix.IsPresent) {
            Invoke-Gate "recording BLE product matrix smoke" {
                Invoke-External "pwsh" @(
                    "-NoProfile",
                    "-File", (Join-Path $PSScriptRoot "verify_audio_ble_product_matrix.ps1"),
                    "-Port", $Port,
                    "-DeviceName", $DeviceName,
                    "-Cases", "smoke",
                    "-CaptureSeconds", "5",
                    "-LongCaptureSeconds", "30"
                ) $repoRoot
            }
        }
    } else {
        $results.Add([pscustomobject]@{
            name = "hardware gates"
            status = "SKIP"
            seconds = 0
            log = ""
            error = "Skipped by -SkipHardware"
        }) | Out-Null
    }
} finally {
    $summaryPath = Join-Path $OutputDir "summary.json"
    $results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $summaryPath -Encoding utf8
    Write-Host ""
    Write-Host "Regression summary: $summaryPath"
    $results | Format-Table -AutoSize
}

$failed = @($results | Where-Object { $_.status -eq "FAIL" })
if ($failed.Count -gt 0) {
    exit 1
}

Write-Host "PASS: Listener Firmware v1.0.2 regression gate completed."
