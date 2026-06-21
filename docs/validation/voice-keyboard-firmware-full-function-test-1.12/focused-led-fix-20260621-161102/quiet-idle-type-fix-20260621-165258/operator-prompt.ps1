Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$form = [System.Windows.Forms.Form]::new()
$form.Text = 'Listener 验证提示'
$form.TopMost = $true
$form.StartPosition = 'CenterScreen'
$form.Width = 620
$form.Height = 230
$label = [System.Windows.Forms.Label]::new()
$label.Dock = 'Fill'
$label.Padding = [System.Windows.Forms.Padding]::new(18)
$label.Font = [System.Drawing.Font]::new('Microsoft YaHei UI', 11)
$label.Text = '正在自动抓取 Listener BLE idle 日志约 2 分钟。请暂时不要触碰设备、Type 窗口或 Windows 蓝牙设置。成功证据：结束后聊天里会报告，日志里应看到外接电 idle 保持 active BLE 参数，Type 不再把普通断开误报为低功耗断开。'
$form.Controls.Add($label)
$timer = [System.Windows.Forms.Timer]::new()
$timer.Interval = 145000
$timer.Add_Tick({ $timer.Stop(); $form.Close() })
$timer.Start()
[void]$form.ShowDialog()
