# 嵌入式音频接入 Listener 软件方案

状态：草案  
日期：2026-05-18  
关联仓库：

- 固件：`C:\Users\Billy\Desktop\listener\voice-keyboard-firmware`
- 桌面听写：`C:\Users\Billy\Desktop\listener\Listener-Type`
- AI Agent：`C:\Users\Billy\Desktop\listener\Listener-ai-agent`

## 执行进度

- `2026-05-18`：S0 已完成。已同步 `!docs/features/voice_input_backend_contract.md` 的 P1-P10 最新稳定性结论、P13 软件后端闭环口径、`Listener-Type` / `Listener-ai-agent` 职责边界和随机人声测试策略。
- `2026-05-18`：S1.1 已完成第一轮验证。在 `Listener-Type` 新增纯 Rust `VKA1` parser/session collector 初版和单元测试，用于后续 BLE host adapter、协议回放和 TTS fixture 注入。已按官方路径安装 Rust stable minimal 和 Visual Studio Build Tools C++ 工具链；`embedded_audio.rs` 已 rustfmt，并通过临时轻量 harness 运行 6 个 parser/collector 单测。整套 `Listener-Type` Tauri lib 测试依赖较重，留到接入 coordinator / WinRT 前再跑完整 gate。
- `2026-05-18`：S1.2 已完成纯软件协议回放基础。`Listener-Type` 的 `embedded_audio.rs` 已支持把 PCM fixture 切成 `VKA1` `session_start -> audio_data... -> session_stop` notifications，也支持 cancel/error 控制包和 `collect_notifications` 回放入口。临时轻量 harness 已通过 11 个 parser/collector/replay 单测。
- `2026-05-18`：S1.3 已完成 P13.1 纯软件 fixture runner。`Listener-Type/tools/embedded_audio_replay` 已支持读取 `wav` / `pcm16le`、生成 `VKA1` replay、输出 `replay_result_json`，并提供 `generate_tts_fixtures.ps1` 用 Windows 内建 `System.Speech` 根据 seed 生成随机中文人声 WAV 和 manifest。smoke：`Seed=20260518 Count=2` 生成 2 条 TTS fixture，replay 全部 `PASS`。
- `2026-05-18`：S1.4 / P13.2 纯软件 ASR baseline 已完成。`Listener-Type/tools/foundry_runtime_prepare` 已按官方 Foundry Local / Windows App Runtime 路径准备运行时，`runtimeReady=true`，native DLL 包含 `Microsoft.AI.Foundry.Local.Core.dll`、`onnxruntime.dll`、`onnxruntime-genai.dll`。`Listener-Type/tools/foundry_asr_probe prepare --model whisper-small` 已通过，模型 ID 为 `openai-whisper-small-generic-cpu:3`。随机 TTS smoke：`Seed=20260518 Count=1`，`Microsoft Huihui Desktop` 合成“明天下午两点提醒我检查蓝牙音频丢包率。”，`VKA1` replay `PASS`，Foundry raw transcript 为“明天下午兩點提醒我檢查藍牙音頻丟包率。”，语义正确但存在简繁差异，后续准确率统计需加入简繁归一。
- `2026-05-18`：S1.5 / P13.2 真实 BLE acoustic smoke 已完成。通过 native pipeline 持有 `COM3,BLE`，PC 扬声器循环播放 `Microsoft Huihui Desktop` TTS“明天下午两点提醒我检查蓝牙音频丢包率。”，设备麦克风采集后经 BLE 回传 WAV，再由 `Listener-Type` Foundry Local `whisper-small` 转写。pipeline artifact：`.cache/claude_executor/20260518-111311-p13-ble-tts-asr-smoke`，`pipeline_policy.json` 为 `PASS` 且 `continue_ok=true`。抓音结果：`received_packet_count=407`、`expected_packet_count=407`、`missing_packet_count=0`、`packet_loss_ratio=0.0`、`duration_seconds=6.1`、`best_corr=0.943`。ASR raw transcript：“明天下午兩點提醒或檢查藍芽音頻中包”。结论：真实声学链路已能进入后端 ASR 并产出文字；识别质量还不是准确率 gate，应继续做简繁归一、CER 统计、音量/摆位/播放窗口优化。
- `2026-05-18`：S1.6 / P13.3 产品链路入口初版已完成。`Listener-Type` 新增 `submit_embedded_audio_notifications` Tauri IPC：接收完整 `VKA1` notification batch，使用 `SessionCollector` 重组 PCM，然后进入现有 coordinator 听写链路，复用 Foundry Local Whisper / Whisper 兼容 batch ASR、polish、插入、history；不走 CPAL，不要求麦克风权限；开启录音调试时会把重组 PCM 归档为 history 对应 WAV。验证：`npm ci`、`npm run build`、`cargo check --manifest-path src-tauri/Cargo.toml`、`npm run check:traceability`、`npm run check:brand`、`npm run check:cloud`、`cargo test --manifest-path tools/embedded_audio_replay/Cargo.toml` 全部通过；`cargo test --manifest-path src-tauri/Cargo.toml embedded_audio --lib` 编译完成但测试进程在 Windows 动态库入口点处启动失败，暂以轻量 replay crate 覆盖 `embedded_audio.rs` 的 11 个 parser/collector/replay 单测。
- `2026-05-18`：S1.7 / P13.3 自动化调试入口已完成。`Listener-Type` 新增 `submit_embedded_audio_file` Tauri IPC 和 CLI 参数：`--submit-embedded-audio <path>`、`--submit-embedded-audio-wav <path>`、`--submit-embedded-audio-pcm16le <path>`。该入口读取本地 `16k mono i16` WAV/PCM，构造成成功的 `VKA1` replay session，再走与 `submit_embedded_audio_notifications` 相同的 coordinator 产品链路，用于把真实 BLE 抓到的 WAV、随机 TTS fixture 或后续 pipeline artifact 直接打进插入/历史 smoke。验证：`cargo test --manifest-path tools/embedded_audio_replay/Cargo.toml` 13/13 通过，`cargo check --manifest-path src-tauri/Cargo.toml` 通过，`npm run build`、`npm run check:traceability`、`npm run check:brand`、`npm run check:cloud`、`git diff --check` 通过。
- `2026-05-18`：S2.1 native BLE source MVP 已完成代码入口。`Listener-Type` 新增 Windows WinRT BLE 接收模块：按官方 GATT 路径扫描 `VKA1` service UUID，打开 notify characteristic，注册 `ValueChanged`，写 CCCD `Notify`，收集到 `session_stop/session_cancel/session_error` 后清理订阅；新增 `submit_embedded_audio_ble_once` Tauri IPC 和 CLI 参数 `--submit-embedded-audio-ble-once [timeout_ms]`，可把真实 BLE notification batch 直接送入同一条 coordinator 产品链路。当前已通过编译检查，真实 KEY1 插入 smoke 仍需 pipeline + `COM3,BLE` 执行。
- `2026-05-18`：S2.2 / S3 前端入口第一版已完成。因 Tai 暂未在线，按人工指令由 Codex 代做 `S3 前端 UI`：`Listener-Type` 设置页新增 `Microphone` / `Listener BLE` 输入源选择，持久化 `dictationInputSource` 偏好；选择 `Listener BLE` 后显示 Windows 支持状态、一键 BLE 测试和 `received/expected/missing/duration` stats。验证：`npm run build` 通过，`npm run check:traceability` 通过；第一次冷 `cargo check --manifest-path src-tauri/Cargo.toml` 因依赖编译超时但无编译错误，随后使用同一目标目录 warm rerun 通过。该项只代表 UI/设置入口和一次性 BLE 测试完成，不代表真实 KEY1 产品链路已验收。
- `2026-05-18`：P13.2 已切换并验证火山 ASR provider。`Listener-Type/tools/volcengine_asr_probe` 读取 `com.listener.type` 凭据和 `preferences.json`，确认 active ASR 为 `volcengine`，`resourceId=volc.bigasr.sauc.duration`。同一条随机 TTS source WAV 直接送火山识别通过，raw transcript 为“后天下午3点提醒我检查蓝牙音频回放，并保存火山识别报告。”；说明 provider、凭据、资源 ID、音频格式和 paced feed 都可用。
- `2026-05-18`：P13.2 真实 BLE + 火山 ASR smoke 已执行但未达准确率 gate。native pipeline 持有 `COM3,BLE`，TTS 原文为“后天下午三点提醒我检查蓝牙音频回放，并保存火山识别报告。”；真实 BLE 抓音 `407/407` 包、丢包 `0`、`duration_seconds=6.1`、`best_corr=0.896`，但 `recorded_peak=32768` 且 `active_frame_count=6`，火山返回空 transcript。结论：BLE 传输链路可用，火山软件 baseline 可用；当前失败集中在扬声器到设备麦克风的声学质量/增益/摆位，不应判定为 BLE 丢包或 provider 配置失败。
- `2026-05-18`：P13.6 固件 P1-P10 回归已通过。pipeline artifact：`.cache/claude_executor/20260518-140307-p13-p1-p10-realistic-regression`；命令使用 realistic profile + `--fail-on-warning`，随机 seed `1424787968`，结果 `matrix_total=10`、`matrix_failed=0`、`matrix_warning=0`、`matrix_skipped=0`，结构化结果为 `tests/artifacts/ble_product_matrix/p13_p1_p10_latest_result.json`。这说明前面蓝牙时序修复未引入 P1-P10 产品矩阵回归。
- 当前人工介入点：前端 source selection 已不再是阻塞项。进入下一轮真实 BLE 准确率 gate 前，需要调整物理声学条件或固件输入增益，优先降低 PC 播放音量、调整扬声器与设备麦克风距离/朝向，避免 `recorded_peak=32768` 削顶；必要时降低固件 `AUDIO_CAPTURE_INPUT_GAIN_DB` 后重刷再测。P13.3 “真实设备 KEY1 -> native BLE host -> 当前光标插入/历史记录”仍需设备在线、系统已配对、目标输入窗口/光标明确，并通过 pipeline + `COM3,BLE` 资源锁执行。

