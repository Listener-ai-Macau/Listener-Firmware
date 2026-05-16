# BLE 音频回归与按键检测修复计划

## 任务目标

修复 `dfc146f` 之后完整回归测试不稳定的问题，并把 BLE 设备名统一为 `listener`。

## 观察到的问题

- `f33228d5a6d9c5493b949d9d9cc09758cdb244e8` 能通过完整回归测试。
- 当前 `HEAD=dfc146f` 在 P1 BLE 音频回归中能建立连接并开始录音，但出现大量 `notify failed after retries`。
- P1 证据显示 `expected_packet_count=480`，设备端累计 `audio data packet dropped=121`，丢包率约 25%。
- `dfc146f` 把按键扫描从普通 FreeRTOS 任务改成了 `esp_timer` 回调，并通过 `esp_io_expander_gpio_wrapper` 把虚拟 GPIO 的 `gpio_get_level()` 转成同步 I2C 读取。
- 当前 sdkconfig 启用了 `CONFIG_BT_NIMBLE_USE_ESP_TIMER=y`，在 `esp_timer` 回调里做 I2C 轮询会干扰 BLE notify 时序。
- 固件 BLE 名称已经从旧名称变化过，测试脚本默认设备名需要和固件一致。

## 修复范围

- 固件 BLE 设备名改为 `listener`。
- 主机侧 BLE 测试脚本默认设备名改为 `listener`。
- `ports/esp32/voice_key_input/voice_key_input_esp32.c` 改回 FreeRTOS 任务轮询。
- 继续使用官方 `esp_io_expander_get_level()` 读取 XL9555/TCA9555 输入，不再通过 GPIO wrapper 在 timer 回调里读 expander。
- 使用 FreeRTOS semaphore 传递按键 toggle 事件，避免旧的裸 `volatile` 计数。

## 不在本次范围内

- 不切换到新原理图 `GPIO45` 直接按键路径。
- 不重写 BLE audio session notify 协议。
- 不改变 host 订阅顺序 `CCCD notify -> ValueChanged`。
- 不删除 subscribe-before-connect 兼容路径。
- 不调整 SPH0645 / ES8311 音频硬件路径。

## 执行步骤与验收

### P1 统一 BLE 名称

预计改动：

- `ports/esp32/ble_hid/ble_hid.c`
- `tools/*` 中所有 BLE 测试脚本默认 `DeviceName` / `--device-name`
- 相关功能文档中的设备名示例

验收：

- `rg --fixed-strings '"listener"' ports tools` 能命中固件和测试脚本默认设备名。
- `rg --fixed-strings 'Listener Keyboard' .` 不再命中。

### P2 修复按键检测调度方式

预计改动：

- `ports/esp32/voice_key_input/voice_key_input_esp32.c`

验收：

- 不再包含 `esp_timer` 和 `esp_io_expander_gpio_wrapper`。
- 按键轮询任务中每轮最多一次 `esp_io_expander_get_level()` 读取 expander 输入。
- `voice_key_input_take_toggle_event()` 通过 FreeRTOS semaphore 等待事件。
- `powershell -ExecutionPolicy Bypass -File .\tools\test.ps1` 通过。

### P3 硬件回归验证

预计命令：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash.ps1 -Port COM3
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases P1 --capture-seconds 5 --random-seed 20260516 --no-reset-before-capture --skip-preflight-recover
```

验收：

- P1 不再出现大量 `notify failed after retries`。
- `missing_packet_count=0` 或满足当前矩阵阈值。
- 如果 P1 通过，再运行完整矩阵。

## 已知风险

- 当前修复保留旧板 XL9555/TCA9555 和 `GPIO0` fallback，只解决这次回归；新原理图 `GPIO45` 直接按键另按 `schematic_v1_software_adaptation_plan.md` 执行。
- BLE 名称改成小写后，Windows 可能保留旧配对缓存；必要时需要跑 host recovery 或重新配对。
