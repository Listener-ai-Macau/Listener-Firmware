# BLE 音频产品测试矩阵

## 状态

- status: `implemented`
- scope: BLE 音频产品使用面回归矩阵，`A1-A11` 为默认自动 BLE 音频/传输 case 并叠加产品链路 overlay，`A12` 为显式混合长测，`H1` 为显式手动 runner 内产品链路 case，`H2` 为外部环境占位
- source_of_truth: `tools/verify_audio_ble_product_matrix.py` 中的 `CASE_ORDER` / `EXTENDED_AUTO_CASES` / `MANUAL_CASES` / `PRODUCT_CHAIN_OVERLAY_CASES` / `PRODUCT_CHAIN_IN_RUNNER_CASES` / `CASE_RUNNERS` / `MANUAL_OR_EXTERNAL_CASES`

## 当前能力

矩阵脚本以中文 TTS 或回退合成音源驱动 BLE 音频链路，按基础性排序覆盖短录音、长录音、多轮随机短录音、断连恢复、多轮重连、主机侧恢复、空闲恢复、取消恢复、静音负向、快速 toggle、采集中并发客户端、混合长测和物理 KEY1 半自动验证。默认验收口径是完整产品链路：每个默认 A case 先跑对应 BLE 场景，再叠加 Listener-Type 原生 BLE 流式 smoke，把真实 BLE 音频送入 ASR，并验证 transcript、历史和当前光标插入状态。

`A1-A11` 属于 `PRODUCT_CHAIN_OVERLAY_CASES`，也是 `--cases auto` 的默认自动矩阵。默认执行策略是逐 case gate：一个 case fail，或 `--fail-on-warning` 下出现 warning，就停在当前位置先修，只有显式加 `--continue-on-failure` 才继续收集后续失败。`A12` 属于 `EXTENDED_AUTO_CASES` 和 `PRODUCT_CHAIN_IN_RUNNER_CASES`，只能显式指定运行，用于较长的混合使用 soak。`H1` 属于 `MANUAL_CASES` 和 `PRODUCT_CHAIN_IN_RUNNER_CASES`，只能显式指定运行以保留人工按键语义。需要旧式纯传输调试时，用 `--transport-only` 关闭 A 系列产品链路 overlay。

## 代码入口

- `tools/verify_audio_ble_product_matrix.py`
- `tools/ble_audio_regression_common.py`
- `tools/capture_audio_ble_wav.py`
- `..\Listener-Type\tools\embedded_audio_replay\run_ble_stream_smoke.ps1`

## 已实现 case

| ID | 类型 | 场景 |
|---|---|---|
| A1 | 自动产品链路 | 单轮短录音 + ASR 文本输出 |
| A2 | 自动产品链路 | 单轮多句随机长录音 + ASR 文本输出 |
| A3 | 自动产品链路 | 同连接多轮随机短录音 + ASR 文本输出 |
| A4 | 自动产品链路 | 断连恢复 + ASR 文本输出 |
| A5 | 自动产品链路 | 多轮独立重连 + ASR 文本输出 |
| A6 | 自动产品链路 | 主机侧恢复/无蓝牙重启 + ASR 文本输出 |
| A7 | 自动产品链路 | 空闲后首录 + ASR 文本输出 |
| A8 | 自动产品链路 | 取消后不插字 + 恢复 ASR 文本输出 |
| A9 | 自动产品链路 | 静音/无输入不插字 + 恢复 ASR 文本输出 |
| A10 | 自动产品链路 | 快速启停压力 + ASR 文本输出 |
| A11 | 自动产品链路 | 采集中并发 BLE 客户端 + ASR 文本输出 |
| A12 | 显式自动长测 | 多轮混合使用 soak + ASR 文本输出 |
| H1 | 手动产品链路 | 物理 KEY1 手动触发 + ASR 文本输出 |
| H2 | 外部/手动占位 | RF 干扰/距离，需特殊环境 |

## 验收命令

```powershell
python -m compileall -q tools
python .\tools\verify_audio_ble_product_matrix.py --list-cases
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases A1 --fail-on-warning
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases A1 --transport-only --fail-on-warning
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --realistic-usage-profile --random-seed 20260525
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases A12 --soak-round-count 6 --fail-on-warning
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases H1 --fail-on-warning
```