## 结论

嵌入式设备的原始音频应优先接入 `Listener-Type`，由它复用现有的录音、ASR、润色、插入和历史记录链路。`Listener-ai-agent` 在当前阶段不应直接接收 BLE PCM 或 WAV，它更适合作为下游，只接收 `Listener-Type` 产出的最终文本或明确的用户指令。

推荐链路：

```text
ESP32-S3 firmware
  -> BLE GATT Notify: VKA1 session_start / audio_data / session_stop
  -> Listener-Type embedded audio source
  -> Listener-Type existing ASR providers
  -> polish / style / history / insertion
  -> optional text handoff to Listener-ai-agent
```

这个方案的核心收益是：固件继续保持简单稳定，桌面端只新增一个“外部 PCM 音频源”，不会重写 ASR 和插入链路，也不会让 Agent 应用背上实时音频协议。

## 已确认现状

### 固件侧

固件仓库已经有稳定的 BLE 音频会话协议：

- 音频格式：`16 kHz / mono / signed 16-bit / little-endian PCM`。
- 协议头：`VKA1`，20 字节 header。
- 包类型：`session_start`、`audio_data`、`session_stop`、`session_cancel`、`session_error`。
- `audio_data` 使用 `packet_sequence`，不再回到旧的 chunk/fragment 语义。
- Windows host 的 Python 工具已经验证了 BLE Notify 订阅、session reassembly、丢包统计、WAV 输出。
- 当前音频 BLE 产品矩阵 P1-P10 已经达到标准/realistic 多 seed 通过，后续软件对接对应 P13。

