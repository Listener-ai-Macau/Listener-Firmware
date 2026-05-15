# 量产稳定性优化计划

## 任务目标

把当前代码审阅中发现的量产风险整理成可执行 fix plan，目标是让 `BLE HID keyboard + voice capture upload` 链路从“能跑通”收敛到“可重复、可诊断、可量产验收”。

本计划聚焦软件稳定性、测试真实性和产品化细节。新原理图引脚和 SPH0645 适配细节另见 `!docs/fixes/schematic_v1_software_adaptation_plan.md`。

## 当前问题

当前链路已经大量复用官方/成熟能力，包括 ESP-IDF、NimBLE、`esp_hid`、I2S/I2C/GPIO driver、Windows BLE API。自定义部分主要集中在 BLE audio notify 协议、Windows host 重组脚本、KEY1 控制状态机和产品测试矩阵。

主要风险不是“官方能力不够”，而是自定义层还不够薄、不够强约束，测试也还需要更接近真实使用场景。

## 范围

- KEY1 按键链路稳定性。
- BLE 音频 packet sequence / expected packet count 边界。
- BLE notify 发送失败的设备端可观测性。
- 测试矩阵真实性和严谨度。
- 产品化占位信息清理。

## 不在本计划内

- 不重写 BLE audio session 协议整体设计。
- 不把 `audio_data` 退回旧的 `chunk + fragment` 模型。
- 不改变 host 订阅顺序 `CCCD notify -> ValueChanged`。
- 不删除 subscribe-before-connect 兼容路径。
- 不覆盖新原理图 SPH0645 适配计划。

## 官方优先 / 自定义边界

优先使用：

- FreeRTOS queue/task notification/critical section 处理跨任务按键事件。
- ESP-IDF GPIO driver 做按键输入和后续中断能力。
- NimBLE / ESP-IDF GATT notify 回调作为发送完成依据。
- Python 标准库和现有 Windows BLE host 脚本做测试证据生成。

保留自定义但要收窄：

- KEY1 产品语义保留为 start/stop toggle，但底层事件必须队列化和消抖。
- BLE audio notify 协议继续使用当前 packet/session 语义，但要显式限制 16-bit 计数边界。
- Windows host 重组脚本继续使用，但测试结果必须区分 `pass`、`warning`、`fail`、`skipped`、`blocked`，不能把未跑项目写成通过。

## P1 按键链路队列化和消抖

### 问题

`ports/esp32/voice_key_input/voice_key_input_esp32.c` 当前使用 `volatile uint32_t s_toggle_event_count` 作为跨任务事件计数，`voice_key_input_take_toggle_event()` 直接读写计数。这个模型没有 FreeRTOS 队列/临界区保护，也没有稳定消抖，量产上可能出现一次按键触发两次、快速按键漏触发或并发读写丢事件。

当前还存在 `GPIO0` direct fallback。`GPIO0` 是 BOOT/strapping 相关脚，不适合量产版用户按键 fallback。

### 预计改动

- 修改 `ports/esp32/voice_key_input/voice_key_input_esp32.c`。
- 使用 FreeRTOS queue 或 task notification 传递 KEY1 toggle 事件。
- 增加 30-50 ms 稳定消抖窗口，建议先用 20 ms polling + 40 ms stable threshold。
- 只启用原理图确认的一个 KEY1 来源。新 `Voice Keyboard V1.0` 原理图下应使用 `KEY-1=IO45` active-low。
- 删除 `GPIO0` fallback 和旧 XL9555/TCA9555 探测路径，或把旧路径隔离为明确的 legacy board 配置。
- 日志打印明确来源，例如 `voice key ready: source=gpio45 active_low debounce_ms=40`。

### 验收

- `powershell -ExecutionPolicy Bypass -File .\tools\test.ps1` 通过。
- `rg "GPIO_NUM_0|VOICE_KEY_INPUT_DIRECT_GPIO|s_toggle_event_count|XL9555|TCA9555" ports\esp32\voice_key_input` 不再命中量产路径。
- P11 真实 KEY1 测试至少连续 2 轮通过。
- 快速连按、长按、按住上电/复位都要记录结果。

### 人工检查点

- 确认 `IO45` 上电约束和 KEY1 外部上拉/按下电平不会影响启动。
- 如果硬件上 KEY1 按住上电有风险，必须回到原理图修改，而不是靠软件规避。

## P2 BLE 音频包序号边界

### 问题

当前 BLE 音频包序号和 `expected_packet_count` 是 `uint16_t`。安全上限 `AUDIO_CAPTURE_SESSION_SAFETY_MAX_SECONDS=600` 在大 MTU 下通常没问题，但如果协商 payload 变小，600 秒可能超过 65535 个 packet，导致序号或 stop expected count 溢出。

### 预计改动

- 修改 `ports/esp32/audio_capture/audio_capture_esp32.c`。
- 修改 `ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c`。
- 在 session begin 或首包发送前，根据 `ble_audio_stream_get_audio_payload_bytes()` 计算当前 payload 下最大安全录音时长。
- 如果仍使用 16-bit 协议，动态 cap `total_frames`，确保 `stream_next_packet_sequence + packet_count <= UINT16_MAX`。
- 如果产品明确需要更长录音，另开协议 V2，把 packet sequence / expected packet count 升级为 32-bit。
- stop/cancel 发送前，如果 expected packet count 已到边界，要发 `SESSION_ERROR` 或明确 fail，不允许静默 wrap。

