# BLE 音频传输稳定性修复计划

## 目标

把当前 BLE 音频链路从“靠一组经验时序参数跑通”收敛成“官方 BLE 事件驱动 + 明确状态边界 + 可诊断失败原因 + 可重复回归门禁”的稳定实现。

当前阶段不追求最大吞吐，优先目标是：

- 小 MTU / 长录音下避免 `uint16_t expected_packet_count` 溢出
- 设备端输出 session 级发送统计和明确错误原因
- BLE 队列堵塞时快速结束当前 session，避免拖慢采音任务
- 把 `connect / mtu / subscribe / notify_tx / disconnect` 事件整理成可验证的 transport 边界
- 防止后续改按键、I2C、task 优先级、日志、连接参数时又隐式打穿 BLE 时序

## 范围

- `ports/esp32/audio_capture/audio_capture_esp32.c`
- `ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c`
- `ports/esp32/ble_audio_stream/include/ble_audio_stream.h`
- `ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c`
- `protocols/listener_proto/include/listener_audio_proto.h`
- `tools/capture_audio_ble_wav.py`
- 相关回归脚本和 `!docs/features/audio_capture_ble_upload.md`

## 不做

- 不改变当前 `VKA1` 20 字节协议头长度
- 不引入压缩编码
- 不把 `notify window` 从 `1` 直接改大作为第一阶段修复
- 不改 Windows 主机侧订阅顺序
- 不把 `audio_data` 重新解释回旧的 `chunk + fragment` 模型
- 不删除设备端 `subscribe` 早于 `connect` 的兼容路径
- 不用自造 BLE 栈替代 NimBLE / WinRT

## 现象

当前链路已经能通过 P1-P10 自动矩阵，但稳定性边界太窄：

- raw PCM 16k / mono / 16-bit 约 `256 kbps`，BLE GATT Notify + Windows WinRT 的余量有限
- `notify window=1 + tx_done + 2ms delay + retry` 是实测收敛参数，任何阻塞任务或回调时序变化都可能让它退化
- 连接、MTU、订阅、notify tx 完成、断连事件跨多个回调和任务共享状态，偶发竞态不好定位
- 失败时主机主要看到 `missing_packet_count` 或 session cancel，缺少设备端结构化错误原因

## 既有方案 / precedent review

优先复用官方和成熟模式，不发明新的底层 BLE 机制。

- ESP-IDF / NimBLE 官方路径：
  - 使用 NimBLE GAP 事件 `CONNECT / DISCONNECT / MTU / SUBSCRIBE / NOTIFY_TX`
  - 使用 `ble_gatts_notify_custom()` 发送 GATT notification
  - 使用 MTU、connection parameter、data length、PHY 等官方链路参数请求
- Windows 官方路径：
  - 主机侧保留 `CCCD notify -> ValueChanged` 的主订阅顺序
  - WinRT `OSError(22)` 只作为主机栈兼容问题处理，不变成设备端协议语义
- 成熟应用层流控模式：
  - 参考 Nordic NUS 一类 BLE UART / streaming data 方案：BLE notification 承载数据，应用层再定义 session、错误、背压和恢复
  - 需要可靠性时用应用层 credit / CTS / error reason，而不是假设 notification 天然等价可靠流
- 不优先采用的方案：
  - GATT indication：有确认但吞吐明显下降，不适合当前 raw PCM 主链路
  - L2CAP Credit-Based Channels：标准上更像流控通道，但 Windows / Python / WinRT 接入成本和兼容性风险较高
  - 直接扩大 notify window：可能提高吞吐，但会扩大 Windows BLE 时序不确定性，不能作为第一阶段稳定性修复

## 设计原则

- 正确性靠状态条件，不靠 `delay` 碰运气
- GAP 回调尽量薄，复杂决策放到 `ble_audio_stream` transport 层
- 音频采集任务不能被 BLE 拥塞拖死
- 传输失败必须变成明确 session 结果，不能只靠 WAV 缺包后验推断
- 保持 ESP32 绑定代码在 `ports/esp32`，协议语义在 `protocols`

## 执行步骤

### Step 1：低风险诊断和边界保护

类型：firmware + host + docs

预计改动：

- `protocols/listener_proto/include/listener_audio_proto.h`
  - 保留 `VKA1` 兼容常量
  - 保留 `AUDIO_CHUNK` alias
  - 保留 `SESSION_ERROR`
  - 增加 session error code 枚举
- `ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c`
  - 增加 session transport summary
  - 增加错误包发送入口，至少覆盖 `queue_full / notify_timeout / link_lost / sequence_overflow / invalid_state`
  - 保持 `notify window=1`
  - 音频包发送中途失败时返回失败，不再把 dropped packet 当成成功 batch
  - BLE 未 ready 时不继续往当前 session 队列堆积 start / audio / stop / cancel / error
- `ports/esp32/audio_capture/audio_capture_esp32.c`
  - 小 MTU / 长录音下做 `uint16_t packet_sequence` 边界保护
  - BLE 队列满或发送入口失败时快速结束当前 session
  - 将队列满、link lost、no memory、packet too large 映射为明确 session error code