需要注意：`!docs/features/voice_input_backend_contract.md` 的部分状态文字仍偏旧，后续应把 P1-P10 最新结论和 P13 对接要求同步进去。

### Listener-Type

`Listener-Type` 已经具备完整听写后端：

- Rust 后端使用 CPAL 录音，输出格式正好是 `16 kHz / mono / i16 little-endian PCM`。
- `recorder::AudioConsumer` 和 ASR 消费者已经支持持续接收 PCM chunk。
- coordinator 已有 `start_dictation`、`stop_dictation`、`cancel_dictation` 流程。
- streaming ASR 已有 Volcengine/Bailian 等路径，batch ASR 已有 Whisper/OpenAI-compatible 路径。
- 录音结果已经接入润色、风格、插入、历史记录。

因此嵌入式音频最小改动点不是新增一套 ASR，而是新增一个与麦克风并列的外部音频源。

### Listener-ai-agent

`Listener-ai-agent` 当前定位是桌面 AI Agent 客户端：

- Node sidecar 提供 chat/session/task/im/plugin 等能力。
- 当前没有稳定的原始音频输入 API。
- `/chat/send` 一类能力面向文本、图片和运行时配置，不适合作为 BLE PCM 实时入口。
- tool attachment 中虽然有 audio 类型，但它是工具产物展示，不是用户听写输入链路。

