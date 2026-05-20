# BLE 音频产品测试矩阵

## 状态

- status: `validated` (2026-05-19 全量硬件通过：A1-A19 PASS, H1 手动 PASS, T1-T5 PASS)
- scope: BLE 音频真实用户场景矩阵。`A1-A19` 为自动化用户场景 case，`H1-H3` 为手动 case，`T1-T5` 为旧传输层 case（`--cases transport`）
- source_of_truth: `tools/verify_audio_ble_product_matrix.py` 中的 `CASE_ORDER` / `MANUAL_CASES` / `TRANSPORT_ONLY_CASES` / `CASE_SUITES` / `CASE_RUNNERS` / `CASE_DESCRIPTIONS`

## 当前能力

矩阵脚本模拟真实用户使用语音键盘的场景。每个 A case 对应一个真实用户故事——正常说话、犹豫停顿、小声说话、说很快、短命令、极短语音、标点命令、中英混合、重复同句、环境噪音、旁边有人说话、取消、误触、一边走一边说、长时间空闲、网络异常、长时间综合使用。

TTS 音频通过分段播放（句间随机停顿）、噪音叠加（白噪声/粉红噪声）、语音干扰叠加（次声 TTS 混合）、渐变音量（模拟走动距离变化）等能力模拟真实使用条件。句子池覆盖长句（24条）、短命令（17条）、极短词（10条）、标点命令（8条）、中英混合（15条）。

每个 A case 默认叠加 Listener-Type 产品链路 overlay：BLE 音频 → ASR 识别 → 文本插入 → 准确率门禁。默认执行策略是 fail-fast。产品链路 overlay 自动附加胶囊（Capsule）UX 验证。

## Suite 分层

| Suite | Case | 说明 |
|-------|------|------|
| `smoke` | A1, A3, A14, A15 | 核心门禁，快速验证 |
| `daily`/`auto` | A1-A16 | 日常回归，不含长 idle/soak |
| `full` | A1-A19 | 全量矩阵 |
| `soak` | A17, A18, A19 | 长 idle + 网络异常 + 综合 soak |
| `transport` | T1-T5 | 旧传输层 case，通过 `--cases transport` 运行 |

`--cases auto` 默认只跑 A1-A16（不含 A17 5min idle、A18 网络异常、A19 soak）。

## 胶囊（Capsule）验证

胶囊是 Listener-Type 桌面端的核心 UX 组件：听写过程中显示实时 partial preview，识别完成后展示最终文本并插入光标。矩阵在产品链路 overlay 中自动附加胶囊验证：

| 场景 | 验证内容 | 适用 case |
|---|---|---|
| 正常听写 | `partial_preview_count > 0`（胶囊可见）+ `final_text_received` | A1-A13, A16-A19 |
| 长段听写 | 额外验证 partial preview 内容质量（prefix CER ≤ 0.5） | A2 |
| 取消 | 胶囊不应显示有意义的 partial preview | A14 |
| 静音/误触 | 胶囊不应显示有意义的 partial preview | A15 |

胶囊验证逻辑位于 `validate_capsule_evidence()` 函数。验证失败时产生 warning 级别结果（不直接 fail 主链路）。

## 代码入口

- `tools/verify_audio_ble_product_matrix.py`
- `tools/ble_audio_regression_common.py`
- `tools/capture_audio_ble_wav.py`
- `..\Listener-Type\tools\embedded_audio_replay\run_ble_stream_smoke.ps1`

## 已实现 case

### 基本使用

| ID | 场景 |
|---|---|
| A1 | 单句 normal 基线：说一句话，文字出现在光标 |
| A2 | 长段话（3-5句）：验证 partial preview 内容质量和 final insertion |
| A3 | 连续多轮：每轮说不同的话，混合 normal/fast/low-volume profile |

### 说话风格

| ID | 场景 |
|---|---|
| A4 | 犹豫停顿：句间 1-3s 随机停顿 |
| A5 | 小声说话：low-volume profile |
| A6 | 快速说话：fast profile（rate=3） |

### 内容多样性

| ID | 场景 |
|---|---|
| A7 | 短命令+长句混合：随机交替 |
| A8 | 极短语音（1-2字）：验证 ASR 能识别 |
| A9 | 标点命令：warning_only，验证输出趋势 |
| A10 | 中英混合：句内中英交替 |
| A11 | 重复同句 3 轮：验证每轮独立不串 |

### 环境干扰

| ID | 场景 |
|---|---|
| A12 | 环境噪音：TTS + 白噪声/粉红噪声叠加（SNR 12dB） |
| A13 | 语音干扰：TTS + 次语音叠加（主声高 8dB） |

### 操作异常

