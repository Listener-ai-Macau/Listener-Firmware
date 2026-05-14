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

## 当前端到端链路现状

当前已经打通的真实链路可以拆成 `5` 层来看：

### 1. 板级采音层

当前板上真实采音路径为：

`MIC -> ES8311 ADC -> I2S -> ESP32-S3`

当前职责是：

- `ES8311` 负责把模拟麦克风信号转成数字音频
- `I2S` 负责把数字音频稳定送进 `ESP32-S3`
- `ports/esp32/audio_capture/` 负责把 `ESP-IDF + ES8311 + I2S` 这些板级细节收口在平台层

### 2. 设备端录音 session 层

当前设备端不是“永远一直录音”，而是显式 session 模式：

- `KEY1` 按一次开始
- 再按一次结束
- 或用串口 `~VREC:TOGGLE` 做自动化自检

当前设备端内部已经统一为：

- 采样格式：`16kHz mono PCM16-LE`
- 内部基础帧：`20ms / 640 bytes`
- 上行 chunk：`200ms / 6400 bytes`

也就是说，设备端是一边录音，一边把 `20ms` 小帧累积成 `200ms` chunk，然后把这些 chunk 交给 `BLE` 上传层。

### 3. 设备端 BLE 上传层

当前 `BLE` 层不是走标准蓝牙麦克风 profile，而是走自定义 `GATT notify` 音频协议：

- `session_start`
- `audio_chunk`
- `session_stop`
- `session_cancel`
- `session_error`

当前每个 `200ms` 音频 chunk 会继续按协商后的 `MTU` 再切成多个 `notify` 分片，分片头里带：

- `session_id`
- `chunk_index`
- `fragment_index`
- `fragment_count`
- `chunk_pcm_bytes`

所以当前真正的上行形态是：

`音频 PCM -> 200ms chunk -> 按 MTU 再分片 -> 多个 BLE notify`

### 4. Windows 主机接收层

当前 `Windows` 侧由
[tools/capture_audio_ble_wav.py](../../tools/capture_audio_ble_wav.py)
通过 `WinRT GATT` 订阅自定义音频特征值。

当前主机侧职责是：

- 订阅音频 notify
- 按 `session_id`
- 按 `chunk_index`
- 按 `fragment_index`
  重组完整音频块
- 按 `session_stop.chunk_count`
  严格校验 `0..chunk_count-1` 是否完整
- 收到 `session_stop` 后，把本次 session 拼成一个 `wav`

当前默认产物是：

- `tests/capture_ble_latest_16k_mono.wav`
- `tests/capture_ble_latest.log`

### 5. 后端接入预留层

当前阶段还没有真正接上识别模型，但接口方向已经比较清楚了：

- 设备端负责稳定采音和稳定上行
- `Windows` 端负责把同一个 session 拼回连续 PCM
- 后端后续可以直接消费这段 `16kHz mono PCM16-LE`
- 后端既可以“整段结束后再识别”，也可以“边收边做流式识别”

换句话说，当前这条链路已经满足：

`设备采音 -> 电脑收音频 -> 以后再接 ASR / Agent`

而不是还停留在“只能导出一段测试 wav”的阶段。

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

## 对照官方示例后的结论

这轮对照的主要官方先例是本机 `ESP-IDF` 自带的：

- `C:\Users\Billy\esp\esp-idf\examples\bluetooth\nimble\throughput_app`

这个官方示例不是“音频上传 demo”，但它正好是 `NimBLE GATT notify` 高吞吐的标准参考，所以对我们现在这条自定义音频上行链路很有价值。

当前已经借鉴并落地的做法有：

- 采用较大的首选 `ATT MTU`
  - 当前仓库已配置：
    - `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=517`
- 连接建立后主动请求更适合音频上传的连接参数
  - 当前已请求：
    - `itvl_min=6`
    - `itvl_max=12`
    - `latency=0`
    - `supervision_timeout=400`
- 连接建立后主动请求 `2M PHY`
- 发送包大小按协商后的 `MTU` 动态计算，而不是死写固定值
- 对 `BLE_HS_ENOMEM` 做短延迟重试，而不是一失败就直接放弃

当前还没有完全照搬、但官方示例里值得继续借的做法有：

- 更强的 notification pipeline
  - 官方吞吐例子会维持多条 in-flight notify
  - 我们当前还是“队列 + 固定间隔”式发送，稳，但还不是最满吞吐
- 更大的 `MSYS` buffer 预算
  - 当前本仓库 `CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT=12`
  - 官方吞吐例子会配到更高，给高吞吐 notify 更充裕的 mbuf 空间
- 显式的数据长度扩展设置
  - 官方吞吐例子会显式调用 `ble_hs_hci_util_set_data_len(...)`
  - 我们当前主要依赖已有链路参数和控制器默认协商

