# 设备端音频采集、录音控制与 BLE 上传链路

## 状态

- 状态：`可用`
- 定位：`设备端语音输入主链路的前置验证能力`
- 阶段结论：`已收敛到连续 notify 会话流模型，可作为后续 Windows 常驻接收端 / ASR 接入的稳定基线`

## 链路

`KEY1 / 串口 toggle -> voice_recording_control -> audio_capture -> ble_audio_stream -> WinRT GATT client -> session 重组 -> wav`

- 用户语义：`KEY1` 按一下开始录音，再按一下结束录音
- 自动化验证：`~VREC:TOGGLE` / `~VREC:CANCEL`
- 采音路径：`MIC -> ES8311 ADC -> I2S -> ESP32-S3`

## 音频契约

- `16kHz` / `mono` / `16-bit` / `PCM little-endian`
- 基础帧：`20ms` = `320` 样本 = `640 bytes`

## 会话协议

`session_start` -> 连续 `audio_data` -> `session_stop`（或 `session_cancel`）

- `audio_data` 代表连续音频流中的一个顺序包（`packet_sequence`）
- 不再是旧的 "chunk + fragment" 模型
- wire layout 保留旧字段名兼容，但语义已改：`chunk_index` = `packet_sequence`，`fragment_index` 固定 0，`fragment_count` 固定 1

## 主机端重组

- 以 `session_id` 为主键，按 `packet_sequence` 顺序重组
- 缺失包补静音，优先产出完整时长 `wav`
- 统计字段：`expected_packet_count` / `received_packet_count` / `missing_packet_count` / `received_pcm_bytes` / `duration_seconds`
- 关键串口观测字段：`serial_stream_start_count` / `serial_stream_audio_count` / `serial_stream_stop_count` / `serial_upload_begin_count` / `serial_upload_end_count` / `serial_upload_skipped_count`

### 回归三档结果

- `pass`：wav 生成、时长正常、丢包 <= 2%、音频分析通过（`best_corr >= 0.20`、`peak >= 500`、`active_frame_count >= 5`）
- `warning`：wav 生成、丢包 > 2% 且 <= 12%
- `fail`：无有效音频 / 无 wav / 时长异常 / 丢包 > 12% / 分析失败

## 主机侧订阅顺序（不要改）

1. `GattSession.MaintainConnection`
2. 服务发现走 `CACHED` 优先
3. 按 `UUID` 过滤
4. **先写 `CCCD notify`**
5. **成功后再挂 `ValueChanged`**
6. `OSError(22)` 走 warm-up / rebuild fallback

不要改回"先挂 ValueChanged 再写 CCCD"，会落入 `OSError(22)`。

## 设备端竞态兼容（不要删）

Windows 自动恢复订阅时 `BLE_GAP_EVENT_SUBSCRIBE` 可能早于 `BLE_GAP_EVENT_CONNECT`。设备端暂存提前到来的订阅状态，connect 后恢复。删掉会导致主机侧 `OSError(22)` / `status=1`。

## 关键约束

- 不要把 `audio_data` 重新解释回 "chunk + fragment"
- 不要把"零缺 chunk"重新引回自动验收主口径
- 不要改主机侧订阅顺序
- 不要删设备端 "subscribe 早于 connect" 兼容
- 统一 `UTF-8`
- 同一时间只能一条自动回归独占一个 `COM` 口

## 已知限制

- 还不是最终常驻 Windows 语音输入 app
- `WinRT OSError(22)` 可恢复但不保证不出现
- 复位后自动重连比"已连接直接抓音"更脆
- 音频声学验证受环境音量影响，传输层验证更可靠
- `R1-R5` 是 bring-up / integration 回归，不是产品级长期稳定性证明

## 验证方式

