# 语音输入前端到后端的输入契约

## 状态

- 状态：`可联调`
- 定位：`后端接入语音输入链路时应依赖的稳定输入边界`
- 当前阶段结论：`BLE 音频会话主链路已完成 P1-P10 standard/realistic 多 seed 收敛，可作为 Listener-Type 外部音频源和 P13 软件后端闭环的稳定基线`

## 文档目的

这份文档不定义 `BLE` 底层实现细节的所有行为，而定义后端当前应该依赖的输入契约。

后端接入时，优先依赖下面这些稳定语义：

- 音频格式
- 会话起止语义
- 顺序包重组语义
- 成功 / 失败口径
- 超时与错误处理边界

不要把当前 `BLE` 的承载细节直接等同于最终后端协议本体。

## 当前链路分层

当前已跑通的链路是：

`EC11 单击 / 串口 toggle -> voice_recording_control -> audio_capture -> ble_audio_stream -> Windows 主机重组 -> wav / Listener-Type 外部音频源 / 上层 ASR 接入点`

面向后端时，建议按两层理解：

- 传输承载层：
  当前是 `BLE notify -> Windows 主机接收`
- 语音输入会话层：
  当前已经形成稳定的 `session_start -> audio_data -> session_stop/session_cancel` 语义

后端应尽量依赖第二层，而不是绑定第一层。

## 软件侧职责边界

当前嵌入式音频到软件的推荐边界是：

```text
ESP32-S3 firmware
  -> BLE GATT Notify: VKA1 session_start / audio_data / session_stop
  -> Listener-Type embedded audio source
  -> Listener-Type existing ASR / polish / insertion / history
  -> optional final text handoff to Listener-ai-agent
```

也就是说：

- `Listener-Type` 是 `raw PCM` 和听写会话的优先承接方。
- `Listener-Type` 应把 BLE 音频建模成与麦克风并列的外部音频源，复用现有 ASR、润色、插入和历史记录链路。
- `Listener-ai-agent` 当前不接收 `BLE PCM` / `WAV` 原始音频，只作为可选的最终文本或用户指令下游。
- 固件不做 ASR、云端 API 调用、Agent 调度或业务理解，只负责采集、分包、会话边界和错误上报。
- 如果未来需要 Agent handoff，应通过稳定的 app 级文本入口，不依赖私有 sidecar port。

## 当前建议冻结的输入契约

### 音频格式

- 编码：`PCM`
- 字节序：`little-endian`
- 采样率：`16 kHz`
- 声道：`mono`
- 位宽：`16-bit`

当前基础分片量级：

- `20 ms`
- `320` 样本
- `640 bytes`

对后端的实际含义是：

- 可以按 `16k mono 16-bit PCM` 直接做流式识别输入
- 不需要先等待完整 `wav` 文件生成
- 不要把当前输入当作压缩音频流

### 会话语义

当前推荐后端按下面的上层语义建模：

`session_start -> audio_data(packet_sequence...) -> session_stop`

异常结束时：

`session_start -> audio_data(packet_sequence...) -> session_cancel`

当前含义：

- `session_start`
  一次新的录音会话开始
- `audio_data`
  会话内连续音频包
- `session_stop`
  会话正常结束，并携带本次期望总包数
- `session_cancel`
  会话异常取消，也会携带本次期望总包数

### 核心字段语义

当前主机侧和设备侧已经稳定依赖这些字段语义：

- `session_id`
  区分一次完整录音会话
- `packet_sequence`
  会话内顺序包编号，从 `0` 连续递增
- `expected_packet_count`
  当前会话应有的总包数，由 `session_stop / session_cancel` 给出
- `payload_len`
  当前包有效负载长度
- `chunk_pcm_bytes`
  当前包对应的有效 `PCM` 字节数

当前 wire layout 还保留旧字段名兼容，但后端不要再按旧语义理解：

- `chunk_index`
  现在等价于 `packet_sequence`
- `fragment_index`
  当前阶段固定为 `0`