这说明当前方案不是“自己凭空编的”，而是：

- 协议层是我们为音频 session 自定义的
- 但底层吞吐优化方向，已经尽量向官方 `NimBLE throughput` 示例靠拢

## 当前链路的总体评价

如果按“现在能不能作为语音输入主链路基线”来评估，当前结论是：

- 设备端采音链路已经成立
- `BLE` 上行链路已经成立
- `Windows` 端重组链路已经成立
- 实体 `KEY1` 交互已经成立
- 当前已经足够进入下一阶段：
  - `Windows` 常驻接收 app
  - 后端流式识别
  - 识别后文本输出

如果按“是不是已经达到最终产品形态”来评估，当前答案还是：

- 还没有

因为当前更像：

- 一个已经稳定的设备侧基线
- 一个可自动验收的主机侧 bring-up 工具

而不是：

- 面向最终用户的一体化语音输入产品

## 当前最值得做的优化

### 1. 主机侧已经补上 chunk 完整性校验

当前 `Windows` 侧已经不再静默接受“收到一部分 chunk 就生成 wav”的 session。

当前行为已经收紧为：

- 收到 `session_stop`
- 解析出设备端声明的 `expected_chunk_count`
- 严格检查 `0..chunk_count-1` 是否完整
- 如果有缺块，直接报错，并输出缺失 `chunk_index` 摘要

这意味着后续接流式后端时，主机侧不会再把一段隐性缺块音频伪装成“正常完成的 session”。

### 2. 把主机侧从脚本推进成常驻 app

当前 `capture_audio_ble_wav.py` 已经足够做 bring-up 和联调，但它还不是最终产品形态。

后续真正要做语音输入时，更合理的是主机侧有一个常驻 app，负责：

- 设备发现和连接恢复
- 音频 session 生命周期
- chunk 重组
- 后端流式识别接入
- 文本输出
- 日志与错误提示

这个优化的价值不在于“多高级”，而在于把现在的测试链路变成真正产品主链路。

### 3. 设备端发送侧可以继续向官方吞吐例子靠拢

如果后面发现更长录音、更复杂环境下偶尔还会丢块，设备端优先可以从下面几项继续收紧：

- 评估是否引入更明确的 notify pipeline
- 评估是否提高 `MSYS` buffer 数量
- 评估是否显式设置 `data length extension`
- 把现在的固定 `2ms / 5ms` 发送间隔，逐步演进成更有依据的流控策略

这些优化主要解决的是：

- 吞吐余量
- 长时稳定性
- 高负载场景下的掉包概率

### 4. 音频清洗先放主机侧或后端，不急着先压到嵌入式端

从当前试听结果看，链路已经“能用”，但还有环境噪声空间。

对当前项目阶段来说，更合理的顺序通常是：

- 先保证原始音频稳定拿到
- 再在 `Windows` 端或后端加：
  - `VAD`
  - 降噪
  - `AGC`
  - 流式分段

而不是一开始就在 `ESP32-S3` 上堆很多前处理。

这样做的好处是：

- 调参更快
- 模型侧收益更直接
- 不会过早把平台耦合压到固件里

### 5. 蓝牙稳定性回归要继续保留自动化

虽然这条链路现在已经通过验收，但后面只要继续改：

- `NimBLE`
- `GATT`
- 主机侧订阅逻辑
- 录音状态机

就仍然要持续保留：

- 自动 `12s` 串口 toggle 自检
- 实体 `KEY1` 人工验收
- 断链 / 重连 / 重订阅类回归

因为语音产品真正难的地方，通常不是“第一次能录出来”，而是“反复用仍然稳定”。

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
  - `expected_chunk_count=61`
  - `missing_chunk_count=0`
  - `pcm_bytes=385280`
  - `best_corr=0.7465`
  - `recorded_peak=5859`
  - `active_frame_count=602`

说明：

- 这证明“设备采音 -> BLE 分片上传 -> Windows 重组”主链路已可稳定自动验收
- 后续固件变更后，默认先跑这一轮自检，再做实体键人工验收

`2026-05-14` chunk 完整性校验补齐后的最新自动自检结果：

- 主机侧结果：
  - `chunk_count=61`
  - `expected_chunk_count=61`
  - `missing_chunk_count=0`
- 自动比对结果：
  - `best_corr=0.3467`
  - `recorded_peak=28661`
  - `active_frame_count=152`

说明：

- 这证明主机侧新的 chunk 完整性校验没有误伤当前稳定链路
- 当前 `Windows` 接收侧已经可以把“完整 session”与“缺块 session”明确区分开

`2026-05-14` 连续多轮短录音 `R2` 回归结果：

- 执行入口：
  - `powershell -ExecutionPolicy Bypass -File .\tools\verify_audio_ble_upload_multi_round.ps1 -Port COM3`