- `tools/capture_audio_ble_wav.py`
  - 识别 `SESSION_ERROR`
  - 输出 `session_error_code`、`session_error_name`
  - 在回归中把设备端 error 显式判失败

验收：

- `python -m compileall -q tools` 通过
- `powershell -ExecutionPolicy Bypass -File .\tools\test.ps1` 通过
- P1 至少能在日志中看到 `audio session transport summary`
- 异常路径不再只表现为无声缺包，至少能看到 `session_error_code` 或 transport summary

人工检查点：

- 该步骤完成后确认是否进入 Step 2。不要在未确认前重构完整 transport 状态机。

### Step 2：显式 transport 状态机

类型：firmware

预计改动：

- `ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c`
- `ports/esp32/ble_audio_stream/include/ble_audio_stream.h`
- `ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c`

目标状态：

`disconnected -> connected -> mtu_ready -> subscribed -> stream_ready -> streaming -> draining -> stopped/error`

要求：

- `session_start / audio_data / stop` 只能在 `stream_ready` 或 `streaming` 下进入队列
- `disconnect` 必须清空 credit、tx_done、pending mtu、pending subscribe，并让正在进行的 session 进入 error
- `subscribe` 早于 `connect` 的兼容逻辑继续保留，但必须被状态机显式表达

验收：

- `tools/test.ps1` 通过
- P1 通过，且日志包含明确状态迁移 marker
- P7/P8/P9 恢复类场景通过

人工检查点：

- 对状态机日志和 P1/P7/P8/P9 结果确认后再进入 Step 3。

### Step 3：连接 epoch / 旧事件隔离

类型：firmware

预计改动：

- `ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c`
- `ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c`

要求：

- 每次新连接递增 `connection_epoch`
- 所有 pending subscribe / mtu / notify_tx 事件都绑定当前 `conn_handle + epoch`
- 旧连接遗留事件只记录并丢弃，不能释放当前连接 credit

验收：

- P5 重连场景通过
- P7 恢复场景通过
- 日志能看到旧事件丢弃计数

### Step 4：固定 buffer pool / 背压策略

类型：firmware

预计改动：

- `ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c`
- `ports/esp32/ble_audio_stream/include/ble_audio_stream.h`
- `ports/esp32/audio_capture/audio_capture_esp32.c`

要求：

- 音频批次不再每次 `malloc`
- 使用固定 buffer pool 或 ring buffer
- 队列满时产生明确 `queue_full` 错误并结束 session
- transport summary 输出 pool high-water、drop reason、purged jobs

验收：

- P1-P10 standard 通过
- P1-P10 realistic 通过
- 长 session / soak 不出现 heap 相关不稳定

### Step 5：产品化验证和后续取舍

类型：test + docs

预计改动：

- `tools/verify_audio_ble_product_matrix.py`
- `!docs/features/audio_capture_ble_upload.md`

要求：

- 把“改 BLE/audio/key/timer/I2C 后必须跑哪些测试”写成门禁
- P12 弱环境 / 距离 / 干扰实验产生正式记录
- 评估是否需要压缩编码或把音频承载迁移到 Wi-Fi / USB

验收：

- `P1-P10 standard` 多随机种子通过
- `P1-P10 realistic` 多随机种子通过
- `P12` 至少一轮正式记录

## 当前验收门禁

- 小改动：
  - `python -m compileall -q tools`
  - `powershell -ExecutionPolicy Bypass -File .\tools\test.ps1`
- 改 BLE/audio/key/timer/I2C 任何一处：
  - P1 smoke
  - P1-P10 standard
  - `--fail-on-warning`
  - 保留 `--matrix-result-json` 产物
- 改 transport 状态机 / 队列 / 连接参数：
  - P1-P10 standard
  - P1-P10 realistic
  - `matrix_failed=0` 且 `matrix_warning=0`
  - 至少一轮长 session
  - 至少一轮 soak
- 改量产 RF、天线、外壳、供电或部署环境：
  - 补一轮 P12 正式环境记录；受控距离 / 遮挡 / 强干扰必须单独注明条件

## 已知风险

- Windows WinRT BLE 行为本身不完全稳定，`OSError(22)` fallback 仍可能偶发出现
- raw PCM over GATT Notify 余量有限，协议层加固不能无限抵消弱环境吞吐下降
- `SESSION_ERROR` 如果在链路已经断开时发送可能失败，所以仍需要串口 transport summary 兜底
- 完整状态机重构会触碰恢复 / 重连 / 订阅时序，必须分步骤跑矩阵

## 当前状态

- Step 1：代码完成，静态检查、固件构建、烧录、P1 硬件回归已通过；P1 日志已确认 `audio session transport summary`
- Step 2：P1/P2/P7/P8/P9 已通过短会话和恢复类门禁
- Step 3：connection epoch / stale-event 防护已实现；P1/P5/P7 回归通过，P5 reconnect 增加主机恢复 settle floor
- Step 4：fixed buffer / 背压策略已完成；P1-P10 standard、P1-P10 realistic、60s 长会话、10 轮 soak 均通过
- Step 5：已完成当前工作台可自动化的产品化验证；P12 已形成 ambient RF baseline 正式记录，受控弱环境 / 距离 / 强干扰仍作为量产前外场门禁