所以 v1 不建议让固件音频直接进入 `Listener-ai-agent`。如果产品需要“按键说话给 Agent”，也应先由 `Listener-Type` 完成 ASR，再把最终文本交给 Agent。

## 目标

1. 用户按下嵌入式设备的语音键后，Windows 上的 `Listener-Type` 可以接收 BLE PCM 音频。
2. `Listener-Type` 将 BLE 音频当作一个外部录音源，复用现有 ASR、润色、插入、历史记录。
3. 固件 BLE 协议保持稳定，除非新增向后兼容的 metadata，不改 `VKA1` 基础包语义。
4. 可选支持把最终文本发送到 `Listener-ai-agent` 的当前会话，但不在 v1 传 raw audio。
5. 验收时形成 P13 软件对接矩阵，覆盖正常听写、取消、异常断链、重连、丢包统计和回归。
6. 验证音频优先使用固定人声样本或 TTS 生成样本，减少每轮人工说话。

## 非目标

- 不在固件上做 ASR、降噪、云端 API 调用或复杂业务状态。
- 不让 `Listener-ai-agent` 直接订阅 BLE 音频。
- 不把 `audio_data` 重新解释成旧版 chunk/fragment。
- 不改变 host BLE 订阅顺序约束，仍需保持 subscribe-before-connect 兼容。
- 不用软件对接掩盖 P1-P10 的固件时序回归。

## 推荐架构

### Listener-Type 新增模块

建议在 `Listener-Type` Rust 后端新增 embedded audio 模块，命名可选：

- `src-tauri/src/embedded_audio/`
- 或 `src-tauri/src/ble_audio_source.rs`

职责：

1. Windows BLE 设备发现、连接、Notify 订阅。
2. 解析 `VKA1` header。
3. 按 `session_id + packet_sequence` 重组 PCM。
4. 维护 session stats：收到包数、缺失包、重复包、补静音字节数、错误原因、BLE 断链原因。
5. 将 PCM chunk 推给现有 `AudioConsumer` 或等价抽象。
6. 将 `session_start`、`session_stop`、`session_cancel` 映射到 coordinator 的 session 生命周期。

### Coordinator 接入方式

推荐把“录音源”抽象为麦克风和嵌入式两种来源：

```text
DictationSource
  - Microphone: current CPAL Recorder
  - EmbeddedBle: BLE PCM source
```

嵌入式来源不应调用 CPAL，也不应要求麦克风权限。它应该在收到 `session_start` 时创建一次 dictation session，在持续收到 `audio_data` 时把 PCM 推给 ASR consumer，在 `session_stop` 时结束并等待最终 ASR 文本。

候选内部接口：

```text
start_embedded_dictation(device_id, session_id, audio_format)
push_embedded_pcm(session_id, pcm_bytes)
finish_embedded_dictation(session_id, reason, stats)
cancel_embedded_dictation(session_id, reason, stats)
```

