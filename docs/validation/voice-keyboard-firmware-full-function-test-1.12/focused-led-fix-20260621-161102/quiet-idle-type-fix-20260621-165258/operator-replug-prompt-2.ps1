Add-Type -AssemblyName System.Windows.Forms
$msg = 'COM10 仍然是 Unknown，没有恢复。请现在拔掉连接电脑的 Listener USB-C 线，等 2 秒，再插回电脑。等设备灯恢复、Windows 重新识别后，再点击“确定”。不要按设备按键，不需要重复灯效验收。'
[System.Windows.Forms.MessageBox]::Show($msg, 'Listener 串口仍未恢复', [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Warning, [System.Windows.Forms.MessageBoxDefaultButton]::Button1, [System.Windows.Forms.MessageBoxOptions]::ServiceNotification) | Out-Null
