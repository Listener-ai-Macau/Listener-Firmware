# V1 剩余能力补齐（协议身份 + 开机自检 + Host 文本注入）

完成时间：2026-05-19
计划：p15_v1_remaining_capabilities

## 做了什么

三个独立能力，全部仅涉及固件侧：

1. **协议身份 + 构建版本注入** — `protocols/listener_device/` 组件，从 git 注入 `fw_version`（短 hash）和 `protocol_version`（整数）到固件二进制；BLE DIS（Device Information Service）暴露 manufacturer / model / serial / firmware revision 字段。
2. **开机自检（POST）** — `components/self_test/` 组件，在 `app_main()` 中非阻塞检查 NVS / SPIRAM / heap，通过 ESP_LOG 输出 `POST: nvs=OK ble=PENDING audio=PENDING spiram=OK heap=xxx fw=xxx proto=1`。
3. **Host 端文本注入工具** — `tools/inject_text.py`，通过 USB Serial JTAG 向设备发送文本，设备经 BLE HID 键盘输出到光标。支持 `--mode type`（逐字符模拟按键）和 `--mode paste`（剪贴板粘贴中文）。

## 代码在哪

| 能力 | 路径 |
|---|---|
| 协议身份 | `protocols/listener_device/`，`ports/esp32/ble_hid/ble_hid.c`（DIS 注册） |
| 开机自检 | `components/self_test/`，`main/main.c`（调用入口） |
| 文本注入 | `tools/inject_text.py` |
| 版本注入 | `CMakeLists.txt`（git describe → cmake define） |

## 如何验证

```powershell
# 构建烧录
idf.py build && idf.py -p COM3 flash

# 串口观察 POST + 版本
# 期望: self_test: POST: nvs=... spiram=OK ... fw=<hash> proto=1
# 期望: ble_hid: fw_version=<hash> protocol_version=1 build=... serial=...

# BLE 连接 + A1 音频矩阵
python tools/verify_audio_ble_product_matrix.py --port COM3 --cases A1 --capture-seconds 5 --fail-on-warning

# 文本注入
python tools/inject_text.py --mode type --text "hello"
python tools/inject_text.py --mode paste --text "你好"
```

## 已知限制

- NVS POST 在首次烧录后会报 FAIL（分区未格式化），不影响功能，写入一次后即 OK。
- 文本注入依赖 USB Serial JTAG 连接，不适用于纯 BLE 场景。
- `inject_text.py` 使用 `pyautogui`，需要焦点在目标窗口。

## 已验证

- 真机验证 2026-05-19：master (ab64c02)，POST 输出正常，BLE DIS 可查，A1 pass，type/paste 正常。