实际实现时可以不暴露这些名字，但需要保持同样的生命周期语义。

### UI 和设置

`Listener-Type` 应新增一个输入源设置：

- `Microphone`
- `Listener BLE device`

嵌入式设备状态建议显示：

- 未连接
- 扫描中
- 已连接
- 正在听写
- 最近一次错误

听写中的悬浮状态、取消、最终插入和历史记录应尽量复用现有 UI，不另起一套“设备专用听写”体验。

### Listener-ai-agent handoff

v1 推荐只做文本交接：

```text
Listener-Type final text
  -> optional user-approved handoff
  -> Listener-ai-agent active session
```

这里需要一个稳定的 app 级入口，例如：

- Listener-ai-agent 提供本地应用 API，由 Rust proxy 或管理层转发到内部 `/chat/send`。
- 或提供明确 CLI/app protocol，用于“把文本发送到当前 Agent 会话”。

不建议 `Listener-Type` 直接依赖 `Listener-ai-agent` 的私有 sidecar session port，因为 sidecar 生命周期与 tab/session 绑定，跨应用直接调用容易破坏 Agent 侧的 owner 模型。

## 测试音频策略

为了减少人工干预，P13 验证默认不依赖人工每轮说话。建议准备一组固定中文人声 fixture：

- 短句：1 到 3 秒，用于 smoke test 和 ASR 基本可用性。
- 中句：5 到 10 秒，用于正常听写和标点稳定性。
- 长句：20 到 30 秒，用于长 session、BLE 缓冲和 finalization。
- 噪声版：同一句话叠加低强度环境噪声，用于弱环境回归。

测试分三层：

1. 软件注入：直接把 fixture PCM/WAV 注入 `Listener-Type` external audio source，用来验证 ASR、润色、插入、历史记录，不经过设备麦克风。
2. 协议回放：把 fixture PCM 切成 `VKA1 audio_data` 包，走 session parser/collector，用来验证软件协议处理、丢包统计、cancel/error 映射。
3. 声学播放：从 PC 扬声器播放 fixture 人声给嵌入式麦克风采集，用来验证真实麦克风、I2S/PDM、BLE Notify 和 Windows host 全链路。

前两层可以完全自动化；第三层需要真实设备和扬声器环境，但仍然不需要人工每次开口说话。测试人声应使用可授权样本、内部录制样本或通用 TTS，不使用未授权的特定真人声纹克隆。

### 随机句子和识别准确率

固定 fixture 适合回归，但不能覆盖 ASR 泛化能力。P13 可以增加 seeded 随机语料：每轮根据 seed 生成一批中文句子，再用 TTS 生成音频，同时保存 ground truth。

建议语料类型：

- 普通听写：日常短句、会议记录、备忘。
- 标点句：包含逗号、句号、问号、顿号的自然语句。
- 数字日期：金额、时间、日期、编号、百分比。
- 中英混合：产品名、模型名、路径、命令片段。
- Agent 指令：让 AI 总结、改写、创建任务、查询资料的口语化指令。
- 领域词表：从项目词表注入 `BLE`、`PCM`、`Listener-Type`、`session_id` 等固定术语。

每次测试应输出一份可复现 artifact：

```json
{
  "seed": 20260518,
  "case_id": "p13-random-0001",
  "category": "数字日期",
  "text": "明天下午三点半提醒我检查蓝牙音频丢包率。",
  "expected_normalized": "明天下午三点半提醒我检查蓝牙音频丢包率",
  "tts_wav": "artifacts/p13/p13-random-0001.wav",
  "pcm_sha256": "..."
}
```

准确率评估建议优先使用 raw ASR transcript，不使用润色后的文本做主指标。中文主指标使用 CER，英文和中英混合可补充 WER。文本比较前先做 normalization：去除首尾空白、统一全半角、统一常见标点、可选忽略大小写。

建议先跑 baseline，再把阈值收紧成 gate：