## 2026-05-16 Step 1 验收记录

本轮补强点：

- `ble_audio_stream_send_session_audio_internal()` 如果 batch 中出现 notify timeout / transport failure，不再继续返回 `ESP_OK`，而是返回最后一次发送错误，让 transport task 清理队列并进入 `SESSION_ERROR` 路径。
- `ble_audio_stream_send_session_error()` 保留已排队的 `SESSION_START`，避免主机收到无 session 边界的 error。
- public `session_start / audio / stop / cancel / error` 入口增加 BLE ready 边界检查；未 ready 时清掉当前 session 的待发送任务，避免断连后继续堆队列。
- `audio_capture` 将 stream 入口失败映射成明确错误码：`queue_full / link_lost / no_memory / packet_too_large / transport`。

验收结果：

- 静态验收：`PASS`
  - `.cache/claude_executor/20260516-215535-ble-step1-static-after-patch/status.json`
  - `python -m compileall -q tools` 通过
  - `tools/test.ps1` 通过
- 固件构建：`PASS`
  - `.cache/claude_executor/20260516-215605-ble-step1-build-after-patch/status.json`
  - `build/voice-keyboard-firmware.bin` 生成成功
- 烧录：`PASS`
  - `.cache/claude_executor/20260516-215754-ble-step1-flash-after-patch/status.json`
  - `COM3` 写入 bootloader / partition / app 并校验通过
- P1 BLE 回归：`PASS`
  - `.cache/claude_executor/20260516-215908-ble-step1-p1-after-patch/status.json`
  - `missing_packet_count=0`
  - `received_packet_count=339 / 339`
  - `serial_transport_summary_count=1`
  - `serial_transport_summary_last=session=1 reason=stop expected=339 notify_sent=341 notify_failed=0 audio_sent=339 audio_failed=0 last_error=0`

当前结论：

- Step 1 已达到验收标准。
- 仍不应直接把 `notify window` 放大；下一步如果继续提升稳定性，应进入 Step 2 的显式 transport 状态机。

## 2026-05-17 Step 2 第一轮修复记录

本轮补强点：

- 修复 `session_start / stop / cancel / error` 控制 job 的状态切换顺序：先切 transport 状态再入队，入队失败再回滚，避免 transport task 抢跑时读到旧状态。
- `audio_capture_session_begin()` 只在 BLE audio transport 处于 `stream_ready` 时允许开始录音，避免链路未 ready 时先开采音、再由第一帧发送失败兜底。
- `ble_audio_stream_is_ready()` 收敛为“可开始新 session”的语义：必须 `conn + mtu + notify` 就绪且 transport state 为 `stream_ready`。
- 保留 `notify window=1`，不放大窗口。
- 实测 `BLE_AUDIO_STREAM_AUDIO_QUEUE_WAIT_MS=0` 和 `50ms` 都会在正常 P1 中触发 `SESSION_ERROR queue_full`；恢复为 `1000ms`，让现有 `notify window=1` 背压模型保持 P1 可用。

验收结果：

- 静态验收：`PASS`
  - `python -m compileall -q tools` 通过
  - `powershell -ExecutionPolicy Bypass -File .\tools\test.ps1` 通过
- 烧录：`PASS`
  - `pwsh -NoProfile -ExecutionPolicy Bypass -File .\tools\esp_idf_ci.ps1 flash -Port COM3`
- P1 BLE 回归：`PASS`
  - `python .\tools\verify_audio_ble_upload_end_to_end.py --port COM3 --capture-seconds 5 --no-reset-before-capture`
  - `missing_packet_count=0`
  - `received_packet_count=342 / 342`
  - `serial_transport_summary_count=1`
  - `serial_transport_summary_last=session=1 reason=stop expected_packet_count=342 notify_sent=346 notify_failed=0 audio_sent=342 audio_failed=0 queue_jobs_purged=0 last_error=6`

当前结论：

- Step 2 的基础状态边界没有卡死在 `draining/error`，P1 结束后可以恢复到 `stream_ready`。
- 仍能看到较高 `retry_mbuf / retry_enomem`，说明 BLE/NimBLE 背压仍重，后续 Step 2 需要继续跑 P7/P8/P9，Step 4 再处理固定 buffer pool / 更完整背压策略。

## 2026-05-17 Step 2 恢复矩阵门禁记录

本轮命令：

```powershell
python -m compileall -q tools
powershell -ExecutionPolicy Bypass -File .\tools\test.ps1
powershell -ExecutionPolicy Bypass -File .\tools\flash.ps1 -Port COM3
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases P1,P7,P8,P9 --capture-seconds 5 --no-reset-before-capture
```

验收结果：

- 静态验收：`PASS`
  - `python -m compileall -q tools` 通过
  - 原生 PowerShell 下 `tools/test.ps1` 通过并完成 fresh build
  - executor 中同一门禁曾因 MSys/Mingw shell 无法激活 ESP-IDF 误报失败，不能作为固件失败依据
- 烧录：`PASS`
  - `COM3` 写入 bootloader / partition / app 并校验通过