| ID | 场景 |
|---|---|
| A14 | 取消后恢复：负向验证 + 正常录音 |
| A15 | 静音误触：负向验证 |
| A16 | 渐变音量：模拟走动距离变化（gain 0.4-1.0 周期） |

### 时间与网络

| ID | 场景 |
|---|---|
| A17 | 长时间空闲后首录（5min idle） |
| A18 | ASR 网络异常：可控超时验证不卡死 |

### 综合压力

| ID | 场景 |
|---|---|
| A19 | 综合 soak：混合所有 profile 和句子类型 |

### 手动 case

| ID | 场景 |
|---|---|
| H1 | 物理 KEY1 语音输入 |
| H2 | 不同距离/角度说话（需手动操作） |
| H3 | 不同人说话（需切换声音或真人） |

### 传输层 case（--cases transport）

| ID | 原对应 | 场景 |
|---|---|---|
| T1 | 旧 A4 | BT 重启后重连 |
| T2 | 旧 A5 | 多轮重连循环 |
| T3 | 旧 A6 | Host 恢复不重启蓝牙 |
| T4 | 旧 A10 | 快速 toggle 压力 |
| T5 | 旧 A11 | 并发 BLE 客户端 |

## Audio Profiles

| Profile | TTS rate | Gain | 最低准确率 | warning_only | 使用 case |
|---------|---------|------|----------|-------------|---------|
| `normal` | 0 | 4.0 | 0.85 | 否 | A1, A2, A3, A4, A7-A8, A10-A11, A13-A18 |
| `fast` | 3 | 4.0 | 0.78 | 否 | A2, A3, A6, A19 |
| `low-volume` | 0 | 1.8 | 0.72 | 否 | A3, A5, A19 |
| `noisy` | 0 | 4.0+噪音 | 0.70 | 否 | A12, A19 |
| `punctuation` | 0 | 4.0 | 0.60 | **是** | A9 |
| `fast-low-volume` | 3 | 1.8 | 0.65 | **是** | A19 |

A9 标点命令使用 `punctuation` profile（`warning_only=True`），因为 ASR 可能输出文字"逗号"而非标点符号，准确率不稳定，只作诊断/warning 不 hard fail。

## 句子池

| 池 | 数量 | 用途 |
|---|---|---|
| CHINESE_SENTENCE_POOL | 24 | 长句基线 |
| SHORT_COMMAND_POOL | 17 | 短命令（2-6字） |
| ULTRA_SHORT_POOL | 10 | 极短（1-2字） |
| PUNCTUATION_COMMAND_POOL | 8 | 标点命令 |
| MIXED_LANGUAGE_POOL | 15 | 中英混合 |

## 验收命令

```powershell
python -m compileall -q tools
python .\tools\verify_audio_ble_product_matrix.py --list-cases
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --fail-on-warning
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases A19 --soak-round-count 3 --fail-on-warning
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases H1 --fail-on-warning
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases T1,T2,T3,T4,T5 --transport-only --fail-on-warning
```

真实硬件命令前按资源加锁：`aiw.ps1 lock -Resource COM3` 与 `aiw.ps1 lock -Resource BLE`，用完分别释放。

## 关键不变量

- `CASE_SUITES` 定义 suite 分层：`--cases auto` 默认只跑 A1-A16，A17/A18/A19 在 `soak` suite 中需显式指定。
- 传输层 case（T1-T5）只通过 `--cases transport` 运行，不在 `--cases auto` 中。
- `--transport-only` 只控制产品链路 overlay 开关，不影响 case 选择。
- 默认逐 case fail-fast；需要全量失败收集时加 `--continue-on-failure`。
- A2 验证 partial preview 内容质量（前缀 CER ≤ 0.5），不只是存在性。
- A9 标点命令使用 `warning_only=True` profile，不 hard fail。
- 胶囊验证失败产生 warning 不直接 fail。
- 每个 A case 只测一个维度，不混合多个用户场景。
- A18 使用可控超时（8s total / 5s listener），不依赖真实网络异常。
- 矩阵只做编排和验收，不重新实现 Listener-Type 的 BLE 流式/ASR/插入逻辑。
- `FAST_TTS_RATE = 3`，对应 fast profile 的 TTS rate。

## 已知限制

- 不同说话人音色需额外 TTS voice 或真人（H3 手动预留）。
- ASR 错误后的用户修改流程（产品决策待定）。
- 产品链路验收需要同级 `Listener-Type` 仓库、可用 ASR 配置、真实设备在线。
- RF 干扰/距离需外部环境（H2）。
- 胶囊视觉细节（退出动画、compact text 截断）需手动目视验证。
- 胶囊验证依赖 smoke 报告暴露 `partial_preview_count` / `last_partial_preview` 字段。