| 指标 | 初期用途 | 后续 gate 建议 |
| --- | --- | --- |
| `empty_result_rate` | 检查 ASR 是否完全失效 | 必须为 0 |
| `cer_avg` | 观察平均识别质量 | 不高于 baseline + 2% |
| `cer_p95` | 观察长尾错误 | 不高于 baseline + 5% |
| `exact_or_near_match_rate` | 观察稳定句子识别 | 不低于 baseline - 5% |
| `session_success_rate` | 检查 BLE/session 是否完成 | 必须为 100% |

随机不等于不可复现。任何失败都必须打印 seed、case_id、原文、ASR 原始结果、normalized diff 和音频文件路径。这样既能每轮覆盖不同句子，又能把失败样本固定下来复查。

## 分阶段实现计划

### S0：文档和合同同步

修改范围：固件仓库文档。

工作：

1. 同步 `voice_input_backend_contract.md` 中的最新 BLE 稳定性状态。
2. 将 P13 定义为“软件后端对接验证”。
3. 明确 `Listener-Type` 是 raw PCM owner，`Listener-ai-agent` 是 optional text handoff target。

验收：

- 文档中不存在 P1-P10 与当前矩阵结果冲突的陈述。
- P13 验收项可以被 Listener-Type 和固件双方共同执行。

### S1：Listener-Type host adapter MVP

修改范围：`Listener-Type`。

工作：

1. 迁移或复用固件仓库 Python host 的 `VKA1` parser/session collector 逻辑。
2. 先允许 helper/subprocess 或调试入口把 BLE session 输出为 WAV/PCM。
3. 用现有 batch ASR 验证“嵌入式按键 -> PCM/WAV -> ASR 文本”。

验收：

- KEY1 真实触发后，Listener-Type 能得到一段合法 `16k mono i16` 音频。
- WAV duration、packet_count、missing_count 能写入日志。
- 不影响原有麦克风听写。

### S2：Listener-Type native external source

修改范围：`Listener-Type`。

工作：

1. 用 Rust/WinRT 实现 BLE device scan/connect/notify。
2. 将 BLE PCM source 接入 coordinator，而不是只生成 WAV 文件。
3. 复用现有 streaming/batch ASR consumer。
4. 增加 source selection 设置和设备状态。

验收：

- 选择 `Listener BLE device` 后，KEY1 可以直接开始听写。
- `session_stop` 后进入现有 finalization 流程。
- cancel/error/link_lost 能让 coordinator 回到 Idle。
- CPAL 麦克风路径测试仍通过。

### S3：端到端听写产品化

修改范围：`Listener-Type`，必要时少量固件 metadata。

工作：

1. 对接悬浮状态、取消、错误提示、历史记录。
2. 在历史记录中保存 device/session stats。
3. 调整用户可见错误文案，比如 BLE 断开、设备未找到、音频包缺失过多。
4. 如确实需要，固件新增向后兼容的 device info 或 firmware version metadata。

验收：

- KEY1 -> ASR -> 润色 -> 插入到当前光标位置。
- 历史记录可追踪 BLE session_id、duration、packet stats。
- 断链/取消不会留下卡住的 Recording 状态。

### S4：Listener-ai-agent 文本交接

修改范围：`Listener-Type` 和 `Listener-ai-agent`，仅当产品需要。

工作：

1. 在 Listener-ai-agent 侧设计稳定的 app 级文本入口。
2. Listener-Type 增加用户开关：听写完成后发送到当前 Agent 会话。
3. 明确发送内容是 final polished text、raw transcript，还是二者都带 metadata。

验收：

- 用户开启 handoff 后，KEY1 的最终文本能进入 Agent 当前会话。
- 关闭 handoff 后，Listener-Type 仍只做普通听写插入。
- 不传 raw PCM，不依赖私有 sidecar port。

## P13 验收矩阵