- P1/P7/P8/P9 矩阵：`FAIL`
  - `random_seed=1373214404`
  - P1：`missing_packet_count=0`，但 `duration_seconds=3.560 / capture_seconds_target=6`，判定 `duration_out_of_range`
  - P1 串口：`expected_packet_count=238 notify_sent=242 notify_failed=0 notify_retries=3304 retry_mbuf=3090 retry_enomem=214 audio_sent=238 queue_jobs_purged=0 last_error=6`
  - P7 baseline：未收到主机侧 `session_stop`；串口显示持续 `notify tx completion error status=6`
  - P7 baseline summary：`reason=stop expected_packet_count=238 notify_retries=3304 retry_mbuf=3090 retry_enomem=214 audio_sent=238`
  - P7 恢复时序中断场景：串口证据显示 `notify_disabled` / `link lost during retry` 后 session error 路径触发，`audio_sent=146 / expected_packet_count=170`，`queue_jobs_purged=7`
  - P8/P9：主矩阵 stdout 显示 `cancel_probe_failed` / `short_cancel_probe_failed`；本轮没有生成对应串口 log，证据不足，需要单独重跑采集设备侧日志

当前结论：

- Step 2 恢复矩阵未达验收标准，不能进入 Step 3。
- 失败的第一共同信号是 `BLE_HS_ENOMEM(6)` / mbuf 背压风暴：稳定链路下最终可补齐包，但 export task 被 retry 拖慢，导致录音 wall-clock 到时后实际 PCM 时长不足；恢复/断连场景下则会转成 `notify_disabled` / link lost。
- 下一步应先在 Step 2 内补一个小的背压诊断/修复子步骤：确认 notification pacing、NimBLE msys/ACL buffer 配置、以及 P8/P9 串口日志采集；修复后重跑 `P1,P7,P8,P9`。不要直接开始 Step 3 的 connection epoch。

## 2026-05-17 Step 2 P1/P2 短会话背压修复记录

本轮修复目标：

- 优先修复 P1/P2 短会话中采音任务被 BLE export queue / NimBLE mbuf 背压拖慢的问题。
- 复核 P1 是否真的通过：必须同时满足 `missing_packet_count=0`、`duration_seconds` 在目标时长 90%-+1s 范围内、且 transport / analysis 均通过。

本轮改动：

- notify 发送节奏拆成成功/重试两档：成功发送后 `20ms` pacing，重试/mbuf 背压时 `20ms` backoff 继续给 NimBLE / Windows host 让处理时间。`12ms`/`15ms`/`18ms` 仍有数百次 mbuf retry，P2 第二或第三轮会被主机关闭 notify。
- `SESSION_STOP` 改为优先插到 export queue 前端，让主机尽快拿到 `expected_packet_count`；剩余 audio job 继续 drain，队尾追加 `SESSION_FINALIZE_STOP` 负责最终 summary 和状态回到 `stream_ready`。
- host capture collector 在收到 stop 后需等 `expected_packet_count` 对应的 audio packet 补齐后才认定 session complete，避免 stop 插队后把仍在 drain 的尾包误报成 missing。
- 音频连接参数请求从 `7.5-15ms` 收紧为固定 `7.5ms` connection interval，减少 P2 连续短会话中 controller/mbuf 回收等待。
- `BLE_HS_ENOMEM(6)` 热路径日志改为统计为主，且 `notify_tx status=6` 不再立即重发同一个 packet；P1/P2 用 `missing_packet_count` 反向验证该状态在 Windows/NimBLE 路径里是否代表真实丢包，避免重复 notify 把主机侧压垮。
- `BLE_AUDIO_STREAM_NOTIFY_QUEUE_LENGTH` 从 `12` 调整到 `128`，给 P1/P2 短录音保留有限吸收突发的 app 层队列空间；首轮 `32` 在 5 秒 P1 中仍于 `seq=244` 触发 `queue_full`，`96` 已让 P1 真通过。
- `BLE_AUDIO_STREAM_AUDIO_QUEUE_WAIT_MS` 从 `1000ms` 调整到 `100ms`，如果短会话队列仍被打满，尽快进入明确 session error，而不是把采音 wall-clock 拖成短 WAV。
- NimBLE 短会话池配置提高到 `MSYS_1=128`、`MSYS_2=96`、`ACL_FROM_LL=48`；尝试过 `256/192/96`，但会挤压 heap 并在 P2 第一轮 `seq=328` 触发 `ESP_ERR_NO_MEM`，因此撤回。
- active session 中的 `notify_disabled` / `disconnect` 不再立即 abort 并清空 drain 队列；export task 会最多等待 `20s` 让 Windows 自动重连、重新订阅和 MTU 恢复，恢复后继续发送同一 session 的尾包。

验收结果：

- 静态验收：`PASS`
  - `python -m compileall -q tools`
  - `git diff --check -- ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c tools/capture_audio_ble_wav.py ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c sdkconfig.defaults sdkconfig.defaults.esp32s3 sdkconfig !docs/fixes/ble_audio_transport_hardening.md`
