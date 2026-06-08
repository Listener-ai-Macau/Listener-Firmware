param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$CameraIndex = 0,
    [int]$Baud = 115200
)

$ErrorActionPreference = "Stop"

$outputDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$serialLog = Join-Path $outputDir "led-factory-rgbw-serial-$stamp.log"
$cameraLog = Join-Path $outputDir "led-factory-rgbw-camera-$stamp.log"
$imagePath = Join-Path $outputDir "led-factory-rgbw-camera$CameraIndex-$stamp.jpg"

$lines = New-Object System.Collections.Generic.List[string]

function Add-LogLine {
    param([string]$Text)
    $lines.Add(("{0} {1}" -f (Get-Date -Format "o"), $Text))
}

function Read-SerialFor {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [int]$Milliseconds
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($Milliseconds)
    $chunks = New-Object System.Collections.Generic.List[string]
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $chunk = $Serial.ReadExisting()
            if ($chunk) {
                $chunks.Add($chunk)
            }
        } catch [TimeoutException] {
        }
        Start-Sleep -Milliseconds 50
    }
    return ($chunks -join "")
}

function Write-Command {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [string]$Command
    )

    Add-LogLine ("TX {0}" -f $Command)
    $Serial.Write("$Command`n")
    $Serial.BaseStream.Flush()
}

$serial = [System.IO.Ports.SerialPort]::new($Port, $Baud)
$serial.ReadTimeout = 200
$serial.WriteTimeout = 1000
$serial.DtrEnable = $false
$serial.RtsEnable = $false

Add-LogLine ("probe_start port={0} camera={1}" -f $Port, $CameraIndex)

$previousProfile = $null
$cameraOutput = $null

try {
    $serial.Open()
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    Read-SerialFor -Serial $serial -Milliseconds 250 | Out-Null

    Write-Command -Serial $serial -Command "~LED:STATUS"
    $initialStatus = Read-SerialFor -Serial $serial -Milliseconds 1200
    if ($initialStatus) {
        Add-LogLine "RX initial_status_begin"
        $lines.Add($initialStatus.TrimEnd())
        Add-LogLine "RX initial_status_end"
        if ($initialStatus -match "~LED:STATUS\s+profile=([a-zA-Z0-9_-]+)") {
            $previousProfile = $Matches[1]
            Add-LogLine ("detected_previous_profile={0}" -f $previousProfile)
        }
    }

    Write-Command -Serial $serial -Command "~LED:PROFILE factory"
    Start-Sleep -Milliseconds 350
    $profileResponse = Read-SerialFor -Serial $serial -Milliseconds 1000
    if ($profileResponse) {
        Add-LogLine "RX profile_response_begin"
        $lines.Add($profileResponse.TrimEnd())
        Add-LogLine "RX profile_response_end"
    }

    Write-Command -Serial $serial -Command "~LED:TEST:RGBW all"
    Start-Sleep -Milliseconds 1000
    $testResponse = Read-SerialFor -Serial $serial -Milliseconds 1200
    if ($testResponse) {
        Add-LogLine "RX test_response_begin"
        $lines.Add($testResponse.TrimEnd())
        Add-LogLine "RX test_response_end"
    }

    foreach ($command in @("~LED:STATUS", "~LED:BUDGET", "~BOARD:POWER", "~BOARD:STATUS", "~POWER:STATUS")) {
        Write-Command -Serial $serial -Command $command
        Start-Sleep -Milliseconds 150
    }
    $diagnosticsBeforeCamera = Read-SerialFor -Serial $serial -Milliseconds 2500
    if ($diagnosticsBeforeCamera) {
        Add-LogLine "RX diagnostics_before_camera_begin"
        $lines.Add($diagnosticsBeforeCamera.TrimEnd())
        Add-LogLine "RX diagnostics_before_camera_end"
    }

    $env:AIW_LED_CAMERA_INDEX = [string]$CameraIndex
    $env:AIW_LED_CAMERA_IMAGE = $imagePath
    $cameraOutput = @'
import os
import sys
import time

import cv2
import numpy as np

index = int(os.environ["AIW_LED_CAMERA_INDEX"])
image_path = os.environ["AIW_LED_CAMERA_IMAGE"]

cap = cv2.VideoCapture(index, cv2.CAP_DSHOW)
if not cap.isOpened():
    raise SystemExit(f"camera_open=FAIL index={index}")

try:
    frame = None
    for _ in range(12):
        ok, current = cap.read()
        if ok and current is not None:
            frame = current
        time.sleep(0.05)
finally:
    cap.release()

if frame is None:
    raise SystemExit(f"camera_capture=FAIL index={index}")

ok = cv2.imwrite(image_path, frame)
if not ok:
    raise SystemExit(f"camera_write=FAIL path={image_path}")

gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
height, width = gray.shape
mean_value = float(np.mean(gray))
max_value = int(np.max(gray))
p99_value = float(np.percentile(gray, 99))
bright_pixels = int(np.count_nonzero(gray >= 220))
print(f"camera_capture=PASS index={index} path={image_path}")
print(f"image_stats width={width} height={height} mean={mean_value:.2f} p99={p99_value:.2f} max={max_value} bright_pixels_ge_220={bright_pixels}")
'@ | python -
    Set-Content -LiteralPath $cameraLog -Value ($cameraOutput -join "`r`n") -Encoding UTF8
    foreach ($line in $cameraOutput) {
        Add-LogLine ("CAMERA {0}" -f $line)
    }

    foreach ($command in @("~LED:BUDGET", "~BOARD:POWER")) {
        Write-Command -Serial $serial -Command $command
        Start-Sleep -Milliseconds 150
    }
    $diagnosticsAfterCamera = Read-SerialFor -Serial $serial -Milliseconds 1600
    if ($diagnosticsAfterCamera) {
        Add-LogLine "RX diagnostics_after_camera_begin"
        $lines.Add($diagnosticsAfterCamera.TrimEnd())
        Add-LogLine "RX diagnostics_after_camera_end"
    }
} finally {
    if ($serial.IsOpen) {
        if ($previousProfile) {
            try {
                Write-Command -Serial $serial -Command ("~LED:PROFILE {0}" -f $previousProfile)
                Start-Sleep -Milliseconds 250
                $restoreResponse = Read-SerialFor -Serial $serial -Milliseconds 800
                if ($restoreResponse) {
                    Add-LogLine "RX restore_response_begin"
                    $lines.Add($restoreResponse.TrimEnd())
                    Add-LogLine "RX restore_response_end"
                }
            } catch {
                Add-LogLine ("restore_profile_error={0}" -f $_.Exception.Message)
            }
        }
        $serial.Close()
    }
    Add-LogLine "probe_end"
    Set-Content -LiteralPath $serialLog -Value ($lines -join "`r`n") -Encoding UTF8
}

Write-Output ("serial_log={0}" -f $serialLog)
Write-Output ("camera_log={0}" -f $cameraLog)
Write-Output ("camera_image={0}" -f $imagePath)
if ($cameraOutput) {
    $cameraOutput
}