真实硬件命令前按资源加锁：矩阵通常同时占用 `COM3` 和 `BLE`，需要分别调用 `lock_resource.ps1 -Resource COM3` 与 `lock_resource.ps1 -Resource BLE`，用完分别释放。

## 验收证据

- 当前代码注册 `CASE_ORDER = ("A1", "A2", ..., "A11")` 作为默认自动矩阵；`EXTENDED_AUTO_CASES = ("A12",)` 和 `MANUAL_CASES = ("H1",)` 只能显式指定运行；其中 `PRODUCT_CHAIN_OVERLAY_CASES` 覆盖 A1-A11，`PRODUCT_CHAIN_IN_RUNNER_CASES` 覆盖 A12/H1。
- A 系列默认先跑原 BLE 传输/音频场景，再叠加 Listener-Type smoke，矩阵结果 JSON 在每个 case 的 `details.product_chain` 中记录 transcript、history、embeddedAudioStats、insertStatus 和 artifact 路径。
- A1 使用 `short_capture_window` 做最基础短录音；A2 使用 `long_capture_window` 和多句随机 TTS 做长录音，BLE/WAV 阶段和 product-chain 阶段都不能用单句循环撑长；A3 使用随机多轮短录音和随机轮间间隔。
- 矩阵 TTS 和 Listener-Type product-chain TTS 默认使用同一 4.0 PCM gain，避免同一回归中播放音量忽大忽小。
- A8 额外运行 cancel 负向产品链路，断言取消后的音频不会产出 transcript/history 文本，也不会插入当前光标；随后仍由 overlay 验证恢复输出。
- A9 额外运行静音负向产品链路，断言无输入不会产出 transcript/history 文本，也不会插入当前光标；随后仍由 overlay 验证恢复输出。
- A12 显式运行多轮 Listener-Type product-chain soak，默认每轮不 reset 设备，用 `--soak-round-count` 和 `--soak-idle-seconds` 控制强度。
- `--transport-only` 会跳过 A 系列产品链路 overlay，仅保留 BLE 传输/音频调试语义。
- H1 使用 Listener-Type smoke 的 `manual-key` 触发模式，保留物理 KEY1 手动触发语义，同时验证 ASR 和文本输出。
- Listener-Type smoke 校验 `transcript`、`history_session.embeddedAudioStats` 与 `history_session.insertStatus == "inserted"`；如果 smoke 报告未及时带出 history，矩阵会按 `history.json` 中的 transcript/PCM 证据做后置查找，避免落盘时序假阴性。
- `MANUAL_OR_EXTERNAL_CASES` 中仅 `H2` 是外部环境占位。

## 关键不变量

- 已实现 case ID 以代码里的 `CASE_ORDER` / `EXTENDED_AUTO_CASES` / `CASE_RUNNERS` / `MANUAL_OR_EXTERNAL_CASES` 为准。
- `--cases auto` 默认只跑 A1-A11 自动产品链路矩阵；A12/H 系列必须显式指定；需要旧式纯 BLE 传输/音频调试时显式加 `--transport-only`。
- 默认逐 case gate，失败即停；需要一次性收集全矩阵失败时显式加 `--continue-on-failure`。
- `--full-chain` 是兼容 no-op；当前产品链路验证已成为 A/H case 的默认语义。
- 矩阵输出必须保留 compact summary 字段，供 AI/CI 判断通过、失败、warning 和 artifact 路径。
- 中文 TTS 优先使用 Windows `System.Speech`，不可用时才回退合成信号。
- 矩阵只做编排和验收，不重新实现 Listener-Type 的 BLE 流式/ASR/插入逻辑。

## 已知限制

- RF 干扰/距离、安全配对、多设备切换、功耗、App 生命周期/托盘启动等仍需要外部测试环境或后续独立 case。
- 产品链路验收需要同级 `Listener-Type` 仓库、可用 ASR 配置、真实设备在线、当前光标可接收文本，并按协作工作流加锁 `COM3` 和 `BLE`。