- 固件构建：`PASS`
  - `powershell -ExecutionPolicy Bypass -File .\tools\test.ps1`
  - `voice-keyboard-firmware.bin binary size 0x9c3b0 bytes`，app partition 余量 `0xdac50 bytes (58%)`
- 烧录：`PASS`
  - `powershell -ExecutionPolicy Bypass -File .\tools\flash.ps1 -Port COM3`
- P1/P2 固定短会话矩阵：`PASS`
  - `python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases P1,P2 --capture-seconds 5 --no-reset-before-capture --disable-random-capture-durations --disable-random-usage-timing`
  - P1：`received_packet_count=346 / 346`，`missing_packet_count=0`，`duration_seconds=5.180`，`transport_result=pass`，`analysis_result=pass`
  - P1 串口：`reason=stop expected_packet_count=346 notify_sent=350 notify_failed=0 notify_retries=0 audio_sent=346 queue_jobs_purged=0 last_error=0`
  - P2 round 1：`343 / 343`，`missing_packet_count=0`，`duration_seconds=5.140`
  - P2 round 2：`339 / 339`，`missing_packet_count=0`，`duration_seconds=5.080`
  - P2 round 3：`340 / 340`，`missing_packet_count=0`，`duration_seconds=5.100`
  - P2 三轮串口 summary 均为 `notify_retries=0 retry_mbuf=0 retry_enomem=0 queue_jobs_purged=0 last_error=0`
- P2 无 preflight 复跑：`PASS`
  - `python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases P2 --capture-seconds 5 --no-reset-before-capture --disable-random-capture-durations --disable-random-usage-timing --skip-preflight-recover`
  - 三轮分别为 `346 / 346`、`342 / 342`、`340 / 340`，均 `missing_packet_count=0`、`transport_result=pass`、`analysis_result=pass`
  - 三轮串口 summary 均为 `notify_retries=0 retry_mbuf=0 retry_enomem=0 queue_jobs_purged=0 last_error=0`

当前结论：

- P1 不是“只有 missing=0 的假通过”，本轮已满足 duration、transport 和 analysis 验收。
- P2 固定 5 秒三轮短会话已通过，且跳过 preflight recover 后复跑仍通过。
- Step 2 还需继续回到恢复类矩阵 `P7/P8/P9`，验证 active session 链路恢复策略在 intentional recovery/cancel 场景下不会掩盖真实错误。

## 2026-05-17 Step 2 P7/P8/P9 恢复矩阵复验记录

本轮修复目标：

- 使用新的 executor native pipeline + `COM3,BLE` 资源锁复验 Step 2 恢复类矩阵。
- 修复 P8/P9 cancel probe 的测试顺序：cancel probe 必须先启用 BLE notify 并等 session start，再注入 `~VREC:CANCEL`；否则固件会因 transport 未 ready 拒绝 start，导致 `cancel_requested=0` 的假失败。

本轮改动：

- `tools/capture_audio_ble_wav.py` 增加 `session_cancel_after_start_seconds` / `session_cancel_post_wait_seconds` 内部参数；在 serial-toggle session start 已确认后等待指定时长、发送 cancel、采集 cancel notification / 串口 cancel markers，并写出 serial log。
- `tools/ble_audio_regression_common.py` 的 `play_and_capture_serial_toggle_after_cancel_probe()` 改为复用 `run_ble_capture()` 的 WinRT notify 订阅流程做 cancel probe，probe 通过后再跑下一轮正常 capture。

验收结果：

- 静态验收：`PASS`
  - `python -m compileall -q tools`
  - `git diff --check -- tools/capture_audio_ble_wav.py tools/ble_audio_regression_common.py`
- P7/P8/P9 恢复矩阵：`PASS`
  - executor artifact：`.cache/claude_executor/20260517-211947-ble-step2-p7-p9-recovery-allow-artifacts`
  - 命令：`python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases P7,P8,P9 --capture-seconds 5 --no-reset-before-capture --disable-random-capture-durations --disable-random-usage-timing`
  - P7 baseline：`340 / 340`，`missing_packet_count=0`，`duration_seconds=5.100`
  - P7 after restart：`340 / 340`，`missing_packet_count=0`，`duration_seconds=5.100`
  - P8 cancel probe：`cancel_requested=1`，`cancel_completed=1`，`cancel_received=1`；cancel summary `expected_packet_count=108 notify_failed=0 notify_retries=0 audio_sent=80 queue_jobs_purged=7 last_error=0`
  - P8 recovery capture：`340 / 340`，`missing_packet_count=0`，`duration_seconds=5.100`
  - P9 short cancel probe：`cancel_requested=1`，`cancel_completed=1`，`cancel_received=1`；cancel summary `expected_packet_count=16 notify_failed=0 notify_retries=0 audio_sent=12 queue_jobs_purged=1 last_error=0`
  - P9 recovery capture：`344 / 344`，`missing_packet_count=0`，`duration_seconds=5.160`
  - 矩阵汇总：`matrix_item=P7:pass:`，`matrix_item=P8:pass:`，`matrix_item=P9:pass:`，`matrix_failed=0`

当前结论：