```powershell
# 直接抓一段 BLE 录音
python .\tools\capture_audio_ble_wav.py --port COM3 --capture-seconds 5

# KEY1 物理按键模式
python .\tools\capture_audio_ble_wav.py --port COM3 --capture-seconds 5 --trigger-mode physical-key --no-reset-before-capture

# KEY1 物理按键模式 + 明确超时预算
python .\tools\capture_audio_ble_wav.py --port COM3 --capture-seconds 10 --trigger-mode physical-key --no-reset-before-capture --timeout-seconds 120

# KEY1 物理按键模式验收
python .\tools\verify_audio_capture_session_end_to_end.py --port COM3 --capture-seconds 10 --artifacts-dir .\tests\artifacts\audio --trigger-mode physical-key --no-reset-before-capture --timeout-seconds 120

# 标准端到端回归 R1
python .\tools\verify_audio_ble_upload_end_to_end.py --port COM3 --capture-seconds 5 --no-reset-before-capture

# 多轮 / 长录音 / 恢复回归
python .\tools\verify_audio_ble_upload_multi_round.py --port COM3 --capture-seconds 10
python .\tools\verify_audio_ble_upload_long_session.py --port COM3 --capture-seconds 60
python .\tools\verify_audio_ble_upload_recovery.py --port COM3 --capture-seconds 10
python .\tools\verify_audio_ble_upload_reconnect.py --port COM3 --capture-seconds 10
```

### 当前 physical-key 成功基线

- 最近一次 `KEY1 -> KEY1` 实测已通过：
  - `received_packet_count=1524`
  - `expected_packet_count=1524`
  - `missing_packet_count=0`
  - `received_pcm_bytes=650240`
  - `duration_seconds=20.320`
  - `serial_stream_audio_count=29`
  - `serial_stream_stop_count=1`
  - `serial_upload_begin_count=1`
  - `serial_upload_end_count=1`
  - `serial_upload_skipped_count=0`
- 对应产物：
  - `tests/capture_ble_latest_16k_mono.wav`
- 主机侧首轮 `notify` 建链等待已收紧：
  - 首轮 `CCCD` 超时预算从 `8s` 收紧到 `4s`
  - 明显异常时更快进入 fallback / retry

### 当前回归结论更新

- `physical-key` 单轮链路当前可用，且最近一次实测为零丢包
- `R2` 多轮回归在当前版本仍未完全收敛：
  - 第 1 轮可通过
  - 第 2 轮出现高丢包（最近一次实测 `missing_packet_count=175 / expected_packet_count=338`，`packet_loss_ratio=0.5178`）
  - 第 3 轮曾出现串口状态异常：`ClearCommError failed (PermissionError(13, ..., 22))`
- `R4 / R5` 恢复与重连场景在最近一次实测中未通过：
  - 当前直接卡在 `COM3` 重新打开失败：`FileNotFoundError(2, '系统找不到指定的文件。')`

当前解释：

- 传输主干（单轮采音、BLE 上行、主机重组、wav 落盘）已经可用
- 当前更大的脆点不再是单轮吞吐参数，而是：
  - 多轮场景下的主机侧 `notify` 建链与状态清理
  - 恢复 / 重连场景下的串口与设备重新枚举稳定性

## 产物

- `tests/capture_ble_latest_16k_mono.wav`
- `tests/capture_ble_latest.log`
- `tests/artifacts/audio_regression_sources/`

## 关键代码位置

- [ports/esp32/audio_capture/audio_capture_esp32.c](../../ports/esp32/audio_capture/audio_capture_esp32.c) — 采样与录音
- [components/voice_recording_control/voice_recording_control.c](../../components/voice_recording_control/voice_recording_control.c) — 录音控制状态机
- [ports/esp32/voice_key_input/voice_key_input_esp32.c](../../ports/esp32/voice_key_input/voice_key_input_esp32.c) — 语音键输入
- [ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c](../../ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c) — BLE 音频上行
- [protocols/listener_proto/include/listener_audio_proto.h](../../protocols/listener_proto/include/listener_audio_proto.h) — 协议头
- [tools/capture_audio_ble_wav.py](../../tools/capture_audio_ble_wav.py) — 主机端抓音与重组
- [tools/ble_audio_regression_common.py](../../tools/ble_audio_regression_common.py) — 回归公共逻辑
- [tools/verify_audio_ble_upload_end_to_end.py](../../tools/verify_audio_ble_upload_end_to_end.py)
- [tools/verify_audio_ble_upload_multi_round.py](../../tools/verify_audio_ble_upload_multi_round.py)
- [tools/verify_audio_ble_upload_long_session.py](../../tools/verify_audio_ble_upload_long_session.py)
- [tools/verify_audio_ble_upload_recovery.py](../../tools/verify_audio_ble_upload_recovery.py)
- [tools/verify_audio_ble_upload_reconnect.py](../../tools/verify_audio_ble_upload_reconnect.py)
