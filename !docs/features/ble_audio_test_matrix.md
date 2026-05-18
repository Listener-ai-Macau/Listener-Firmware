# BLE 音频产品测试矩阵

## 状态

- status: `implemented`
- scope: BLE 音频产品使用面回归矩阵，`A` 为自动 case，`H1` 为半自动物理按键 case，`H2/H3` 为外部占位
- source_of_truth: `tools/verify_audio_ble_product_matrix.py` 中的 `CASE_ORDER` / `CASE_RUNNERS` / `MANUAL_OR_EXTERNAL_CASES`

## 当前能力

矩阵脚本以中文 TTS 或回退合成音源驱动 BLE 音频链路，覆盖单次录音、多轮连续录音、断连恢复、多轮重连、空闲恢复、取消恢复、静音负向/快速 toggle、采集中并发客户端和物理 KEY1 半自动验证。

## 代码入口

- `tools/verify_audio_ble_product_matrix.py`
- `tools/ble_audio_regression_common.py`
- `tools/capture_audio_ble_wav.py`

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
| H3 | 外部 | 后端 ASR 集成，需外部端点 |

## 验收命令

```powershell
python -m compileall -q tools
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases A1 --fail-on-warning
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --realistic-usage-profile --random-seed 20260525
```

真实硬件命令前按资源加锁：矩阵通常同时占用 `COM3` 和 `BLE`，需要分别调用 `lock_resource.ps1 -Resource COM3` 与 `lock_resource.ps1 -Resource BLE`，用完分别释放。

## 验收证据

- 当前代码注册 `CASE_ORDER = ("A1", "A2", ..., "A10", "H1")`。
- `MANUAL_OR_EXTERNAL_CASES` 中 `H2/H3` 明确是外部占位，不计作自动完成。
- 矩阵入口、中文 TTS 音源和 capture 公共逻辑均在 `tools/` 下有代码入口。

## 关键不变量

- 已实现 case ID 以代码里的 `CASE_ORDER` / `CASE_RUNNERS` 为准。
- `H2/H3` 没有代码级自动验收前，只能作为外部占位，不写成 completed 自动能力。
- 矩阵输出必须保留 compact summary 字段，供 AI/CI 判断通过、失败、warning 和 artifact 路径。
- 中文 TTS 优先使用 Windows `System.Speech`，不可用时才回退合成信号。

## 已知限制

- RF 干扰/距离、安全配对、多设备切换、功耗等仍需要外部测试环境。
- 后端 ASR 集成不是 firmware 矩阵的自动完成项。