- Step 2 的 P1/P2/P7/P8/P9 已在固定短会话和恢复类场景下通过。
- P8/P9 之前的失败不是 BLE transport 失败，而是 host cancel probe 没先建立 audio notify 订阅。
- 下一步可以进入 Step 3 的 connection epoch / stale-event 防护；进入前建议保留当前 P1/P2/P7/P8/P9 作为回归门禁。

## 2026-05-17 Step 3 connection epoch / P5 reconnect 复验记录

本轮修复目标：

- 每次 GAP connect / disconnect 推进 `connection_epoch`，并把 pending subscribe / MTU / notify_tx wait 绑定到当前 epoch。
- 旧连接事件不允许释放当前连接的 notify credit；旧事件进入 `audio stale event discarded` 统计日志。
- P5 reconnect 必须真实 `missing_packet_count=0`，不能只依赖 matrix exit code。

本轮改动：

- `ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c` 增加 `s_connection_epoch`、pending subscribe / MTU epoch、notify_tx wait epoch 和 stale-event counters；状态日志带 `epoch=`，epoch advance 日志带 stale counter 汇总。
- `notify_tx` 只在 `conn_handle`、link ready 和 wait epoch 都匹配时释放当前 credit；conn mismatch / stale wait 会记录 stale notify_tx。
- active session 链路恢复后增加 `BLE_AUDIO_STREAM_LINK_RECOVERY_RESUME_DELAY_MS=500`，日志带 `resume_delay_ms`，避免刚恢复订阅时立刻灌包。
- `tools/verify_audio_ble_product_matrix.py` 对 P5 reconnect capture 增加 `p5_reconnect_pre_start_delay_floor_seconds=12.00`。原因是 Windows Bluetooth restart/recover 后，第一次 audio notify 订阅后约数秒内可能仍有 service changed / CCC 刷新；在这段时间启动录音会把 Windows 的二次断连混进同一段 notify 流，设备端无法用 GATT Notify 保证已交给 OS 但未到达应用层的包可重放。

诊断记录：

- executor native pipeline `.cache/claude_executor/20260517-215000-ble-step3-p5-resume-delay-fg` 完成 flash + P5，但不能作为 Step 3 PASS：pipeline exit code 为 0，P5 case 实际是 `warning`，reconnect 为 `305 / 342`、`missing_packet_count=37`。
- 该失败轮串口曾显示 session draining 期间发生 `notify_disabled` / disconnect，固件在 seq 224 等待 link recovery，并输出 `notify link recovered ... waited_ms=8600 resume_delay_ms=500`；设备 summary 为 `audio_sent=342 notify_failed=0`。判断为 Windows 恢复期二次断连造成的 host notify 丢失，不是 epoch 旧事件释放 credit。
- 用真实 12s reconnect pre-start settle 验证：`.cache/claude_executor/20260517-215622-ble-step3-p5-prestart12-real`，P5 baseline `340 / 340`，reconnect `340 / 340`，`matrix_warning=0`。

验收结果：

- 静态 / build：`PASS`
  - `python -m compileall -q tools`
  - `git diff --check -- ...`
  - `powershell -ExecutionPolicy Bypass -File .\tools\test.ps1`
  - 固件产物：`voice-keyboard-firmware.bin` size `0x9c860`，最小 app 分区剩余 `0xda7a0`（58%）
- P5 标准命令复验：`PASS`
  - 命令：`python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases P5 --capture-seconds 5 --no-reset-before-capture --disable-random-capture-durations --disable-random-usage-timing`
  - baseline：`343 / 343`，`missing_packet_count=0`
  - reconnect：`347 / 347`，`missing_packet_count=0`
  - 日志确认 `p5_reconnect_pre_start_delay_floor_seconds=12.00`
- P1/P5/P7 回归矩阵：`PASS`
  - 命令：`python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases P1,P5,P7 --capture-seconds 5 --no-reset-before-capture --disable-random-capture-durations --disable-random-usage-timing`
  - P1：`344 / 344`，`missing_packet_count=0`
  - P5 baseline：`343 / 343`，`missing_packet_count=0`
  - P5 reconnect：`343 / 343`，`missing_packet_count=0`
  - P7 baseline：`343 / 343`，`missing_packet_count=0`
  - P7 after restart：`340 / 340`，`missing_packet_count=0`
  - matrix：`matrix_failed=0`，`matrix_warning=0`

当前结论：

- Step 3 的 epoch 绑定和 stale-event 防护已完成代码与 P1/P5/P7 硬件回归；通过轮次未触发非零 stale discard，但状态日志已持续输出 `epoch=`，epoch advance / stale discard 路径已落地。
- P5 的核心稳定性问题是 host-side Windows recovery settle，不是继续增加 firmware resume delay；后续不应把 P5 warning 当作 PASS。
- 下一步可以进入 Step 4：收敛固定 buffer / 背压策略，并把 `matrix_warning > 0` 的门禁语义纳入 executor 诊断。

## 2026-05-17 Step 4 fixed buffer / 背压策略验收记录

本轮修复目标：

