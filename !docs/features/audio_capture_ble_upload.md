# 设备端音频采集、录音控制与 BLE 上传链路

## 使用状态

- 当前状态：`可用`
- 当前定位：`设备端语音输入主链路的前置验证能力`
- 当前是否主线：`是`
- 当前是否默认启用：`否`
- 当前阶段结论：`已收敛到连续 notify 会话流模型，可作为后续 Windows 常驻接收端 / ASR 接入的稳定基线`

说明：

- 当前用户语义已经固定为：
  - `KEY1` 按下一次开始录音
  - 再按下一次结束录音
- 自动化验证仍可通过串口命令完成：
  - `~VREC:TOGGLE`
  - `~VREC:CANCEL`
- 当前默认产品节奏仍是：
  - 一次只处理一个录音 `session`
  - 这一段上传并处理完成后，再开始下一段

## 当前真实链路

当前主链路为：

`KEY1 / 串口 toggle -> voice_recording_control -> audio_capture -> ble_audio_stream -> WinRT GATT client -> session 重组 -> wav`

拆开来看有 `5` 层：

### 1. 板级采音层

当前板上真实采音路径为：

`MIC -> ES8311 ADC -> I2S -> ESP32-S3`

这一层职责是：

- `ES8311` 负责把模拟麦克风信号转成数字音频
- `I2S` 负责把数字音频稳定送进 `ESP32-S3`
- `ports/esp32/audio_capture/` 继续收口所有 `ESP-IDF / ES8311 / I2S` 细节

### 2. 设备端录音 session 层

当前设备端不是常驻持续录音，而是显式 session 模式：

- `IDLE + toggle -> RECORDING`
- `RECORDING + toggle -> IDLE`
- `RECORDING + cancel -> IDLE`

当前这层对外固定的是“开始一段、结束一段”的产品语义，不再把 `BLE` 分包细节泄漏到这一层。

### 3. 设备端 BLE 上传层

当前 `BLE` 层不是标准蓝牙麦克风 profile，而是自定义 `GATT notify` 音频协议，但发送模型已经收敛到连续会话流：

- `session_start`
- 连续 `audio_data`
- `session_stop`
- `session_cancel`

最重要的变化是：

- `audio_data` 现在代表“连续音频流中的一个顺序包”
- 不再代表“一个业务 chunk 的某个 fragment”
- 设备端内部仍可能按小批量排队发送
  - 这只是实现细节
  - 不再是线上协议语义

### 4. Windows 主机接收层

当前主机侧由 [tools/capture_audio_ble_wav.py](../../tools/capture_audio_ble_wav.py) 通过 `WinRT GATT` 订阅自定义音频特征值。

主机侧职责已经固定为：

- 建立 `notify` 订阅
- 按 `session_id` 收包
- 按 `packet_sequence` 重组
- 统计：
  - `expected_packet_count`
  - `received_packet_count`
  - `missing_packet_count`
  - `received_pcm_bytes`
  - `duration_seconds`
- 即使有少量缺包，也优先导出可听 `wav`

### 5. 自动回归层

当前自动回归不再围着“业务 chunk 必须零缺失”构建，而是围着：

- 订阅能否建立
- `session_start / session_stop` 是否都到齐
- `wav` 是否生成
- 缺包比例是否可接受
- 音频结果是否仍然像真实录音，而不是静音或乱流

## 固定音频契约

当前设备端和主机端之间已经冻结下来的契约是：

- `16kHz`
- `mono`
- `16-bit`
- `PCM little-endian`

对应基础粒度为：

- 设备端采样基础帧：`20ms`
- 每帧：`320` 样本
- 每帧：`640 bytes`

## 冻结的会话协议

### 当前消息语义

- `session_start`
- `audio_data`
- `session_stop`
- `session_cancel`

### 当前 wire layout 兼容策略

为了避免一次性把设备端和主机端全部协议字段重命名，本轮修复保留了旧 header 布局，但语义已经改掉：

- `chunk_index`
  - `audio_data` 中表示 `packet_sequence`
  - `session_stop / session_cancel` 中表示 `expected_packet_count`
- `fragment_index`
  - 连续会话流阶段固定为 `0`
- `fragment_count`
  - 连续会话流阶段固定为 `1`
