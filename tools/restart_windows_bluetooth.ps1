param(
    [switch]$RestartPanAdapter = $false
)

$ErrorActionPreference = "Stop"

Write-Host "restart_windows_bluetooth: restarting bthserv"
Restart-Service -Name bthserv -Force

if ($RestartPanAdapter) {
    $pan_adapter = Get-NetAdapter -IncludeHidden |
        Where-Object { $_.InterfaceDescription -eq "Bluetooth Device (Personal Area Network)" } |
        Select-Object -First 1

    if ($null -ne $pan_adapter) {
        Write-Host "restart_windows_bluetooth: cycling PAN adapter $($pan_adapter.Name)"
        Disable-NetAdapter -Name $pan_adapter.Name -Confirm:$false
        Start-Sleep -Seconds 2
        Enable-NetAdapter -Name $pan_adapter.Name -Confirm:$false
    } else {
        Write-Warning "restart_windows_bluetooth: Bluetooth PAN adapter not found"
    }
}

Write-Host "restart_windows_bluetooth: done"
