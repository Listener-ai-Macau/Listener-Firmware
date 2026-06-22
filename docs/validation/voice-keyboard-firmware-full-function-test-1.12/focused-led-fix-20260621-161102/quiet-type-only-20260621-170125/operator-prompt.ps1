Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$form = [System.Windows.Forms.Form]::new()
$form.Text = 'Listener Type 日志抓取'
$form.TopMost = $true
$form.StartPosition = 'CenterScreen'
$form.Width = 620
$form.Height = 220
$label = [System.Windows.Forms.Label]::new()
$label.Dock = 'Fill'
$label.Padding = [System.Windows.Forms.Padding]::new(18)
$label.Font = [System.Drawing.Font]::new('Microsoft YaHei UI', 11)
$label.Text = '正在自动抓取 Type/BLE idle 日志约 90 秒。请暂时不要触碰 Listener、Type 窗口或 Windows 蓝牙设置。结束后我会在聊天里汇报；不需要重复已经通过的灯效验收。'
$form.Controls.Add($label)
$timer = [System.Windows.Forms.Timer]::new()
$timer.Interval = 95000
$timer.Add_Tick({ $timer.Stop(); $form.Close() })
$timer.Start()
[void]$form.ShowDialog()