- `fragment_count`
  当前阶段固定为 `1`

也就是说，当前已经不是旧的 `chunk + fragment` 模型。

## 主机重组与后端输入口径

当前 Windows 主机重组规则是：

- 以 `session_id` 为主键聚合
- 按 `packet_sequence` 顺序重组
- 以 `expected_packet_count` 判断会话完整度
- 缺失包补静音，优先保留时间轴完整性

当前主机侧会输出这些重要统计字段：

- `received_packet_count`
- `expected_packet_count`
- `missing_packet_count`
- `missing_packet_indices`
- `received_pcm_bytes`
- `duration_seconds`

对后端更推荐的输入口径是：

- 接收一个明确开始和结束的音频会话
- 每个会话内部按顺序消费 `PCM` 数据
- 如果中间丢包，允许由前端或中转层补静音并上报缺失统计

不建议后端直接依赖：

- `BLE` 连接生命周期事件
- `WinRT` 的具体异常字符串
- 当前主机工具脚本里的历史兼容字段名

## 成功 / 失败口径

### 当前可视为成功的联调口径

单轮会话至少应满足：

- 收到 `session_start`
- 收到 `session_stop`
- 能输出完整 `wav` 或等效 `PCM`
- `duration_seconds` 与实际录音时长大体一致
- `missing_packet_count` 可统计

当前已收敛的自动矩阵口径：

- `P1-P10` 在 `standard` 口径下多随机种子通过
- `P1-P10` 在 `realistic` 口径下多随机种子通过
- 当前自动矩阵结论为 `matrix_failed=0`、`matrix_warning=0`

历史物理按键成功基线之一：

- `received_packet_count=1524`
- `expected_packet_count=1524`
- `missing_packet_count=0`
- `received_pcm_bytes=650240`
- `duration_seconds=20.320`

这说明：

- BLE 音频主链路已经可用
- 当前音频格式和会话模型已经足够支撑 Listener-Type 外部音频源联调
- 后续软件后端闭环应进入 `P13`

### 当前仍需单独验收的部分

下面这些场景不应被 P1-P10 自动矩阵冒充为已完成：

- `P11` 真实物理按键口径：已有人工听感通过记录，但仍不计入自动矩阵
- `P12` 弱环境 / 距离 / 遮挡 / 强干扰：已有当前桌面 ambient RF baseline，受控外场仍需补测
- `P13` 软件后端闭环：`Listener-Type` 外部音频源、ASR、润色、插入、历史记录、可选 Agent handoff
- 更长时间的量产级 soak

因此当前应把结论写成：

- `BLE 音频主链路 P1-P10 已收敛`
- `P12 仍需要受控弱环境补测`
- `P13 仍需要软件后端闭环验收`
- `产品级使用面测试矩阵已切到 P 系列，旧 R1-R5 或更早失败记录不能代表当前结论`

### P13 软件后端闭环口径

P13 负责回答“嵌入式音频是否真正进入软件产品链路”。建议至少拆成：

- `P13.1`：BLE host adapter smoke，KEY1 触发后软件端收到完整 session，并产出合法 `16k mono i16` PCM/WAV stats
- `P13.2`：`Listener-Type` ASR，嵌入式 PCM 进入现有 ASR provider，得到 raw transcript
- `P13.3`：`Listener-Type` 产品链路，最终文本完成润色、插入和历史记录，并记录 session stats
- `P13.4`：cancel/error/reconnect，`session_cancel`、`session_error`、BLE 断链后软件回到 Idle
- `P13.5`：可选 Agent handoff，开启后最终文本进入 `Listener-ai-agent` 当前会话
- `P13.6`：固件回归，P1-P10 realistic `--fail-on-warning` 仍通过

P13 验证音频优先使用固定人声样本或 seeded TTS 随机语料，减少人工每轮说话。随机语料必须保存 seed、case_id、ground truth、ASR raw transcript、normalized diff 和音频路径，方便失败复现和识别准确率统计。