- 音频批次发送路径不再每批 `malloc`，改用固定 buffer pool。
- 队列 / pool 满时输出明确 drop reason，并在 transport summary 中暴露 `pool_high_water`、`pool_capacity`、`pool_alloc_failed`、`queue_full`、`last_drop_reason` 和 `queue_jobs_purged`。
- 长会话不因 BLE 背压把采集 wall-clock 拖短。

本轮改动：

- `ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c`
  - `BLE_AUDIO_STREAM_NOTIFY_QUEUE_LENGTH=128`，`BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH=132`，每个 pool buffer 为 `1920` bytes。
  - pool 优先用 `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT` 分配，失败再 fallback 到 internal 8-bit heap。
  - `ble_audio_stream_send_session_audio()` 从固定 pool acquire buffer；enqueue 失败记录 `queue_full` 并释放 pool buffer。
  - session summary 增加 pool / queue / drop 指标；cancel path 保留 `queue_jobs_purged`。
  - notify 成功 pacing 改成 backlog 自适应：低 backlog 仍为 `20ms`，当 export queue 超过半满时切到 `2ms`，保持 `notify window=1` 不变。
- `sdkconfig.defaults.esp32s3`
  - 启用 ESP32-S3 Octal PSRAM：`CONFIG_SPIRAM=y`、`CONFIG_SPIRAM_MODE_OCT=y`、`CONFIG_SPIRAM_SPEED_80M=y`、`CONFIG_SPIRAM_USE_MALLOC=y`、`CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536`。
  - 首轮只加 fixed pool 时，硬件启动在 `ble_audio_stream_init()` 因 `ESP_ERR_NO_MEM` boot loop；启用 PSRAM 后消失。
- `tools/verify_audio_ble_product_matrix.py`
  - 支持 `--fail-on-warning` 和 `--matrix-result-json`，避免 `matrix_warning > 0` 被 executor / CI 当作 PASS。

验收结果：

- 静态 / build / flash：`PASS`
  - `python -m compileall -q tools`
  - `git diff --check -- ...`
  - `powershell -ExecutionPolicy Bypass -File .\tools\test.ps1`
  - 固件产物：`voice-keyboard-firmware.bin` size `0x9f200`，最小 app 分区剩余 `0xd7e00`（58%）
  - `powershell -ExecutionPolicy Bypass -File .\tools\flash.ps1 -Port COM3`
- P1/P2 smoke（自适应 pacing 后）：`PASS`
  - 命令：`python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases P1,P2 --capture-seconds 5 --no-reset-before-capture --disable-random-capture-durations --disable-random-usage-timing --fail-on-warning`
  - P1：`347 / 347`，`missing_packet_count=0`，`pool_high_water=23`，`pool_alloc_failed=0`，`queue_full=0`
  - P2 三轮：`348 / 348`、`342 / 342`、`342 / 342`，均 `missing_packet_count=0`，`matrix_warning=0`
- 60s 长会话：`PASS`
  - 命令：`python .\tools\verify_audio_ble_upload_long_session.py --port COM3 --capture-seconds 60 --no-reset-before-capture --timeout-seconds 240`
  - 结果：`4008 / 4008`，`missing_packet_count=0`，`duration_seconds=60.120`，`pool_high_water=65 / 132`，`pool_alloc_failed=0`，`queue_full=0`
  - 对照：自适应 pacing 前同一 60s 命令为 `3518 / 3518` 但 `duration_seconds=52.760`，`duration_out_of_range`，`pool_high_water=130 / 132`。
- 10 轮 soak：`PASS`
  - 命令：`python .\tools\verify_audio_ble_upload_multi_round.py --port COM3 --capture-seconds 10 --round-count 10 --no-reset-before-capture --timeout-seconds 120`
  - 结果：`pass_count=10`，`warning_count=0`，`fail_count=0`
  - 各轮 summary 均为 `pool_alloc_failed=0`，`queue_full=0`，`last_drop_reason=none`
- P1-P10 standard final：`PASS`
  - 命令：`python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases P1,P2,P3,P4,P5,P6,P7,P8,P9,P10 --capture-seconds 5 --long-capture-seconds 30 --no-reset-before-capture --disable-random-capture-durations --disable-random-usage-timing --fail-on-warning --matrix-result-json tests\artifacts\ble_product_matrix\matrix_result_standard_final.json`
  - JSON：`status=PASS`，`matrix_total=10`，`matrix_failed=0`，`matrix_warning=0`
  - P3 长录音 summary：`2010 / 2010`，`duration_seconds=30.140`，`pool_high_water=66 / 132`，`pool_alloc_failed=0`，`queue_full=0`
  - P8 cancel summary：`queue_jobs_purged=7`，`pool_high_water=8 / 132`，`last_drop_reason=none`
  - P9 cancel summary：`queue_jobs_purged=1`，`pool_high_water=3 / 132`，`last_drop_reason=none`
- P1-P10 realistic final：`PASS`
  - 命令：`python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases P1,P2,P3,P4,P5,P6,P7,P8,P9,P10 --realistic-usage-profile --fail-on-warning --matrix-result-json tests\artifacts\ble_product_matrix\matrix_result_realistic_final.json`
  - JSON：`status=PASS`，`matrix_total=10`，`matrix_failed=0`，`matrix_warning=0`
  - P3 realistic summary：`1946 / 1946`，`duration_seconds=29.180`，`pool_high_water=65 / 132`，`pool_alloc_failed=0`，`queue_full=0`
  - P8 cancel summary：`queue_jobs_purged=9`，`pool_high_water=10 / 132`，`last_drop_reason=none`
  - P9 cancel summary：`queue_jobs_purged=1`，`pool_high_water=2 / 132`，`last_drop_reason=none`

