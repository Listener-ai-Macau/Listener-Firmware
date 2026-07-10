# Listener Device Maintenance

这个入口用于离开 AI 后手动维护一块 Listener 键盘板：找串口、探测 ESP32-S3、检查 flash 头部、刷正常固件、恢复 bootloader、擦 OTA 选择区。

在仓库根目录运行：

```powershell
pwsh -NoProfile -File .\tools\device_maintenance.ps1 -Action help
```

## 常用只读检查

列出当前串口：

```powershell
pwsh -NoProfile -File .\tools\device_maintenance.ps1 -Action ports
```

探测芯片、flash ID、MAC：

```powershell
pwsh -NoProfile -File .\tools\device_maintenance.ps1 -Action probe -Port COMx
```

读取 flash 关键头部并保存证据，不写 flash：

```powershell
pwsh -NoProfile -File .\tools\device_maintenance.ps1 -Action check-flash -Port COMx
```

`COMx` 会自动解析为当前唯一的 ESP32-S3 串口；如果机器上有多个串口，改成明确端口，例如 `-Port COM9`。

## 正常刷回固件

构建并刷入当前源码的正常固件。默认会先擦 `otadata`，避免启动到旧 OTA 分区：

```powershell
pwsh -NoProfile -File .\tools\device_maintenance.ps1 -Action flash -Port COMx
```

如果已经构建好，只想快速重刷：

```powershell
pwsh -NoProfile -File .\tools\device_maintenance.ps1 -Action flash -Port COMx -NoBuild
```

如果特意保留 OTA 选择区：

```powershell
pwsh -NoProfile -File .\tools\device_maintenance.ps1 -Action flash -Port COMx -PreserveOtaData
```

## 关闭自动关机并手动测试

调试 PWR_HOLD/IO9 时，不要再刷“启动后等待 10 秒自动关机”的临时固件。刷回正常固件后，用串口先关闭长 idle 自动关机，再按需触发一次真实硬件关机：

```text
~DEVICE:SET auto_shutdown_minutes=off
~DEVICE:SET plugged_auto_shutdown_minutes=off
~DEVICE:SETTINGS
~POWER:STATUS
~POWER:TEST:SHUTDOWN
```

`~POWER:TEST:SHUTDOWN` 和 `~POWER:SHUTDOWN` 等效，都是串口手动测试命令；`auto_shutdown_minutes=off` 关闭电池模式 inactivity 自动关机，`plugged_auto_shutdown_minutes=off` 关闭插电模式 inactivity 自动关机，都不会屏蔽手动测试。

## 恢复 bootloader / OTA 选择区

只恢复 bootloader：

```powershell
pwsh -NoProfile -File .\tools\device_maintenance.ps1 -Action restore-bootloader -Port COMx
```

只擦 OTA 选择区，让下次启动回到新刷的 `ota_0`：

```powershell
pwsh -NoProfile -File .\tools\device_maintenance.ps1 -Action erase-otadata -Port COMx
```

## 救砖顺序

1. 接上 USB-C，短按电源键让板子上电。
2. 运行 `-Action ports`，确认有一个 ESP32-S3 串口。
3. 如果没有串口，按住 BOOT/下载键再点 RESET，或按板子的下载模式方法重新枚举。
4. 运行 `-Action probe -Port COMx`，确认 esptool 能连接。
5. 运行 `-Action restore-bootloader -Port COMx`。
6. 运行 `-Action erase-otadata -Port COMx`。
7. 运行 `-Action flash -Port COMx` 刷回完整固件。
8. 运行 `-Action check-flash -Port COMx` 保存检查证据。

## 危险操作

整片擦除会删除 NVS、配对、OTA、诊断日志和应用分区，只在救砖时使用：

```powershell
pwsh -NoProfile -File .\tools\device_maintenance.ps1 -Action erase-flash -Port COMx -ConfirmEraseFlash
```

擦完整片 flash 后，需要再运行：

```powershell
pwsh -NoProfile -File .\tools\device_maintenance.ps1 -Action flash -Port COMx
```
