# P13 嵌入式音频软件对接 — 任务文档

状态：活跃
日期：2026-05-18
协议：`!docs/ai_collaboration_protocol.md`

## 目标

完成嵌入式 BLE 音频从设备到桌面端听写的完整产品化。

## Worktree

| 仓库 | Codex | Tai | Claude 审查 | 主仓库 / 人类 |
|---|---|---|---|---|
| 固件 `voice-keyboard-firmware` | `C:\Users\Billy\Desktop\listener\voice-keyboard-firmware-codex-p13` | `C:\Users\Billy\Desktop\listener\voice-keyboard-firmware-tai-p13` | `C:\Users\Billy\Desktop\listener\voice-keyboard-firmware-claude-review-p13` | `C:\Users\Billy\Desktop\listener\voice-keyboard-firmware` |
| 桌面端 `Listener-Type` | `C:\Users\Billy\Desktop\listener\Listener-Type-codex-p13` | `C:\Users\Billy\Desktop\listener\Listener-Type-tai-p13` | `C:\Users\Billy\Desktop\listener\Listener-Type-claude-review-p13` | `C:\Users\Billy\Desktop\listener\Listener-Type` |

主仓库目录只用于人类确认、最终集成和发布。后续 AI 继续做代码或测试时，必须进对应角色的 worktree。

## 步骤

| # | 步骤 | 状态 | 说明 |
|---|---|---|---|
| 1 | 固件工具链增强（BLE 回归、紧凑 stdout、executor 结构化） | **已完成** | `verify_ble_tts_asr_smoke.py`、`transcribe_foundry_probe_json.py` 等 |
| 2 | VKA1 协议解析 + BLE 捕获（Listener-Type 侧） | **已完成** | `embedded_audio.rs` + `embedded_ble.rs` + 单元测试 |
| 3 | CLI/IPC 入口 + cancel/error 处理 | **已完成** | `--submit-embedded-audio-*` + coordinator 回 Idle |
| 4 | S3 前端 UI（input source 选择 + BLE 状态面板 + i18n） | **已完成** | Settings.tsx + EmbeddedBleStatusPanel |
| 5 | ASR 链路跑通（火山引擎） | **已完成** | Codex 已验证 |
| 6 | P1-P10 固件回归（`--fail-on-warning`） | **已完成** | 10/10 pass，warning=0 |
| 7 | Listener-Type S3 前端改动收口 | **已完成** | `be0b4b1 整理嵌入式 BLE 设置面板`；`npm run build`、`cargo check --manifest-path src-tauri/Cargo.toml` 通过，Rust 仅有既有 warning |
| 8 | 端到端集成验证：真实设备 KEY1 → BLE → ASR → 文本 | **进行中（Codex）** | 真实硬件 gate，需设备在线并按 KEY1；Codex 分支另有 `758f296 放宽 BLE notify ready 日志等待` 待集成审查 |
| 9 | 识别准确度验证：随机中文句子，对比原文 vs ASR 输出 | 未开始 | 依赖步骤 8 |
| 10 | 固件仓库 master 推送到 origin | 未开始 | 需人确认 |
| 11 | Listener-Type 分支推送到 origin | 未开始 | 需人确认；当前 `task/0516-listener-type-migration` 领先远端 |
| 12 | Codex BLE 工具改动集成 | 未开始 | 分支 `voice-keyboard-firmware-codex-p13` 已提交 `758f296`，需 Claude 审查后决定是否合入 `master` |

## 已完成的验收项

| 编号 | 验收项 | 标准 | 状态 |
|---|---|---|---|
| P13.2 | ASR 转写 | 嵌入式 PCM → raw transcript | 已完成（火山引擎） |
| P13.4 | cancel/error | 代码完成，待真实验证 | 代码完成 |
| P13.6 | 固件回归 | P1-P10 `--fail-on-warning` | 已通过 |

## 待验证

| 编号 | 验收项 | 依赖 |
|---|---|---|
| P13.1 | BLE host adapter smoke（真实 KEY1 → 完整 session） | 步骤 8 |
| P13.3 | 产品链路（文本润色/插入/历史记录） | 步骤 8 |