### 验收

- 构建通过。
- 人工或单元级计算覆盖 payload 为 20、224、480 等边界时的最大安全时长。
- 长录音测试不会出现 `expected_packet_count` 回绕。
- host summary 中 packet count、duration、packet loss 一致。

## P3 BLE 发送失败可观测性

### 问题

`ble_audio_stream_task()` 内部发送失败目前主要留在设备日志里，session 状态机不知道真正 send 是否成功。`audio_capture` 侧打印的是 queued，不等于 delivered。量产测试里如果只看 host 收到的 WAV，有些边界问题可能难以定位是设备队列、NimBLE notify、host subscribe 还是链路环境导致。

### 预计改动

- 修改 `ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c`。
- 增加 per-session 发送统计：queued、sent、dropped、retry、last_error。
- 内部发送失败时记录 session error counter，并在 stop/cancel 或日志中输出结构化 marker。
- 必要时新增轻量回调或查询接口，让 `audio_capture` 能知道 session 最终发送状态。
- host 脚本解析设备日志 marker，把 `device_send_error_count`、`notify_retry_count`、`notify_drop_count` 写入 summary。

### 验收

- 构建通过。
- 正常 P1 捕获时设备端 send error 为 0。
- 人为断开 BLE 或关闭 notify 时，summary 能明确显示设备端发送失败原因。
- `pass` 不能只依赖 WAV 存在，必须同时检查设备端错误计数。

## P4 测试矩阵真实性和严格度

### 问题

测试脚本已经支持随机时长和 `--realistic-usage-profile`，方向正确。但 P2/P10 多轮场景目前偏向传输完整性，`require_analysis=False` 时不能充分证明每轮音频内容都真实有效。量产前还需要更严格的零丢包模式和更明确的失败判定。

### 预计改动

- 修改 `tools/verify_audio_ble_product_matrix.py`。
- 修改 `tools/ble_audio_regression_common.py`。
- 增加 `--strict-zero-loss`，打开后任何 packet loss 都直接 fail。
- 增加 `--require-multi-round-analysis` 或让 P2/P10 默认抽检/全检音频相关性。
- 每轮记录随机 seed、capture window、pre-start delay、inter-session gap、artifact path。
- 保持 realistic profile 下不每轮 reset、不每轮 host recover，避免测试变成“实验室理想场景”。
- 手工/外部项目 P11/P12/P13 必须只标记 `skipped`、`blocked` 或带证据的 `pass`。

### 验收

- `python .\tools\verify_audio_ble_product_matrix.py --port COM3 --realistic-usage-profile --strict-zero-loss --require-multi-round-analysis --random-seed <seed>` 能输出完整矩阵字段。
- P2/P10 每轮都有 `transport_result` 和 `analysis_result`。
- 失败时输出原因、artifact 路径、missing packet indices、recent serial log。
- 未真实执行的 case 不允许进入 pass 统计。

## P5 产品化 placeholder 清理

### 问题

当前 BLE device config 仍有 `Codex` manufacturer / placeholder battery level，`board_print_help()` 还有 demo 文案。这些不直接影响功能，但会影响量产识别、认证资料、用户日志和测试报告可信度。

### 预计改动

- 修改 `ports/esp32/ble_hid/ble_hid.c`。
- 修改 `components/board/board.c`。
- 统一 device name、manufacturer、serial number、VID/PID 策略。
- 固定 battery level `100` placeholder 改成真实电池模块输出，或在电池模块未完成前明确标记为 temporary 并从量产验收中 blocked。
- help 文案从 demo 改为产品 bring-up 指令，避免旧 `capture_audio_wav` 等 retired 路径误导测试。

### 验收

- BLE 设备属性显示符合产品命名。
- 串口 help 不再出现 `demo` 或 retired script 误导。
- 电池状态不再在量产 build 中固定为 100。

## P6 文档归档和关闭

### 问题

fix 文档只应该保留当前修复/调查。完成后，稳定结论需要进入 feature 文档，避免 `!docs/fixes/` 长期堆积完成项。

### 预计改动

- 更新 `!docs/features/audio_capture_ble_upload.md`，写入最终稳定性结论、测试口径和真实结果。
- 必要时更新 `tests/README.md`，说明量产前测试 artifact 规则。
- 当前计划全部完成后删除本文件。

### 验收

- feature 文档有最终结论和证据路径。
- `!docs/fixes/` 只保留仍在进行的 fix。

## 建议执行顺序

1. 先做 P1，按键是用户可见入口，且和新原理图 KEY1 适配强相关。
2. 再做 P2，防止长录音或小 MTU 下出现协议级隐患。
3. 然后做 P3，让 BLE 失败能被设备和 host 双侧定位。
4. 再做 P4，把测试矩阵变成更接近真实使用的量产前门禁。
5. 最后做 P5/P6，收口产品化细节和文档。

## 完成定义

- KEY1 不再依赖 `volatile` 计数和 `GPIO0` fallback。
- BLE audio session 不存在 16-bit packet count 静默溢出路径。
- 设备端 notify 失败能进入 host 测试 summary。
- P2/P10 能证明多轮音频内容有效，不只是 packet 数量接近。
- 产品标识、电池语义和 help 文案不再使用 demo/placeholder。
- 所有通过项都有真实命令输出、日志或 artifact，不靠口头判断。