| 编号 | 场景 | 通过标准 |
| --- | --- | --- |
| P13.1 | BLE host adapter smoke | KEY1 触发后，软件端收到完整 session，并能产出合法 WAV/PCM stats |
| P13.2 | Listener-Type ASR | 嵌入式 PCM 进入现有 ASR provider，得到 raw transcript |
| P13.3 | Listener-Type 产品链路 | 最终文本完成润色/插入/历史记录，且记录 session stats |
| P13.4 | cancel/error/reconnect | `session_cancel`、`session_error`、BLE 断链后软件回到 Idle |
| P13.5 | Agent handoff | 开关开启时，最终文本进入 Listener-ai-agent 当前会话 |
| P13.6 | 固件回归 | P1-P10 realistic `--fail-on-warning` 仍通过 |

## 接口合同

### 音频格式

固定为：

```text
sample_rate_hz = 16000
channels = 1
sample_width_bits = 16
endianness = little
encoding = signed PCM
```

### Session metadata

软件端至少应保留：

- `device_id`
- `session_id`
- `started_at`
- `stopped_at`
- `duration_ms`
- `packet_count`
- `expected_packet_count`
- `missing_packet_count`
- `duplicate_packet_count`
- `silence_filled_bytes`
- `end_reason`

### 错误映射

固件错误建议映射为软件端状态：

| 固件错误 | 软件行为 |
| --- | --- |
| `queue_full` | 结束 session，提示设备忙或链路拥塞 |
| `notify_timeout` | 结束 session，提示蓝牙发送超时 |
| `link_lost` | 结束 session，进入可重连状态 |
| `sequence_overflow` | 结束 session，记录协议异常 |
| `invalid_state` | 取消 session，提示设备状态异常 |
| `no_memory` | 取消 session，提示设备内存不足 |
| `packet_too_large` | 取消 session，提示协议版本不匹配 |
| `transport` | 结束 session，提示蓝牙传输错误 |

## 风险和注意事项

1. Windows BLE Rust 实现要复刻 Python host 已验证的订阅顺序，尤其是 CCCD/Notify 的时序。
2. `Listener-Type` 和 `Listener-ai-agent` 都有桌面级交互，不能让两个应用同时抢全局热键或重复插入文本。
3. Agent 应用当前没有稳定 raw audio API，提前接 raw audio 会扩大协议面和调试成本。
4. 如果 BLE 丢包较多，软件端可以做补静音和 stats，但不能把链路问题伪装成 ASR 问题。
5. 如果后续要做实时 partial transcript，需要优先走 Listener-Type streaming ASR，而不是新增设备到 Agent 的旁路。

## 建议验证命令

固件回归：

```powershell
python .\tools\verify_audio_ble_product_matrix.py --profile realistic --fail-on-warning
```

Listener-Type 基础回归：

```powershell
cargo test --manifest-path src-tauri/Cargo.toml --lib
npm run build
```

Listener-ai-agent 仅在改动 handoff API 时验证：

```powershell
npm run typecheck
npm run test
```

## 待决策问题

1. v1 是否只需要普通听写插入，还是必须同时支持“发给 Agent 当前会话”？
2. 嵌入式设备识别优先用 BLE name、GATT device info，还是固件 session metadata？
3. v1 是否接受 batch ASR MVP，还是从第一版就要求 streaming partial transcript？
4. Listener-Type 与 Listener-ai-agent 是继续两个独立应用协作，还是长期收敛成一个统一入口？

## 下一步建议

优先先把真实声学质量拉回可识别区间，再进入真实产品 smoke。当前 P13.2 的软件火山 baseline 已通过，真实 BLE 传输和 P1-P10 固件回归也已通过，设置页 source selection 和一次性 BLE 测试入口已完成；但 BLE 声学 WAV 被火山判为空 transcript。下一步先用降低音量/调整摆位/必要时降低输入增益的方式重跑“PC TTS -> 设备麦克风 -> BLE WAV -> 火山 ASR”，通过后再用 pipeline + `COM3,BLE` 跑“KEY1 -> native BLE -> ASR -> 插入/历史记录”端到端验证，并把识别准确率升级成 seeded 多句 CER gate。