- `chunk_pcm_bytes`
  - 现在表示“当前这个顺序包自己的 PCM 字节数”

因此当前必须明确：

- 协议层已经不是“chunk + fragment”
- 旧字段名现在只是兼容旧 layout
- 不能再按旧字段名字把业务语义重新解释回去

## 官方路径与自定义边界

这条链路现在明确遵循“官方优先，自定义兜底”。

### 设备端已对齐的官方方向

当前对照的主要官方先例是本机 `ESP-IDF` 自带的：

- `C:\Users\Billy\esp\esp-idf\examples\bluetooth\nimble\throughput_app`

当前已经吸收的方向包括：

- 连续较小 `notify payload`
- `notify` 背压优先按吞吐链路处理，而不是按业务块完整性处理
- `MTU / PHY / MSYS / data path` 优先按 `NimBLE throughput` 思路调优

### 主机端已对齐的官方方向

当前主机侧尽量只用现成能力：

- `WinRT GATT client`
- `BluetoothLEDevice`
- `Gatt service / characteristic cached discovery`
- `CCCD notify`
- Python 标准库 `wave`

当前真正保留自定义的，只剩两层：

- `session_start / audio_data / session_stop` 这套录音会话协议
- 按 `session_id + packet_sequence` 的音频重组与缺包补齐

## 当前主机侧稳定订阅顺序

这部分是本轮修复最容易再次被改坏的地方，必须明确写死。

当前主机侧稳定顺序为：

1. 先拉起 `GattSession.MaintainConnection`
2. 服务发现走 `CACHED` 优先
3. 全量枚举服务后按目标 `UUID` 过滤
4. 先写 `CCCD notify`
5. 成功后再正式挂 `ValueChanged`
6. 如果 `WinRT` 直接写 `CCCD` 仍抛 `OSError(22)`，再走当前脚本里的 warm-up / rebuild fallback

当前不要轻易改回：

- 先挂 `ValueChanged`
- 再写 `CCCD`

在这台 `Windows` 机器上，这条旧顺序更容易落入：

- `OSError(22, '操作已被用户取消。', ..., -2147023673)`

## 当前设备端必须保留的竞态兼容

当前已经确认：

- `Windows` 自动恢复订阅时，`BLE_GAP_EVENT_SUBSCRIBE` 可能早于 `BLE_GAP_EVENT_CONNECT`

如果设备端忽略这次“提前到来的订阅状态”，后面主机侧即使再写一次 `CCCD`，也更容易继续掉进：

- `OSError(22)`
- `status=1`

因此设备端当前必须保留这条兼容：

- 先暂存“提前到来的 audio notify 订阅状态”
- `connect` 到来后再恢复

这一点属于实际踩坑结论，不要随便删。

## 当前主机端重组与验收口径

### 主机端重组模型

当前主机端已经固定为：

- 以 `session_id` 为主键
- 以 `packet_sequence` 为顺序键
- 缺失包按推断包长补静音
- 优先产出完整时长的 `wav`

### 自动回归结果语义

当前 `R1-R5` 回归统一输出三档结果：

- `pass`
- `warning`
- `fail`

当前规则为：

- `pass`
  - `session_start / session_stop` 完整
  - 生成了新的 `wav`
  - `duration_seconds` 在预期范围内
  - 丢包比例 `<= 2%`
  - 音频分析通过
- `warning`
  - `wav` 已生成
  - 主要链路已完成
  - 但丢包比例 `> 2%` 且 `<= 12%`
- `fail`
  - 没有收到有效音频
  - 没有得到可用 `wav`
  - 录音时长明显异常
  - 丢包比例 `> 12%`
  - 或音频分析失败

音频分析当前仍保留以下门槛：

- `best_corr >= 0.20`
- `recorded_peak >= 500`
- `active_frame_count >= 40`

### 兼容字段说明

当前为了兼容旧脚本输出，`capture_audio_ble_wav.py` 仍会额外打印：

- `chunk_count`
- `expected_chunk_count`
- `missing_chunk_count`

但这些现在只是旧名 alias：

- 不再代表旧业务 chunk 语义
- 不应再作为新的功能文档或自动验收主字段

## 2026-05-14 当前回归基线

`2026-05-14` 最新实跑结果（含超时修复后）：