`2026-05-18` 软件侧 baseline：`Listener-Type` 已用 Foundry Local `whisper-small` 完成 P13.2 纯软件 smoke。seeded TTS 句子“明天下午两点提醒我检查蓝牙音频丢包率。”经 `VKA1` replay 后进入 ASR，raw transcript 为“明天下午兩點提醒我檢查藍牙音頻丟包率。”。结论：ASR 链路可用，准确率统计需要在 normalization 中加入简繁归一，避免把等价字符差异误判成识别错误。

`2026-05-18` 真实 BLE acoustic smoke：PC 扬声器播放同一句 TTS，设备麦克风采集并经 BLE 回传，抓音 `407/407` 包、丢包 `0`、声学相关性 `0.943`，Foundry raw transcript 为“明天下午兩點提醒或檢查藍芽音頻中包”。结论：真实声学链路已经能让后端识别到文字；该结果只能证明 P13.2 真实链路可达，不代表识别准确率已经达 gate。

`2026-05-18` P13.3 产品链路入口初版：`Listener-Type` 已新增 `submit_embedded_audio_notifications` IPC。该入口接收完整 `VKA1` notification batch，按 `SessionCollector` 重组 PCM，然后复用现有 coordinator 听写链路完成 ASR、润色、插入和历史记录；当前支持 Foundry Local Whisper 与 Whisper 兼容 batch ASR。该入口证明“外部嵌入式音频源”可以进入产品链路，但还没有完成 native BLE scan/connect/notify 与真实 KEY1 的自动端到端 smoke。

`2026-05-18` P13.3 自动化调试入口：`Listener-Type` 已新增 `submit_embedded_audio_file` IPC 和 CLI 参数 `--submit-embedded-audio*`。该入口把本地 `16k mono i16` WAV/PCM 构造成成功的 `VKA1` replay session，再进入同一条 coordinator 产品链路。它不是最终 BLE host adapter，但可以让 pipeline artifact、真实 BLE 抓到的 WAV、随机 TTS fixture 直接用于插入 / 历史 smoke，减少人工说话和手动复制音频。

`2026-05-18` S2.1 native BLE source MVP：`Listener-Type` 已新增 Windows WinRT BLE `capture once` 入口。软件端按官方 GATT/WinRT 路径发现 `VKA1` service、订阅 notify characteristic，并把收到的 notification batch 送入 `submit_embedded_audio_notifications` 同一条 coordinator 产品链路。该能力已完成编译级验证，仍需真实设备 KEY1 + 当前光标插入 / 历史记录 smoke 才能宣告 P13.3 产品链路通过。

`2026-05-18` 火山 ASR provider baseline：`Listener-Type` 已切到火山 ASR，并用独立 probe 验证 `activeAsr=volcengine`、`resourceId=volc.bigasr.sauc.duration`、凭据可用。随机 TTS source WAV 直接送火山可得到 raw transcript：“后天下午3点提醒我检查蓝牙音频回放，并保存火山识别报告。”。同一轮真实 BLE 抓音传输 `407/407` 包、丢包 `0`，但火山返回空 transcript；抓音统计显示 `recorded_peak=32768`、`active_frame_count=6`。结论：软件 provider 和 BLE 传输不是当前瓶颈，真实声学输入存在削顶/摆位/增益问题，需要先校准再把准确率纳入 gate。

## 超时与错误处理建议

后端或中转层现在就应该冻结这些处理原则：

- 所有抓音、上传、回归脚本必须带明确时间预算
- 超过预算应按 `timeout` 处理，而不是无限等待
- 会话超时应区分：
  - 未收到 `session_start`
  - 已开始但未收到 `session_stop`
  - 已结束但包数不完整

当前更推荐保留的错误信息类别：

- `timeout`
- `transport_loss`
- `session_cancelled`
- `reconnect_required`
- `device_unavailable`

不建议后端直接把当前平台相关错误原样固化为协议级错误码，例如：

