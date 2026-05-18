# BLE 音频产品测试矩阵

## 状态

- status: `implemented`
- scope: BLE 音频产品使用面回归矩阵，`A` 为自动音频/传输 case，`H1` 为半自动物理按键 case，`H2` 为外部环境占位，`H3` 为 Listener-Type ASR/插入完整链路 case
- source_of_truth: `tools/verify_audio_ble_product_matrix.py` 中的 `CASE_ORDER` / `FULL_CHAIN_CASE_ORDER` / `CASE_RUNNERS` / `MANUAL_OR_EXTERNAL_CASES`

## 当前能力

矩阵脚本以中文 TTS 或回退合成音源驱动 BLE 音频链路，覆盖单次录音、多轮连续录音、断连恢复、多轮重连、空闲恢复、取消恢复、静音负向/快速 toggle、采集中并发客户端和物理 KEY1 半自动验证。完整产品验收通过 `H3` 接入 Listener-Type 原生 BLE 流式 smoke，把真实 BLE 音频送入 ASR，验证 final transcript 非空，并记录历史与插入状态证据。

## 代码入口

- `tools/verify_audio_ble_product_matrix.py`
- `tools/ble_audio_regression_common.py`
- `tools/capture_audio_ble_wav.py`
- `..\Listener-Type\tools\embedded_audio_replay\run_ble_stream_smoke.ps1`

## 已实现 case

| ID | 类型 | 场景 |
|---|---|---|
| A1 | 自动 | 单次录音 |
| A2 | 自动 | 多轮连续录音 |
| A3 | 自动 | 断连恢复，支持 `--reconnect-mode bt_restart|host_recover` |
| A4 | 自动 | 多轮独立重连 |
| A5 | 自动 | 空闲后首录 |
| A6 | 自动 | 取消后恢复 |
| A7 | 自动 | 静音负向断言 + 快速 toggle 压力 + 恢复验证 |
| A8 | 自动 | 采集中并发 BLE 客户端 |
| A9 | 自动 | 快速启停压力 |
| A10 | 自动 | 多轮重连（含设备复位） |
| H1 | 半自动 | 物理 KEY1 录音 |
| H2 | 外部 | RF 干扰/距离，需特殊环境 |
| H3 | 完整链路 | 真实 BLE 音频 -> Listener-Type -> ASR final text -> 当前光标插入/历史证据 |

## 验收命令

```powershell
python -m compileall -q tools
python .\tools\verify_audio_ble_product_matrix.py --list-cases
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases A1 --fail-on-warning
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --realistic-usage-profile --random-seed 20260525
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases H3 --fail-on-warning
```

真实硬件命令前按资源加锁：矩阵通常同时占用 `COM3` 和 `BLE`，需要分别调用 `lock_resource.ps1 -Resource COM3` 与 `lock_resource.ps1 -Resource BLE`，用完分别释放。

## 验收证据

- 当前代码注册 `CASE_ORDER = ("A1", "A2", ..., "A10", "H1")`，并用 `FULL_CHAIN_CASE_ORDER = ("H3",)` 表示完整产品链路 case。
- `H3` 复用 Listener-Type 的 `run_ble_stream_smoke.ps1`，校验 `transcript`、`history_session.embeddedAudioStats` 与 `history_session.insertStatus == "inserted"`；如果 smoke 报告未及时带出 history，会按 `history.json` 中的 transcript/PCM 证据做后置查找，避免落盘时序假阴性。
- `MANUAL_OR_EXTERNAL_CASES` 中仅 `H2` 是外部环境占位。
- 矩阵入口、中文 TTS 音源和 capture 公共逻辑均在 `tools/` 下有代码入口。

## 关键不变量

- 已实现 case ID 以代码里的 `CASE_ORDER` / `CASE_RUNNERS` 为准。
- `--cases auto` 默认仍跑音频/传输矩阵；需要完整产品链路时显式跑 `--cases H3`，或使用 `--full-chain` 把 H3 加入 auto。
- 矩阵输出必须保留 compact summary 字段，供 AI/CI 判断通过、失败、warning 和 artifact 路径。
- 中文 TTS 优先使用 Windows `System.Speech`，不可用时才回退合成信号。
- H3 只做编排和验收，不重新实现 Listener-Type 的 BLE 流式/ASR/插入逻辑。

## 已知限制

- RF 干扰/距离、安全配对、多设备切换、功耗等仍需要外部测试环境。
- H3 需要同级 `Listener-Type` 仓库、可用 ASR 配置、真实设备在线，并按协作工作流加锁 `COM3` 和 `BLE`。
