# 出厂固件镜像与 BLE 就绪身份

## 范围

3.2 的固件出厂镜像交付覆盖四件事：

- 固件包包含 bootloader、partition table、app 三个二进制，带 SHA256、版本、刷写偏移和刷写命令。
- 首次上电无需串口命令，设备进入可发现/可配对 BLE HID 广播。
- BLE 身份稳定：名称 `listener`，Appearance `0x03C1` keyboard，HID service `0x1812`，音频服务 `710af845-6d9f-6583-0c4d-9e5b3bc3091a`。
- POST 失败不阻塞启动；关键失败会记录日志，固件继续进入 degraded BLE mode，让状态仍可被观察。

## BLE 可读信息

标准 Device Information Service 暴露：

- Manufacturer: `listener`
- Model Number: `keyboard-v1`
- Hardware Revision: `esp32s3-devkit`
- Firmware Revision: 构建注入的 `git describe --tags --always --dirty`
- Software Revision: 协议版本 `1`
- PnP ID: VID `0x16C0` / PID `0x05DF` / product version `1`

自定义音频服务额外提供两个只读特征：

- Readiness: `710af845-6d9f-6583-0c4d-9e5b3bc3091c`
- Capabilities: `710af845-6d9f-6583-0c4d-9e5b3bc3091d`

当前值：

```text
factory_ready;pairable_on_boot;post_degraded_boot
ble_hid_keyboard;ble_audio_vka1;usb_serial_text;key1_record_toggle;post_status
```

## 打包

出厂镜像包由以下命令生成：

```powershell
pwsh -NoProfile -File .\tools\package_factory_firmware.ps1
```

默认输出到 `.cache\factory_firmware\listener-factory-<version>-<timestamp>\`，包含：

- `bootloader.bin`，刷写偏移 `0x0`
- `partition-table.bin`，刷写偏移 `0x8000`
- `voice-keyboard-firmware.bin`，刷写偏移 `0x10000`
- `manifest.json`
- `FLASHING.md`

`manifest.json` 记录版本、git commit、dirty 状态、BLE 身份、DIS 字段、只读特征 UUID、每个二进制的 SHA256 和刷写命令。

## 主要路径

- `protocols/listener_device/`
- `ports/esp32/ble_hid/ble_hid.c`
- `ports/esp32/ble_audio_stream/`
- `main/main.c`
- `components/self_test/`
- `tools/package_factory_firmware.ps1`

## 验证

```powershell
idf.py build
pwsh -NoProfile -File .\tools\package_factory_firmware.ps1
powershell -ExecutionPolicy Bypass -File .\tools\verify_ble_hid.ps1 -Port COM3 -Text "hello"
```

人工 BLE 扫描确认：

- 名称为 `listener`
- Appearance 为 keyboard `0x03C1`
- DIS Firmware Revision 等于包内版本
- DIS Software Revision 为协议版本 `1`
- 自定义 Readiness / Capabilities 特征可读

## 不变量

- `listener` 是出厂 BLE 名称；脚本和产品矩阵默认依赖该名称。
- 广播包保留 `flags + appearance + 16-bit HID UUID + listener`，音频服务 UUID 保留在 scan response。
- POST 不应因为 NVS 首烧初始化、SPIRAM 等状态直接挂死主流程；失败需要日志可见并继续进入 BLE 暴露状态。
- 出厂包不提交二进制到 git，包产物位于 `.cache\factory_firmware\`。

## 已知限制

- Readiness/Capabilities 是静态出厂能力声明，不替代完整运行期诊断包。
- 真实电量仍取决于后续 ADC 电池读取实现；本能力声明没有声明 battery_level。