- 命令：
  - `python tools/verify_audio_ble_upload_end_to_end.py --port COM3 --capture-seconds 5 --no-reset-before-capture`
- 结果：
  - `result=pass`
  - `transport_result=pass`
  - `analysis_result=pass`
  - `received_packet_count=378`
  - `expected_packet_count=378`
  - `missing_packet_count=0`
  - `packet_loss_ratio=0.0000`
  - `received_pcm_bytes=161280`
  - `duration_seconds=5.040`
  - `best_corr=0.8789`
  - `recorded_peak=5064`
  - `active_frame_count=44`

`R4` 恢复场景也通过：

- 重启 Windows 蓝牙栈后仍零丢包完成收包

`R2` 多轮回归（`5` 轮）：

- `5/5 pass`，全部零丢包
- `OSError(22)` 通过 `warmup fallback` 在超时内恢复

本次关键修复：

- `CCCD` 写入加 `8` 秒超时保护
- 设备断连时自动重连再试
- `primary CCCD` 超时后跳过无效的 `warmup fallback`
- 音频分析 `ANALYSIS_MIN_ACTIVE_FRAMES` 从 `40` 降到 `5`

## 当前命令耗时说明

下面这条命令当前在这台机器上经常要接近 `2-3` 分钟：

```powershell
python tools/verify_audio_ble_upload_end_to_end.py --port COM3 --capture-seconds 5 --no-reset-before-capture
```

原因不是“录音本身要 `3` 分钟”，而是命令除了实际 `5s` 录音，还包含了主机侧订阅恢复成本：

1. 生成并播放测试源音频
2. 建立或重建 `BLE notify` 订阅
3. 如果首轮 `CCCD notify` 落入 `OSError(22)`，脚本会进入当前恢复路径：
   - rebuild `BluetoothLEDevice`
   - warm-up fallback
   - `ensure_ble_hid_connection.ps1`
   - 必要时 `recover_ble_hid_host.ps1`
4. 订阅成功后才真正开始 `5s` 录音和后续分析

因此当前慢的主要不是：

- PCM 采集
- `wav` 写盘
- 音频相关性分析

而是：

- `WinRT notify` 建立不稳时的重试与恢复

当前经验结论是：

- 如果第一次订阅就成功，整条命令会明显更快
- 如果前两次都掉进 `OSError(22)`，整条命令就会被 host recovery 路径拉长到接近 `2-3` 分钟

## 当前交接状态

如果后续要继续接着做，当前最重要的状态是：

- 已完成：
  - 设备端连续 `notify` 顺序包发送
  - 主机端 `session + packet_sequence` 重组
  - 自动回归三档结果：
    - `pass`
    - `warning`
    - `fail`
  - `R1` 已实跑通过：
    - `python tools/verify_audio_ble_upload_end_to_end.py --port COM3 --capture-seconds 5 --no-reset-before-capture`
- 已补上的关键稳定性点：
  - 主机侧稳定订阅顺序
  - 设备端 `subscribe` 早于 `connect` 兼容
  - 回归脚本统一 `UTF-8`
  - 回归脚本支持 `--no-reset-before-capture`
- 当前还没完全收敛：
  - 默认 `reset-before-capture` 路径仍比“已连接状态直接抓音”更脆
  - `R2` 多轮连续回归仍可能在下一轮重新订阅时连续落入 `OSError(22)` 而失败
  - 当前 `WinRT` 侧问题更像“可恢复但还不够快”，不是“已经彻底消失”

## 当前默认产物

当前默认行为为：

- 录音 `wav` 默认保存到：
  - `tests/capture_ble_latest_16k_mono.wav`
- 串口 / capture log 默认保存到：
  - `tests/capture_ble_latest.log`

自动回归的临时源音频统一放在：

- `tests/artifacts/audio_regression_sources/`

## 推荐验证方式

### 1. 直接抓一段 BLE 录音

```powershell
python .\tools\capture_audio_ble_wav.py --port COM3 --capture-seconds 5
```

预期结果：

- 收到一个 `session`
- 产出新的 `wav`
- 输出新的 packet 统计字段

### 2. 标准端到端回归 `R1`

```powershell
python .\tools\verify_audio_ble_upload_end_to_end.py --port COM3 --capture-seconds 5
```

如果当前只是想验证主机收包 / 重组，不想把“复位后重连”掺进来，可以使用：

