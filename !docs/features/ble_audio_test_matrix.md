# BLE 音频产品测试矩阵

## 状态

- status: `simplified` (2026-06-04 从 28 case 精简到 2 个核心 case)
- scope: BLE 音频核心用户场景。A1 快速连续短录音，A2 长段录音。两个 case 均走完整产品链路（BLE→ASR→文本插入→准确率门禁→胶囊验证）。
- source_of_truth: `tools/verify_audio_ble_product_matrix.py` 中的 `CASE_ORDER` / `CASE_SUITES` / `CASE_DESCRIPTIONS`

## 当前能力

矩阵验证两个核心用户场景：

- **A1 快速连续短录音**：连续 5-8 轮短句（从 SHORT_COMMAND_POOL 随机取），每轮走完整产品链路，验证每轮独立识别且不丢轮
- **A2 长段录音**：单次 25-35 秒长段（3-5 句），验证完整传输 + partial preview 质量 + 最终识别准确率

## Suite 分层

| Suite | Case | 说明 |
|-------|------|------|
| `smoke` | A1, A2 | 完整矩阵 |
| `daily` | A1, A2 | 同 smoke |
| `full` | A1, A2 | 同 smoke |
| `auto` | A1, A2 | 同 smoke |

`--cases auto`、`--cases daily` 和 `--cases full` 均跑 A1 + A2。

## Audio Profile

| Profile | TTS rate | Gain | 最低准确率 | 使用 case |
|---------|---------|------|----------|---------|
| `normal` | 0 | 4.0 | 0.85 | A1, A2 |

## 句子池

| 池 | 数量 | 用途 |
|---|---|---|
| CHINESE_SENTENCE_POOL | 24 | A2 长句 |
| SHORT_COMMAND_POOL | 17 | A1 短命令 |

## 代码入口

- `tools/verify_audio_ble_product_matrix.py`
- `tools/ble_audio_regression_common.py`
- `tools/capture_audio_ble_wav.py`

## 验收命令

```powershell
python -m compileall -q tools
python .\tools\verify_audio_ble_product_matrix.py --list-cases
python .\tools\verify_audio_ble_product_matrix.py --port COMx --fail-on-warning
```

真实硬件命令前按资源加锁：`aiw.ps1 lock -Resource COMx` 与 `aiw.ps1 lock -Resource BLE-<address>`，用完分别释放。

## 关键不变量

- 产品链路 overlay 默认开启；`--transport-only` 关闭。
- 默认逐 case fail-fast；需要全量失败收集时加 `--continue-on-failure`。
- A2 验证 partial preview 内容质量（前缀 CER ≤ 0.5），不只是存在性。
- 胶囊验证失败产生 warning 不直接 fail。
- A1 每轮独立验证产品链路；出现丢轮（某轮无 transcript）记为 warning。
- 矩阵只做编排和验收，不重新实现 Listener-Type 的 BLE 流式/ASR/插入逻辑。
