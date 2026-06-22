Add-Type -AssemblyName System.Windows.Forms
$msg = '现在请拔掉 Listener 的 USB-C 线，再插回电脑。等待 Windows 重新识别出 USB 串行设备 COM10，并看到设备灯恢复后，点击“确定”。我会继续抓取日志；不需要重复已经通过的人眼灯效动作。'
[System.Windows.Forms.MessageBox]::Show($msg, 'Listener 串口恢复操作', [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Information, [System.Windows.Forms.MessageBoxDefaultButton]::Button1, [System.Windows.Forms.MessageBoxOptions]::ServiceNotification) | Out-Null