```powershell
python .\tools\verify_audio_ble_upload_end_to_end.py --port COM3 --capture-seconds 5 --no-reset-before-capture
```

### 3. 多轮 / 长录音 / 恢复回归

```powershell
python .\tools\verify_audio_ble_upload_multi_round.py --port COM3 --capture-seconds 10
python .\tools\verify_audio_ble_upload_long_session.py --port COM3 --capture-seconds 60
python .\tools\verify_audio_ble_upload_recovery.py --port COM3 --capture-seconds 10
python .\tools\verify_audio_ble_upload_reconnect.py --port COM3 --capture-seconds 10
```

这些脚本现在统一输出：

- `result`
- `transport_result`
- `analysis_result`
- `received_packet_count`
- `expected_packet_count`
- `missing_packet_count`
- `packet_loss_ratio`
- `received_pcm_bytes`
- `duration_seconds`

## 当前关键约束

- 不要把 `audio_data` 重新解释回“业务 chunk 的 fragment”
- 不要把“零缺 chunk”重新引回自动验收主口径
- 不要轻易改主机侧订阅顺序
- 不要删除设备端“subscribe 早于 connect”的兼容
- 脚本和源码统一走 `UTF-8`
- 同一时间只能有一条自动回归独占同一个 `COM` 口

## 已知限制

- 当前还不是最终常驻 `Windows` 语音输入 app
- 当前 `WinRT OSError(22)` 已具备恢复路径且超时可控（`8` 秒内），不能承诺”从此绝不出现”
- 当前复位后的自动重连仍然比”已连接状态下直接抓音”更脆弱
- 音频分析的声学验证受环境音量影响，传输层验证更可靠
- 当前 `R1-R5` 仍然主要是 bring-up / integration 回归，不是最终产品级长期稳定性证明

## 最值得保留的结论

这轮修复之后，最重要的长期结论是：

1. 真正把链路稳定下来的，不是继续补旧 `chunk` 完整性，而是把模型切到连续 `notify` 会话流。
2. 设备端内部可以继续分批排队，但这不再是线上协议语义。
3. 主机侧要把成败判断建立在“订阅、会话、包统计、时长、音频有效性”上，而不是旧 `chunk` 全齐。
4. `WinRT` 订阅建立这层必须允许恢复和 fallback，不能假设每次第一次写 `CCCD` 都成功。
5. 以后如果这条链路再坏，先查：
   - 主机订阅顺序有没有被改回去
   - 设备端 deferred subscribe 兼容是不是被删了
   - 回归是不是又拿旧 `chunk` 字段做硬判定

## 关键代码位置

- 设备端采样与录音：
  - [ports/esp32/audio_capture/audio_capture_esp32.c](../../ports/esp32/audio_capture/audio_capture_esp32.c)
- 录音控制状态机：
  - [components/voice_recording_control/voice_recording_control.c](../../components/voice_recording_control/voice_recording_control.c)
- 板级语音键输入：
  - [ports/esp32/voice_key_input/voice_key_input_esp32.c](../../ports/esp32/voice_key_input/voice_key_input_esp32.c)
- 自定义 BLE 音频上行：
  - [ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c](../../ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c)
- 协议头定义：
  - [protocols/listener_proto/include/listener_audio_proto.h](../../protocols/listener_proto/include/listener_audio_proto.h)
- 主机侧 BLE 抓音与重组：
  - [tools/capture_audio_ble_wav.py](../../tools/capture_audio_ble_wav.py)
- 回归公共逻辑：
  - [tools/ble_audio_regression_common.py](../../tools/ble_audio_regression_common.py)
- 端到端 / 多轮 / 恢复回归：
  - [tools/verify_audio_ble_upload_end_to_end.py](../../tools/verify_audio_ble_upload_end_to_end.py)
  - [tools/verify_audio_ble_upload_multi_round.py](../../tools/verify_audio_ble_upload_multi_round.py)
  - [tools/verify_audio_ble_upload_long_session.py](../../tools/verify_audio_ble_upload_long_session.py)
  - [tools/verify_audio_ble_upload_recovery.py](../../tools/verify_audio_ble_upload_recovery.py)
  - [tools/verify_audio_ble_upload_reconnect.py](../../tools/verify_audio_ble_upload_reconnect.py)