- `OSError(22)`
- `ClearCommError failed`
- `FileNotFoundError(2, '系统找不到指定的文件。')`

这些更适合作为诊断日志，而不是正式业务协议语义。

## 当前保证与不保证

### 当前可以依赖的部分

- 音频输入格式：`16k / mono / 16-bit / PCM little-endian`
- 上层会话模型：`session_start -> audio_data -> session_stop/session_cancel`
- 会话内顺序编号语义：`packet_sequence`
- 会话结束总包数字段：`expected_packet_count`
- 设备到 Windows 主机的 BLE 音频传输主干
- `Listener-Type` 已有 batch `VKA1 notifications -> coordinator` 产品链路入口
- `Listener-Type` 已有本地 `WAV/PCM file -> VKA1 replay -> coordinator` 自动化调试入口
- `Listener-Type` 已有 native Windows BLE `capture once -> VKA1 notifications -> coordinator` 代码入口
- P1-P10 standard/realistic 自动矩阵的当前稳定性结论

### 当前不要假设已经成立的部分

- 真实 BLE 声学音频已经达到火山 ASR 准确率 gate
- `Listener-Type` native BLE scan/connect/notify 已经完成真实 KEY1 产品 smoke
- 真实 KEY1 已经自动触发 `Listener-Type` 插入 / 历史记录
- `Listener-ai-agent` 已经有稳定 raw audio 输入 API
- 可选 Agent handoff 已经有稳定 app 级文本入口
- `BLE` 承载一定是最终产品的唯一传输层

## 面向后端的接口建议

如果后端现在要开始对接，建议按“语音输入前端输出契约”建模，而不是按“BLE 包协议”建模。

建议至少冻结这些输入字段：

- `protocol_version`
- `device_id`
- `session_id`
- `audio_format`
- `sample_rate_hz`
- `channels`
- `sample_width_bits`
- `packet_sequence`
- `expected_packet_count`
- `payload_pcm_bytes`
- `is_final`
- `session_end_reason`

其中：

- `audio_format`
  当前建议固定为 `pcm_s16le`
- `is_final`
  表示该会话是否已经进入最终结束状态
- `session_end_reason`
  当前建议至少区分：
  - `stop`
  - `cancel`
  - `timeout`
  - `transport_error`

正式后端协议文档仍应由后端维护，但建议以上层契约为冻结基线。

## 未来切到 Wi-Fi 时的复用原则

未来如果从 `BLE` 切到 `Wi-Fi` 传音频到后端，建议：

- 保留当前上层会话语义
- 保留当前音频格式契约
- 保留 `session_id / packet_sequence / expected_packet_count` 这些上层字段含义
- 只替换底层承载

也就是说，推荐复用的是：

- 会话模型
- 音频格式
- 成功 / 失败口径
- 超时与重试原则

不一定需要原样复用的是：

- `BLE notify` 本身
- 历史兼容字段名，如 `chunk_index / fragment_index / fragment_count`
- 当前 Windows 抓音脚本的诊断输出格式

如果未来走 `Wi-Fi`，更适合的封装通常会是：

- 元信息 + 二进制音频负载
- 或显式版本化的消息结构

不建议继续背着只为 `BLE` 兼容留下的线协议包袱。

## 当前项目建议

从当前产品目标看，建议这样推进：

- `BLE` 继续作为当前 bring-up 和前端联调主路径
- 后端现在就基于这份输入契约开始联调
- 后端正式协议文档由后端维护，但应以这里的音频和会话语义为冻结边界
- 后续如果切 `Wi-Fi`，优先迁移承载层，不推翻会话层

一句话总结：

当前真正应该冻结给后端的，不是 `BLE` 本身，而是 `16k PCM + 明确会话边界 + 顺序包重组 + 超时/结束语义` 这一层契约。

## 关联文档

- [设备端音频采集、录音控制与 BLE 上传链路](./audio_capture_ble_upload.md)
- [产品方案](../product_solutions.md)
