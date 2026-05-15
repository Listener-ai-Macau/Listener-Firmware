param(
    [switch]$RestartPanAdapter = $false,
    [int]$ServiceRestartRetryCount = 3,
    [int]$RetryDelaySeconds = 2
)

$ErrorActionPreference = "Stop"

function Restart-BluetoothService {
    param(
        [int]$RetryCount,
        [int]$RetryDelaySeconds
    )

    $last_error = $null
    for ($attempt = 1; $attempt -le $RetryCount; $attempt++) {
        try {
            Write-Host "restart_windows_bluetooth: restarting bthserv attempt=$attempt/$RetryCount"
            Restart-Service -Name bthserv -Force -ErrorAction Stop
            Start-Sleep -Seconds 1

            $service = Get-Service -Name bthserv -ErrorAction Stop
            if ($service.Status -eq "Running") {
                Write-Host "restart_windows_bluetooth: bthserv running after restart"
                return
            }

            throw "bthserv status is $($service.Status) after restart"
        } catch {
            $last_error = $_
            Write-Warning "restart_windows_bluetooth: bthserv restart attempt $attempt failed: $($_.Exception.Message)"

            try {
                $service = Get-Service -Name bthserv -ErrorAction Stop
                if ($service.Status -ne "Running") {
                    Write-Host "restart_windows_bluetooth: starting bthserv explicitly"
                    Start-Service -Name bthserv -ErrorAction Stop
                    Start-Sleep -Seconds 1
                    $service = Get-Service -Name bthserv -ErrorAction Stop
                    if ($service.Status -eq "Running") {
                        Write-Host "restart_windows_bluetooth: bthserv recovered via explicit start"
                        return
                    }
                } else {
                    Write-Warning "restart_windows_bluetooth: bthserv already running; treating restart as recovered"
                    return
                }
            } catch {
                Write-Warning "restart_windows_bluetooth: explicit start check failed: $($_.Exception.Message)"
            }

            if ($attempt -lt $RetryCount) {
                Start-Sleep -Seconds $RetryDelaySeconds
            }
        }
    }

    throw $last_error
}

Restart-BluetoothService -RetryCount $ServiceRestartRetryCount -RetryDelaySeconds $RetryDelaySeconds

if ($RestartPanAdapter) {
    try {
        $pan_adapter = Get-NetAdapter -IncludeHidden |
            Where-Object { $_.InterfaceDescription -eq "Bluetooth Device (Personal Area Network)" } |
            Select-Object -First 1

        if ($null -ne $pan_adapter) {
            Write-Host "restart_windows_bluetooth: cycling PAN adapter $($pan_adapter.Name)"
            Disable-NetAdapter -Name $pan_adapter.Name -Confirm:$false -ErrorAction Stop
            Start-Sleep -Seconds 2
            Enable-NetAdapter -Name $pan_adapter.Name -Confirm:$false -ErrorAction Stop
        } else {
            Write-Warning "restart_windows_bluetooth: Bluetooth PAN adapter not found"
        }
    } catch {
        Write-Warning "restart_windows_bluetooth: PAN adapter cycle failed: $($_.Exception.Message)"
    }
}

Write-Host "restart_windows_bluetooth: done"
