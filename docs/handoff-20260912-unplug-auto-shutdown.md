# 接手：拔电低功耗 / 自动关机（2026-09-12）

Owner 原话：拔掉电以后进不了低功耗，放一晚上没电，不能自动关机。插电模式不会自动关机。后来说可以 OTA、不要反复弹窗，拔电后盯日志。最后要先写接手文档。

本文件给下一会话继续用。产品政策仍以 `docs/features/low_power_wake_policy.md` 为准；这里只记本轮已证实的事实、包身份和未完成动作。

## 1. 现在板上是什么

| 端 | 身份 | 说明 |
|---|---|---|
| 键盘固件 | Type 日志 `build_id=cdc64303399222d0`，版本仍报 `1.0.5` | 2026-09-12 15:21 BLE OTA 成功的那包（bin SHA `7787C6D7…`）。**不是**工作树里最新的 `FF633939…` |
| Type | `C:\Program Files\Listener Type\listener-type.exe` SHA-256 `0AF03712F38BD9F84CD7FB486EE66AC86E7BF0180DFE6CD5C7EE9207F59F6199` | 已装 MSI `ListenerType_1.0.5_x64_en-US.msi` SHA-256 `ACA5AFEED26315C34D6343CA9F64C323494A5BBC226C44D922D8E667E45343E4` |
| 设备 BLE | `D936976C743C` 名 `listener` | 拔电后约 1 分钟会断 BLE；Type 会等 180s 再连 |

设备 NVS（BLE `DEVICE:SET` 已 ACK，USB 读回做不到）：

- `battery_low_power_idle_minutes=1`
- `battery_auto_shutdown_minutes=2`（测试值；出厂电池关机默认更长）
- `plugged_low_power_enabled=1`，`plugged_low_power_idle_minutes=1`
- `plugged_auto_shutdown_minutes=off`

插电不自动关机是产品合同，不要改回去。

## 2. 已经证实的现象

1. **隐藏唤醒曾挡住关机**：`VOICE_ACTIVATION` 进了 `shutdown_blockers`。`~POWER:STATUS` 在隐藏会话中曾看到 `blockers=external_power|voice_activation` 但修好后 `shutdown_blockers` 只剩 `external_power`。代码已进 `power_manager.c`（awake_blockers / 隐藏采音 cancel）。
2. **Type 曾把低功耗拉醒**：掉线后仍缓存 `usb_powered=Some(true)`，200ms 自动重连，日志 guidance「请保持设备唤醒」。Owner 说「自动开回来了」。已装 Type 会在非 USB 时 `holding BLE reconnect … to_ms=180000`。
3. **灯灭 ≠ 关机**：灭灯是 `CONNECTED_IDLE`。真关机是 `PWR_HOLD/GPIO9` 拉高。真关机后 Type 再连应失败。
4. **拔电后仍不关机（当前板上固件）**：Owner 确认 USB 已拔。日志两轮都是约 1 分钟断开 → Type 等 180s → **再连成功**。说明键盘还活着。
5. **根因（未刷到最新包）**：`BAT_STD` 在约 55% 电量仍会假报满电，`s_external_power_present` 保持 true，`power_manager_hardware_shutdown_ms()` 走插电超时 **0**，电池 2 分钟关机根本不会 evaluate。BLE `DEVICE:SET battery_auto_shutdown_minutes=2` 已 ACK，但超时选择仍按「插电」。

## 3. 工作树改了什么（未全部上板）

固件 `Listener-Firmware` 相对 `b92efa2` 未提交：

- `components/power_manager/power_manager.c`
  - LONG_IDLE 用 `awake_blockers`，忽略隐藏 VA；关机时 cancel 隐藏采音
  - `CONNECTED_IDLE` 且 BLE 仍连着时不要 `take_low_power`（避免 light-sleep 掉 BLE）
  - 无 SOF 且未充电时清掉假 `external_power`
  - **关键未上板**：`hardware_shutdown_ms` 用 `live_external = usb_power_present \|\| charging`，不再用 `s_external_power_present`
  - LONG_IDLE 关机门也只认 live USB/charging
- `components/board/board.c`：`usb_power_present = SOF && charging`（不再用 `vin_present`/`BAT_STD`）
- `tools/verify_power_manager_static.py`
- `tools/verify_charging_awake_policy_static.ps1`
- `tools/verify_device_settings_static.ps1`