- 当前回归规模：
  - `10` 轮
  - 每轮 `10s`
- 当前结果：
  - `scenario=R2`
  - `result=pass`
  - `round_count=10`
  - `pass_count=10`
  - `fail_count=0`
  - `failed_rounds=<none>`

说明：

- 这证明当前链路不只是“单轮 10s 能过”，而是已经通过了 `10` 轮连续短录音的主机侧回归
- 当前 `missing_chunk_count` 在这轮 `R2` 回归中保持为 `0`
- 后续如果再改：
  - `BLE` 上传
  - 主机侧接收
  - 录音状态机
  默认应把这条 `R2` 回归继续保留

`2026-05-14` 长录音单 session `R3` 回归结果：

- 执行入口：
  - `powershell -ExecutionPolicy Bypass -File .\tools\verify_audio_ble_upload_long_session.ps1 -Port COM3`
- 当前回归规模：
  - 单次 `60s`
- 当前结果：
  - `scenario=R3`
  - `result=pass`
  - `chunk_count=301`
  - `expected_chunk_count=301`
  - `missing_chunk_count=0`
  - `duration_seconds=60.060`

说明：

- 这证明当前链路已经不只是短录音可用，单次 `60s` 长录音 session 也能稳定完整上传
- 当前长录音回归中，主机侧 chunk 完整性校验同样保持为 `0` 缺块

`2026-05-14` 主机侧订阅恢复 `R4` 回归结果：

- 执行入口：
  - `powershell -ExecutionPolicy Bypass -File .\tools\verify_audio_ble_upload_recovery.ps1 -Port COM3`
- 当前故障注入口径：
  - 先重启 `Windows` 蓝牙服务
  - 再执行 `recover_ble_hid_host.ps1`
  - 然后验证下一轮 `10s` 自动录音
- 当前结果：
  - `scenario=R4`
  - `result=pass`
  - `recover_attempts=2`
  - `missing_chunk_count=0`

说明：

- 这证明当前主机侧 notify 订阅恢复路径已经有了可复测脚本
- 当前恢复场景下，恢复后下一轮录音仍能拿到完整 session，而不是只恢复到“Windows 看起来连着”

`2026-05-14` 断链 / 重连恢复 `R5` 回归结果：

- 执行入口：
  - `powershell -ExecutionPolicy Bypass -File .\tools\verify_audio_ble_upload_reconnect.ps1 -Port COM3`
- 当前回归流程：
  - 先跑一轮基线 `10s` 录音
  - 再重启 `Windows` 蓝牙服务并执行主机恢复脚本
  - 最后验证恢复后的下一轮 `10s` 录音
- 当前结果：
  - `scenario=R5`
  - `result=pass`
  - `recover_attempts=2`
  - `baseline_missing_chunk_count=0`
  - `missing_chunk_count=0`

说明：

- 这证明当前链路已经具备“发生一次主机侧断链 / 重连后，下一轮 session 仍可继续完整录音”的自动回归能力
- 当前 `R5` 并不等于产品已经彻底没有蓝牙风险，但至少已经从人工经验推进成脚本可复测基线

`2026-05-14` 自动回归矩阵收口结论：

- 当前已具备：
  - `R1` 标准短录音自动自检
  - `R2` 连续 `10` 轮短录音
  - `R3` 单次 `60s` 长录音
  - `R4` 主机侧订阅恢复
  - `R5` 断链 / 重连恢复
- 当前这 `5` 类回归都已具备主机侧脚本入口
- 当前统一结果字段至少覆盖：
  - `result`
  - `scenario`
  - `expected_chunk_count`
  - `missing_chunk_count`
  - `duration_seconds`
  - `wav_path`
  - `serial_log_path`

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
- 自动回归所需的临时源音频现在统一落到：
  - `tests/artifacts/audio_regression_sources/`
  - 作为中间产物忽略提交

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
- 当前串口相关自动回归应串行执行，不能并发共用同一个 `COM` 口

## 已知限制

- 当前还不是最终常驻 `Windows` 语音输入 app
- 当前没有把识别模型、文本注入和 agent 交互真正接起来
- 当前录音状态下还没有正式的板载灯光指示逻辑
- 当前 `R4/R5` 主要验证的是主机侧恢复路径，后续如果要做更强产品化，还要继续增加更长期、更多轮次的稳定性回归

## 下一步建议

- 下一阶段应进入 `Windows` 主机端常驻语音输入 / agent app 的实现，而不是继续扩展单次脚本验证
- 下一步最合适的是冻结“主机端 session -> 后端流式识别”的接口，而不是继续扩大设备侧录音验证脚本
- 这条能力后续应继续作为设备侧主链路保留
- `BLE HID` 键盘输入链路继续作为：
  - fallback 输入方式
  - 中文输入实验能力
  - 设备联调验证链路
