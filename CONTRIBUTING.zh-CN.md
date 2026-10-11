# 参与固件开发

**简体中文** · [英文](CONTRIBUTING.md)

本仓库使用 ESP-IDF，提供 Listener 语音键盘固件。

## 开发与验证

```powershell
git submodule update --init --recursive
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
```

子模块命令检出仓库记录的准确 Denzic Platform 版本。`managed_components/` 是自动生成的组件依赖目录，不应提交。

## 目录与变更要求

- `components/`：可复用的控件、电源、设置、健康、更新和诊断逻辑。
- `ports/esp32/`：硬件、音频、蓝牙和存储适配；`protocols/`：共享设备消息与编解码。
- `main/`：启动和组件连接；`tools/`：安装、构建、刷写、监控、打包与验证入口。

桌面识别、文字风格、最终转写归属与光标写入由 Listener Type 处理。固件提供设备和传输证据。保留硬件适配与可复用逻辑的边界；不要提交构建目录、串口日志、诊断包、验证产物或固件二进制。说明实际验证过的硬件路径，以及音频、蓝牙、电源和更新相关的计数或灯光顺序。

[返回中文键盘首页](README.zh-CN.md)。
