# 设备端音频采集、录音控制与 BLE 上传链路

## 使用状态

- 当前状态：`可用`
- 当前定位：`设备端语音输入主链路的前置验证能力`
- 当前是否主线：`是`
- 当前是否默认启用：`否`
- 当前阶段结论：`本阶段目标已完成`

说明：

- 这条链路已经打通了从板载麦克风收音，到设备端录音 session，再到 `Windows` 主机端重组 `wav` 的完整路径
- 当前形态仍然是 bring-up / integration 能力，不是最终用户直接面对的常驻语音输入 app
- 当前用户交互已收敛为：
  - `KEY1` 按下一次开始录音
  - 再按下一次结束录音
  - 也可通过串口命令 `~VREC:TOGGLE` / `~VREC:CANCEL` 做无实体按键验证
- 当前产品式默认入口先收敛为：
  - 一次只录一段
  - 这一段上传并等待后端处理完成后，再开始下一段

## 当前完成判断

当前这条能力已经满足本阶段“可归档到 feature、退出 active plan”的标准，因为以下几项都已通过：

- 设备端板载麦克风采音通过
- 设备端 `KEY1` 录音状态机通过
- 自定义 `BLE GATT notify` 分片上传通过
- `Windows` 主机端 session 重组 `wav` 通过
- 自动 `12s` 串口 toggle 自检通过
- 实体 `KEY1` 开始 / 结束人工验收通过
- 人工听检结果可接受

因此当前可以把这条内容视为：

- 已完成的稳定子系统总结
- 后续 `Windows` 常驻 app / 后端识别接入前的设备侧基线

## 当前已打通的能力

### 1. 设备端有线采音导出

当前链路为：

`MIC -> ES8311 ADC -> I2S -> ESP32-S3 -> USB Serial/JTAG -> Windows 主机脚本 -> wav`

当前已经确认：

- 板级音频输入链路可正常采样
- 设备端可导出 `16kHz mono PCM16-LE`
- 主机侧可保存成 `wav`
- 经人工听检，已能听到可辨认的测试音乐片段

### 2. 语音键录音状态机

当前状态机为：

- `IDLE`
- `RECORDING`

当前事件为：

- `toggle`
- `cancel`

当前交互语义为：

- `IDLE + toggle -> RECORDING`
- `RECORDING + toggle -> IDLE`
- `RECORDING + cancel -> IDLE`

当前板级输入来源为：

- `KEY1 = XL9555.IO0_4`
- `IIC_INT -> GPIO46`

### 3. 自定义 BLE 音频上传

当前链路为：

`KEY1 / 串口 toggle -> 设备端录音 session -> 自定义 BLE GATT notify 分片上行 -> Windows 主机侧重组 -> 保存 wav`

当前已经确认：

- 主机端可订阅自定义 `BLE GATT` 音频 service
- 设备录音结束后可把整段 session 分片发给主机
- 主机可按 `session_id + chunk_index + fragment_index` 完成重组
- 主机重组后的 `wav` 与自动播放测试音频有明显相关性，不是静音

## 固定音频契约

当前设备端和主机端之间已经冻结下来的契约是：

- `16kHz`
- `mono`
- `16-bit`
- `PCM little-endian`

对应内部粒度为：

- 内部基础帧：`20ms`
- 每帧：`320` 样本
- 每帧：`640 bytes`

对应上传逻辑 chunk 为：

- `200ms`
- `3200` 样本
- `6400 bytes`

当前消息语义为：

- `session_start`
- `audio_chunk`
- `session_stop`
- `session_cancel`
- `session_error`

## 关键代码位置

- 设备端采样与导出：
  [ports/esp32/audio_capture/audio_capture_esp32.c](../../ports/esp32/audio_capture/audio_capture_esp32.c)
- 设备端采样接口：
  [ports/esp32/audio_capture/include/audio_capture.h](../../ports/esp32/audio_capture/include/audio_capture.h)
- 录音控制状态机：
  [components/voice_recording_control/voice_recording_control.c](../../components/voice_recording_control/voice_recording_control.c)
- 板级语音键输入：
  [ports/esp32/voice_key_input/voice_key_input_esp32.c](../../ports/esp32/voice_key_input/voice_key_input_esp32.c)
- 自定义 BLE 音频上行：
  [ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c](../../ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c)
- 协议头与分片字段：
  [protocols/listener_proto/include/listener_audio_proto.h](../../protocols/listener_proto/include/listener_audio_proto.h)
- 主机侧有线导出脚本：
  [tools/capture_audio_wav.py](../../tools/capture_audio_wav.py)
- 主机侧录音 session 导出：
  [tools/capture_audio_session_wav.ps1](../../tools/capture_audio_session_wav.ps1)
- 主机侧 BLE 上传接收与重组：
  [tools/capture_audio_ble_wav.py](../../tools/capture_audio_ble_wav.py)
- 直接等待实体 `KEY1` 录音的主机侧入口：
  [tools/start_ble_audio_capture.ps1](../../tools/start_ble_audio_capture.ps1)
- 主机侧 BLE 端到端验证：
  [tools/verify_audio_ble_upload_end_to_end.py](../../tools/verify_audio_ble_upload_end_to_end.py)

## 推荐验证方式

### 1. 有线 WAV 导出

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\capture_audio_wav.ps1 -Port COM3 -DurationSeconds 5
```

预期结果：

- 主机侧生成 `16kHz mono 16-bit PCM` 的 `wav`
- 文件可正常播放
- 能听到明显人声或测试声，不是静音

### 2. 录音状态机 session 导出

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\capture_audio_session_wav.ps1 -Port COM3 -CaptureSeconds 4
```

