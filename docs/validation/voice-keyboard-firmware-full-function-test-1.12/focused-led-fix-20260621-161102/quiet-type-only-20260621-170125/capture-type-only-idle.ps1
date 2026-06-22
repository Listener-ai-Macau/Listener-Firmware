$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$typeRepo = 'C:\Users\Billy\Desktop\Denzic\Listener\Listener-Type'
$outDir = 'C:\\Users\\Billy\\Desktop\\Denzic\\Listener\\Listener-Firmware-pwr-shutdown-new-board\\docs\\validation\\voice-keyboard-firmware-full-function-test-1.12\\focused-led-fix-20260621-161102\\quiet-type-only-20260621-170125'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$runLog = Join-Path $outDir 'capture-run.log'
function Add-RunLog { param([string]$Message) $line = "{0} {1}" -f (Get-Date).ToString('o'), $Message; $line | Tee-Object -FilePath $runLog -Append }
function Test-TcpPort { param([string]$HostName, [int]$Port) $client=$null; try { $client=[System.Net.Sockets.TcpClient]::new(); $async=$client.BeginConnect($HostName,$Port,$null,$null); if(-not $async.AsyncWaitHandle.WaitOne(500)){return $false}; $client.EndConnect($async); return $true } catch { return $false } finally { if($client){$client.Close()} } }
function Stop-ProcessTree { param([int]$ProcessId) $children=@(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object { $_.ParentProcessId -eq $ProcessId }); foreach($child in $children){ Stop-ProcessTree -ProcessId ([int]$child.ProcessId) }; Stop-Process -Id $ProcessId -Force -ErrorAction SilentlyContinue }
function Read-LogFromOffset { param([string]$Path,[ref]$Offset) if(-not (Test-Path -LiteralPath $Path)){return ''}; $fs=[System.IO.FileStream]::new($Path,[System.IO.FileMode]::Open,[System.IO.FileAccess]::Read,[System.IO.FileShare]::ReadWrite); try { if($fs.Length -lt $Offset.Value){$Offset.Value=0}; [void]$fs.Seek([int64]$Offset.Value,[System.IO.SeekOrigin]::Begin); $remaining=[int]($fs.Length-$Offset.Value); if($remaining -le 0){return ''}; $buffer=New-Object byte[] $remaining; $read=$fs.Read($buffer,0,$remaining); $Offset.Value=$fs.Position; return [System.Text.Encoding]::UTF8.GetString($buffer,0,$read) } finally { $fs.Dispose() } }
$promptScript = Join-Path $outDir 'operator-prompt.ps1'
@"
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
`$form = [System.Windows.Forms.Form]::new()
`$form.Text = 'Listener Type 日志抓取'
`$form.TopMost = `$true
`$form.StartPosition = 'CenterScreen'
`$form.Width = 620
`$form.Height = 220
`$label = [System.Windows.Forms.Label]::new()
`$label.Dock = 'Fill'
`$label.Padding = [System.Windows.Forms.Padding]::new(18)
`$label.Font = [System.Drawing.Font]::new('Microsoft YaHei UI', 11)
`$label.Text = '正在自动抓取 Type/BLE idle 日志约 90 秒。请暂时不要触碰 Listener、Type 窗口或 Windows 蓝牙设置。结束后我会在聊天里汇报；不需要重复已经通过的灯效验收。'
`$form.Controls.Add(`$label)
`$timer = [System.Windows.Forms.Timer]::new()
`$timer.Interval = 95000
`$timer.Add_Tick({ `$timer.Stop(); `$form.Close() })
`$timer.Start()
[void]`$form.ShowDialog()
"@ | Set-Content -LiteralPath $promptScript -Encoding UTF8
$promptProc=$null; $viteProc=$null; $appProcess=$null; $startedVite=$false
$oldHideMain=$env:LISTENER_TYPE_HIDE_MAIN_ON_START; $oldShowMain=$env:LISTENER_TYPE_SHOW_MAIN_ON_START; $oldRecordEmbedded=$env:LISTENER_TYPE_RECORD_EMBEDDED_AUDIO_FOR_DEBUG
$listenerCapturedLog = Join-Path $outDir 'listener-type-idle.log'
$listenerStdout = Join-Path $outDir 'listener-type-app.stdout.log'
$listenerStderr = Join-Path $outDir 'listener-type-app.stderr.log'
$startedAt = Get-Date
$notifyReady=$false; $powerCached=$false
try {
    Add-RunLog 'visible Chinese no-touch prompt launching'
    $promptProc = Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',$promptScript) -WindowStyle Normal -PassThru
    if(-not (Test-TcpPort -HostName '127.0.0.1' -Port 1420)){
        Add-RunLog 'starting temporary Vite dev server'
        $viteProc=Start-Process -FilePath 'cmd.exe' -ArgumentList @('/c','npm run dev -- --host 127.0.0.1 --port 1420') -WorkingDirectory $typeRepo -WindowStyle Hidden -RedirectStandardOutput (Join-Path $outDir 'vite.stdout.log') -RedirectStandardError (Join-Path $outDir 'vite.stderr.log') -PassThru
        $startedVite=$true
        $deadline=(Get-Date).AddSeconds(35)
        while((Get-Date) -lt $deadline -and -not (Test-TcpPort -HostName '127.0.0.1' -Port 1420)){ Start-Sleep -Milliseconds 500 }
        if(-not (Test-TcpPort -HostName '127.0.0.1' -Port 1420)){ throw 'Vite dev server did not open port 1420' }
    }
    $existing=@(Get-Process listener-type -ErrorAction SilentlyContinue)
    if($existing.Count -gt 0){ Add-RunLog ("stopping pre-existing listener-type processes: {0}" -f (($existing|ForEach-Object Id)-join ',')); $existing|Stop-Process -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 800 }
    $logPath=Join-Path $env:LOCALAPPDATA 'Listener Type\Logs\listener-type.log'
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $logPath) | Out-Null
    $offsetValue=if(Test-Path -LiteralPath $logPath){(Get-Item -LiteralPath $logPath).Length}else{0}; $offset=[ref]$offsetValue
    $env:LISTENER_TYPE_HIDE_MAIN_ON_START='1'; Remove-Item Env:LISTENER_TYPE_SHOW_MAIN_ON_START -ErrorAction SilentlyContinue; $env:LISTENER_TYPE_RECORD_EMBEDDED_AUDIO_FOR_DEBUG='1'
    $listenerExe=Join-Path $typeRepo 'src-tauri\target\debug\listener-type.exe'
    Add-RunLog 'starting Listener-Type debug app hidden'
    $appProcess=Start-Process -FilePath (Resolve-Path -LiteralPath $listenerExe).Path -WorkingDirectory $typeRepo -WindowStyle Hidden -RedirectStandardOutput $listenerStdout -RedirectStandardError $listenerStderr -PassThru
    $captured=''
    $deadline=(Get-Date).AddSeconds(35)
    while((Get-Date) -lt $deadline){ Start-Sleep -Milliseconds 500; $captured += Read-LogFromOffset -Path $logPath -Offset $offset; if($captured -match '\[embedded-ble\].*background listener notify ready'){ $notifyReady=$true; break }; if($appProcess.HasExited){ throw "Listener-Type exited before notify ready. exit=$($appProcess.ExitCode)" } }
    if($notifyReady){ Add-RunLog 'background listener notify ready observed' } else { Add-RunLog 'notify ready not observed before idle wait' }
    $deadline=(Get-Date).AddSeconds(90)
    while((Get-Date) -lt $deadline){ Start-Sleep -Seconds 1; $captured += Read-LogFromOffset -Path $logPath -Offset $offset; if($captured -match 'cached firmware power state reason=startup_embedded_ble_power_probe usb_powered=Some\(true\)'){ $powerCached=$true }; if($appProcess.HasExited){ Add-RunLog "Listener-Type exited during idle wait exit=$($appProcess.ExitCode)"; break } }
    $captured += Read-LogFromOffset -Path $logPath -Offset $offset
    $captured | Set-Content -LiteralPath $listenerCapturedLog -Encoding UTF8
} finally {
    if($null -eq $oldHideMain){ Remove-Item Env:LISTENER_TYPE_HIDE_MAIN_ON_START -ErrorAction SilentlyContinue } else { $env:LISTENER_TYPE_HIDE_MAIN_ON_START=$oldHideMain }
    if($null -eq $oldShowMain){ Remove-Item Env:LISTENER_TYPE_SHOW_MAIN_ON_START -ErrorAction SilentlyContinue } else { $env:LISTENER_TYPE_SHOW_MAIN_ON_START=$oldShowMain }
    if($null -eq $oldRecordEmbedded){ Remove-Item Env:LISTENER_TYPE_RECORD_EMBEDDED_AUDIO_FOR_DEBUG -ErrorAction SilentlyContinue } else { $env:LISTENER_TYPE_RECORD_EMBEDDED_AUDIO_FOR_DEBUG=$oldRecordEmbedded }
    if($appProcess -and -not $appProcess.HasExited){ Add-RunLog ("stopping Listener-Type pid={0}" -f $appProcess.Id); Stop-Process -Id $appProcess.Id -Force -ErrorAction SilentlyContinue }
    if($startedVite -and $viteProc -and -not $viteProc.HasExited){ Add-RunLog ("stopping temporary Vite process tree pid={0}" -f $viteProc.Id); Stop-ProcessTree -ProcessId $viteProc.Id }
    if($promptProc -and -not $promptProc.HasExited){ Stop-Process -Id $promptProc.Id -Force -ErrorAction SilentlyContinue }
}
$finishedAt=Get-Date
$listenerText=if(Test-Path -LiteralPath $listenerCapturedLog){Get-Content -Raw -LiteralPath $listenerCapturedLog}else{''}
$analysis=[ordered]@{
    status='PASS'
    started_at_utc=$startedAt.ToUniversalTime().ToString('o')
    finished_at_utc=$finishedAt.ToUniversalTime().ToString('o')
    out_dir=$outDir
    notify_ready_observed=[bool]$notifyReady
    type_startup_power_probe_seen=[bool]($listenerText -match 'firmware power probe running for existing embedded BLE source')
    type_cached_usb_powered_true_seen=[bool]($listenerText -match 'cached firmware power state reason=startup_embedded_ble_power_probe usb_powered=Some\(true\)')
    type_low_power_idle_disconnect_seen=[bool]($listenerText -match 'LowPowerIdleDisconnect|low_power_idle=true|低功耗断开')
    type_paired_but_disconnected_seen=[bool]($listenerText -match 'PairedButDisconnected')
    type_reason_546_seen=[bool]($listenerText -match 'reason=546')
    recovery_reconnected_seen=[bool]($listenerText -match 'recovery capsule state=reconnected emitted=true')
    serial_blocked_note='COM10 CDC remained Unknown / SetCommState failed in preceding capture; firmware serial evidence unavailable without board/USB recovery.'
    artifacts=@($runLog,$listenerCapturedLog,$listenerStdout,$listenerStderr)
}
if(-not $analysis.notify_ready_observed -or -not $analysis.type_startup_power_probe_seen){ $analysis.status='WARN' }
if($analysis.type_low_power_idle_disconnect_seen -or $analysis.type_reason_546_seen){ $analysis.status='FAIL' }
$analysisPath=Join-Path $outDir 'type-idle-analysis.json'
($analysis|ConvertTo-Json -Depth 5)|Set-Content -LiteralPath $analysisPath -Encoding UTF8
$summaryPath=Join-Path $outDir 'type-idle-summary.md'
@(
'# Type/BLE Quiet Idle Log Capture','',
('status: {0}' -f $analysis.status),
('notify_ready_observed: {0}' -f $analysis.notify_ready_observed),
('type_startup_power_probe_seen: {0}' -f $analysis.type_startup_power_probe_seen),
('type_cached_usb_powered_true_seen: {0}' -f $analysis.type_cached_usb_powered_true_seen),
('type_low_power_idle_disconnect_seen: {0}' -f $analysis.type_low_power_idle_disconnect_seen),
('type_paired_but_disconnected_seen: {0}' -f $analysis.type_paired_but_disconnected_seen),
('type_reason_546_seen: {0}' -f $analysis.type_reason_546_seen),
('recovery_reconnected_seen: {0}' -f $analysis.recovery_reconnected_seen),
'',
'Serial note: COM10 CDC stayed unavailable after PnP restart and two operator replug prompts; no firmware serial proof was added in this run.',
'',
('Artifacts: {0}, {1}' -f $analysisPath,$listenerCapturedLog)
)|Set-Content -LiteralPath $summaryPath -Encoding UTF8
Add-RunLog ("analysis={0}" -f $analysisPath)
Write-Host ("OUT_DIR={0}" -f $outDir)
Write-Host ("ANALYSIS={0}" -f $analysisPath)
Write-Host ("SUMMARY={0}" -f $summaryPath)
