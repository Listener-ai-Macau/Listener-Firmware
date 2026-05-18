# P13 嵌入式 BLE 流式 ASR 改造 — 计划快照

状态：活跃
日期：2026-05-18
实时任务文档：`C:\Users\Billy\Desktop\listener\.ai-shared\p13_embedded_ble_streaming_asr_plan.md`

## 目标

把当前嵌入式 BLE batch ASR 链路改为流式 ingest：

```text
session_start -> 创建 Listener-Type 听写 session 和 ASR consumer
audio_data     -> 到一包就推一包 PCM 给 ASR
session_stop   -> send last frame / end_session / await final
```

当前代码路径是 `capture_notifications_once -> Vec<Vec<u8>> -> collect_notifications -> reconstructed_pcm -> submit_embedded_pcm_for_dictation`，录完完整 session 后才开始识别。本计划目标是录音期间持续送 ASR，录完后只等待 finalization、润色和插入。

## 非目标

- 不改固件 `VKA1` 包语义。
- 不把 `audio_data` 重新解释成旧的 `chunk + fragment`。
- 不改变 host BLE 订阅顺序。
- 不在本轮做 Agent handoff。
- 不要求第一版显示 partial transcript；本轮目标是 ASR ingest 流式化，最终输出仍以 final text 为准。

## 分工

详见实时共享任务文档。摘要：

| # | 步骤 | 建议执行者 | 主要文件 |
|---|---|---|---|
| 1 | BLE notification 事件流 API | Codex | `Listener-Type/src-tauri/src/embedded_ble.rs` |
| 2 | 增量 VKA1 session collector | Tai | `Listener-Type/src-tauri/src/embedded_audio.rs` |
| 3 | coordinator 流式嵌入式 dictation | Codex | `Listener-Type/src-tauri/src/coordinator/dictation.rs` |
| 4 | cancel/error/link_lost 收尾 | Tai | `dictation.rs`、`embedded_ble.rs` |
| 5 | batch/debug 入口兼容 | Tai | `dictation.rs`、IPC 如需要 |
| 6 | 单元/轻量集成测试 | Codex + Tai | `Listener-Type` 测试 |
| 7 | 文档更新 | 任一实现者 | 固件仓库 `!docs/features/` 和 plans |
| 8 | 审查与回归 | Claude | build/check/tests/review |
| 9 | 真实设备 smoke | Claude 或人类指定 | COM3/BLE 资源锁 |

## 验收

| 编号 | 场景 | 标准 |
|---|---|---|
| S-BLE-1 | 软件 replay 流式路径 | replay start/audio/stop 可进入 ASR，最终文本与 batch 路径一致或接近 |
| S-BLE-2 | cancel/error | cancel/error 后 coordinator 回 Idle，不留下 Recording |
| S-BLE-3 | batch 兼容 | 现有 `submit_embedded_audio_file`、`submit_embedded_audio_notifications` 不回归 |
| S-BLE-4 | 真实 KEY1 smoke | 设备 KEY1 开始后 audio_data 在录音期间持续送入 ASR；KEY1 停止后输出 final text |
| S-BLE-5 | 固件回归 | P1-P10 realistic `--fail-on-warning` 不回归 |

## 启动

Codex:

```powershell
cd C:\Users\Billy\Desktop\listener\Listener-Type-codex-p13
codex
```

Tai:

```powershell
cd C:\Users\Billy\Desktop\listener\Listener-Type-tai-p13
tai
```

Claude:

```powershell
cd C:\Users\Billy\Desktop\listener\Listener-Type-claude-review-p13
claude
```