预期结果：

- `KEY1` 或串口 toggle 可以开启一段 session
- 再次 toggle 后主机侧生成 session `wav`

`2026-05-13` 实测结果：

- 生成：
  - `capture_session_20260513_231654_16k_mono.wav`
- `pcm_bytes=128640`
- 相比 `4s` 理论值多 `1` 个 `20ms` 帧，符合当前按帧边界收敛的预期

### 3. BLE 上传端到端验证

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify_audio_ble_upload_end_to_end.ps1 -Port COM3
```

预期结果：

- 主机成功收到一段完整录音 session
- 主机成功重组出 `wav`
- 自动比对结果显示有明显相关性，且不是静音

当前默认流程：

- 每次固件改动并重新 `flash` 后，优先先跑这一轮自动自检
- 这轮自检默认使用：
  - `12s` 串口 toggle 录音
- 自检通过后，再进入人工实体 `KEY1` 开始/结束验收

`2026-05-14` 实测结果：

- 主机端成功接收到 `session_id=1`
- 成功重组 `21` 个 chunk
- 成功保存：
  - `tests/capture_ble_latest_16k_mono.wav`
- 自动比对结果为：
  - `best_corr=0.6097`
  - `recorded_peak=594`
  - `active_frame_count=194`

`2026-05-14` 最新自动自检结果：

- 按新的默认流程，先执行：
  - `powershell -ExecutionPolicy Bypass -File .\tools\verify_audio_ble_upload_end_to_end.ps1 -Port COM3`
- 当前默认自检时长为：
  - `12s`
- 当前自动自检结果为：
  - `session_id=1`
  - `chunk_count=61`
  - `pcm_bytes=385280`
  - `best_corr=0.7465`
  - `recorded_peak=5859`
  - `active_frame_count=602`

说明：

- 这证明“设备采音 -> BLE 分片上传 -> Windows 重组”主链路已可稳定自动验收
- 后续固件变更后，默认先跑这一轮自检，再做实体键人工验收

### 3.1 实体 `KEY1` 最新人工验收

`2026-05-14` 实体 `KEY1` 验收结果：

- 主机侧等待实体键入口：
  - `tools/start_ble_audio_capture.ps1`
- 人按一次 `KEY1` 开始
- 人再按一次 `KEY1` 结束
- 设备日志确认：
  - `recording stop source=key1`
  - `stream session stop queued: session_id=1 chunk_count=60 duration_s=11`
  - `ble_audio_stream: audio session stop: session=1 chunk_count=60`
- 主机侧结果：
  - `chunk_count=60`
  - `pcm_bytes=382080`
  - `DurationSeconds=11.94`
- 人工听检反馈：
  - `挺不错`

说明：

- 这次结果不是串口 toggle，而是实体 `KEY1` 实际完成了开始 / 结束
- 这段音频同样是经 `BLE` 分片上传后由 `Windows` 主机端重组得到

### 4. 主机侧默认产物规则

当前默认行为为：

- 录音 `wav` 默认保存到：
  - `tests/capture_ble_latest_16k_mono.wav`
- 串口 / capture log 默认保存到：
  - `tests/capture_ble_latest.log`
- 产品式拉起入口默认只接收一段 session：
  - `tools/start_ble_audio_capture.ps1`

说明：

- 当前只保留“最新一份”默认产物
- 新一轮录音会直接覆盖上一轮默认文件
- 这样更接近真实产品验收，不会在目录里堆很多时间戳文件
- 当前不会默认在一次拉起里连续接收多段录音；更符合“录一段 -> 后端处理 -> 再下一段”的产品节奏

## 当前时长语义说明

- 当前 `5s` 固定时长只属于：
  - 有线 bring-up 导出命令 `~ACAP:n`
- 当前 `BLE` 录音 session 不是按 `5s` 自动切段
- 当前 `BLE` 录音 session 的结束条件是：
  - 用户再次按键停止
  - 或极端情况下触发内部安全上限
- 因此如果某次 `wav` 只有约 `4.6s`，表示这次 session 实际就在该时刻结束了，不是主机侧把长录音切成了很多 `5s` 文件

## 当前关键约束

- 当前 `KEY1` 按键触发检测的是按下边沿，而且是 `active-low` 的下降沿
- 当前 `IO43` 不作为录音指示灯使用，因为它与控制台串口脚冲突
- 当前 `Windows` 侧在某些异常态下仍依赖主机恢复脚本先拉起 BLE 会话
- 当前相关性阈值是按“外放扬声器 -> 板载麦克风”真实声学路径放宽后的结果，不能和纯数字直通结果混为一谈
- 当前实现仍主要面向 `ESP32-S3`，`ESP-IDF / ES8311 / I2S / NimBLE` 细节继续留在 `ports/esp32/`

## 已知限制

- 当前还不是最终常驻 `Windows` 语音输入 app
- 当前没有把识别模型、文本注入和 agent 交互真正接起来
- 当前录音状态下还没有正式的板载灯光指示逻辑
- 当前长时稳定性回归和更强的断链恢复脚本，后续仍要继续补到蓝牙产品化方案里

## 下一步建议

- 下一阶段应进入 `Windows` 主机端常驻语音输入 / agent app 的实现，而不是继续扩展单次脚本验证
- 下一步最合适的是冻结“主机端 session -> 后端流式识别”的接口，而不是继续扩大设备侧录音验证脚本
- 这条能力后续应继续作为设备侧主链路保留
- `BLE HID` 键盘输入链路继续作为：
  - fallback 输入方式
  - 中文输入实验能力
  - 设备联调验证链路