Type `Listener-Type` 相对 `98b76ec` 未提交（已装到 Program Files）：

- `src-tauri/src/coordinator/embedded_ble_runtime.rs`
  - 掉线时把 `usb_powered` 置 `None`
  - `usb_powered != Some(true)` 且 link-loss 时重连等待 180s

## 4. OTA 包身份

| 时间 | zip | bin SHA-256 | 板上？ |
|---|---|---|---|
| 11:20 | `listener-ota-1.0.5-20260912-112033.zip` | `9DB8D962…` | 否（已被后续 OTA 替换） |
| 15:21 | `listener-ota-1.0.5-20260912-152126.zip` | `7787C6D7…` → `build_id=cdc64303399222d0` | **是，当前** |
| 15:30 | `listener-ota-1.0.5-20260912-153058.zip` | `FF6339391ED85A7D1D6694FD3678AF7AA50A13BD667137ACE1B468AAE98ACEF0` | **否**。含 live-external 关机超时修复。OTA 因低功耗不广播失败：`background Listener Type notify subscription was not ready before OTA` |

目录：`Listener-Firmware\.cache\ota_firmware\`  
最新 zip SHA-256：`183CEE5E86767070307F38851E88995EC95656E02B7AA80D59F4EDB99FB93AD7`

## 5. 下一会话立刻做什么

1. 键盘要 **ACTIVE**（按旋钮开机）。低功耗会停广播，`run-installed-ota-hidden.ps1` 会 15s 内失败。
2. 连上后马上 OTA（不要等 1 分钟灭灯）：

```powershell
$pkg = "C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware\.cache\ota_firmware\listener-ota-1.0.5-20260912-153058.zip"
$out = "C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware\tests\artifacts\ota-live-external-shutdown.json"
pwsh -NoProfile -File "C:\Users\Billy\Desktop\Denzic\Listener\Listener-Type\scripts\run-installed-ota-hidden.ps1" `
  -Package $pkg -OutputJson $out -StartupTimeoutSeconds 40
```

3. 成功后 Type 日志 `build_id` 必须 **不再是** `cdc64303399222d0`。
4. 保持拔电。看一轮：
   - 约 1 分钟：`Disconnected` + `holding BLE reconnect … 180000`
   - 约 2 分钟（从上次 user activity 起）：`PWR_HOLD` 关机
   - Type 180s 后再连应 **失败**（`recovered=true` 就是还没关）
5. 不要用 USB 串口刷，owner 明确走 OTA。
6. Owner 说过调试过程不要反复弹窗；机器闭环看 Type 日志即可。全部做完再按共用工程规则弹一次验收窗。

OTA 脚本会杀 Type 再拉 hidden 进程；结束后要重新启动  
`C:\Program Files\Listener Type\listener-type.exe`。

## 6. 日志怎么判

路径：`%LOCALAPPDATA%\Listener Type\Logs\listener-type.log`

- 新固件：`runtime identity … build_id=Some("…")` 且不是 `cdc64303399222d0`
- 低功耗：约 60s 后 `BLE device connection status changed to Disconnected` + `holding BLE reconnect`
- 被拉醒（回归）：断开后 **200ms** 内 `notify ready … recovered=true`（旧 Type）
- 真关机：断开后 180s 再连失败，或长时间没有新的 `build_id` / `notify ready recovered=true`
- 假关机：灯灭但 3 分钟后 `recovered=true`（当前板上就是这个）

## 7. 不要做的

- 不要把插电自动关机打开（合同：插电不自动关机）。
- 不要把 Type 180s hold 改回 200ms，除非另有 USB 插电快速恢复需求，且不能再把拔电低功耗拉醒。
- 不要重开 overlap-rearm 唤醒（已 revert，预览会被冲空）。
- 不要用主机麦 / seed 冒充板端录音验收。
- 测试用的 2 分钟电池关机不要当成出厂默认；出厂仍应是更长的电池关机。验过后再决定是否改回 10 分钟。

## 8. 相关文件

- 政策：`docs/features/low_power_wake_policy.md`
- 电源：`components/power_manager/power_manager.c`
- USB 检测：`components/board/board.c`
- Type 重连：`Listener-Type/src-tauri/src/coordinator/embedded_ble_runtime.rs`
- OTA 隐式跑：`Listener-Type/scripts/run-installed-ota-hidden.ps1`