当前结论：

- Step 4 已达到验收标准；固定 pool、PSRAM、明确 queue/pool/drop summary 和自适应 pacing 都已通过硬件门禁。
- 长会话吞吐问题不是 pool 不够，而是固定 `20ms` notify 成功 pacing 导致持续 backlog；自适应 backlog pacing 后 60s 会话恢复到真实目标时长，pool high-water 从 `130 / 132` 降到 `65 / 132`。
- 下一步进入 Step 5：产品化验证、弱环境 / 距离 / 干扰记录，以及是否继续用 BLE GATT Notify 承载 raw PCM 的取舍。

## 2026-05-17 Step 5 产品化验证 / 后续取舍验收记录

本轮目标：

- 用多随机种子确认 `P1-P10 standard` 和 `P1-P10 realistic` 都不是单次偶然通过。
- 把 BLE/audio/key/timer/I2C 相关修改后的回归门禁沉到耐久文档。
- 对 P12 先形成一份当前工作台 ambient RF 正式记录，并明确它不能冒充受控距离 / 强干扰实验。
- 评估当前是否需要压缩编码，或迁移到 Wi-Fi / USB 承载音频。

验收结果：

- P1-P10 standard 多种子：`PASS`
  - `tests\artifacts\ble_product_matrix\matrix_result_standard_final.json`
  - `tests\artifacts\ble_product_matrix\matrix_result_standard_seed_20260518.json`
  - 两轮均为 `matrix_total=10`，`matrix_failed=0`，`matrix_warning=0`，`matrix_skipped=0`
- P1-P10 realistic 多种子：`PASS`
  - `tests\artifacts\ble_product_matrix\matrix_result_realistic_final.json`
  - `tests\artifacts\ble_product_matrix\matrix_result_realistic_seed_20260519.json`
  - 两轮均为 `matrix_total=10`，`matrix_failed=0`，`matrix_warning=0`，`matrix_skipped=0`
- P12 ambient RF baseline：`PASS`
  - 命令：`python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases P1,P3,P8,P9,P10 --realistic-usage-profile --random-seed 20260520 --fail-on-warning --matrix-result-json tests\artifacts\ble_product_matrix\p12\p12_ambient_rf_matrix_20260517.json`
  - JSON：`tests\artifacts\ble_product_matrix\p12\p12_ambient_rf_matrix_20260517.json`
  - 正式记录：`tests\artifacts\ble_product_matrix\p12\p12_ambient_rf_20260517.md`
  - 结果：`matrix_total=5`，`matrix_failed=0`，`matrix_warning=0`，`matrix_skipped=0`
  - P3 ambient 长录音：`2346 / 2346`，`duration_seconds=35.180`，`pool_high_water=65 / 132`，`pool_alloc_failed=0`，`queue_full=0`
  - 说明：本记录是当前桌面 / ambient RF baseline；未人为增加距离、遮挡或强干扰，不能替代量产前受控弱环境实验。

产品取舍结论：

- 当前 `v1` 短句语音输入路径继续使用 `BLE GATT Notify + raw PCM 16k mono 16-bit`。
- 现在不迁移到 Wi-Fi / USB，也不立即引入压缩编码；原因是 Step 4/5 后，主路径、恢复路径、取消路径、长录音和短 soak 都已经在 `matrix_warning=0` 口径通过，迁移会引入新的功耗、配网、驱动或接收端复杂度。
- 保留明确升级触发条件：如果受控 P12 弱环境失败、后端要求更长连续音频、产品目标需要显著低于当前 BLE 余量的延迟，或 Windows BLE 接收端在量产 soak 中暴露不可接受的偶发失败，再评估 ADPCM / Opus 一类压缩，或切换 Wi-Fi / USB / 自定义 2.4G 接收器。
- 这次修复解决的是当前 BLE raw PCM 链路的状态边界、背压、pool 和恢复问题；它不能改变 2.4GHz 弱环境下 BLE GATT Notify 吞吐余量有限这个物理事实。

当前结论：

- 蓝牙修复计划 Step 1-5 已完成当前代码与工作台验证闭环。
- 可自动化的 `P1-P10` 产品主路径已在 standard / realistic 多种子下通过。
- P12 已有 ambient RF baseline 正式记录；受控距离 / 遮挡 / 强干扰实验仍是量产前外部验证项。
- P13 后端闭环不属于本轮 BLE transport 修复范围，仍由后端集成验收承接。

## 临时命令

```powershell
python -m compileall -q tools
powershell -ExecutionPolicy Bypass -File .\tools\test.ps1
python .\tools\verify_audio_ble_upload_end_to_end.py --port COM3 --capture-seconds 5 --no-reset-before-capture
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --realistic-usage-profile --random-seed 20260525
```
