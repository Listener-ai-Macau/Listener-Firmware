# 设备端音频采集、录音控制与 BLE 上传链路

## 状态

- 状态：`可用`
- 定位：`设备端语音输入主链路的前置验证能力`
- 阶段结论：`已收敛到连续 notify 会话流模型，可作为后续 Windows 常驻接收端 / ASR 接入的稳定基线`

## 链路

`EC11_KEY / 串口 toggle -> voice_recording_control -> audio_capture -> ble_audio_stream -> WinRT GATT client -> session 重组 -> wav`

- 用户语义：`EC11_KEY` 按一下开始录音，再按一下结束录音
- 自动化验证：`~VREC:TOGGLE` / `~VREC:CANCEL`
- 采音路径：`SPH0645 I2S digital mic -> ESP32-S3`

## 音频契约

- `16kHz` / `mono` / `16-bit` / `PCM little-endian`
- 基础帧：`20ms` = `320` 样本 = `640 bytes`

## 会话协议

`session_start` -> 连续 `audio_data` -> `session_stop`（或 `session_cancel`）

- `audio_data` 代表连续音频流中的一个顺序包（`packet_sequence`）
- 不再是旧的 "chunk + fragment" 模型
- wire layout 保留旧字段名兼容，但语义已改：`chunk_index` = `packet_sequence`，`fragment_index` 固定 0，`fragment_count` 固定 1

## 官方 / 内建 / 自定义边界

### 官方 / 内建 / 已证明路径

- 设备侧复用：
  - `ESP-IDF + FreeRTOS + CMake`
  - `NimBLE` 官方主机栈
  - 标准 `BLE GATT notify`
  - 标准 bond / 加密 / `NVS`
- 主机侧复用：
  - `Windows WinRT GATT`
  - `GattSession.MaintainConnection`
  - 标准 `CCCD notify`
  - 标准 `ValueChanged`
  - `wave` / `pyserial` 这些成熟库
- 当前原则：
  - 优先吃官方栈和标准 API
  - 只有在产品语义、时序兼容、可测性不够时才加薄的自定义层

### 当前保留的薄自定义层

- `listener_audio_proto` 会话协议：
  - 用来表达 `session_start -> audio_data -> session_stop / cancel`
  - 这是产品语义，不是替代底层 BLE
- 设备端兼容层：
  - `subscribe 早于 connect` 的暂存恢复
  - `mtu update 早于 connect` 的 deferred 恢复
- 主机端兼容层：
  - `WinRT OSError(22)` 时的 warm-up / rebuild fallback
  - 更长的 `notify ready` 等待预算
- 测试层：
  - 随机时长 + 随机使用节奏矩阵
  - `P8/P9` 的“同一串口会话里 cancel 后继续录音”真实恢复验证

### 当前明确避免的重自定义

- 没有自造 BLE 协议栈
- 没有自造 Windows 蓝牙驱动
- 没有回退到自定义 USB 音频主链路
- 没有把主机端订阅顺序改成偏离 WinRT 现实约束的自定义顺序

## 主机端重组

- 以 `session_id` 为主键，按 `packet_sequence` 顺序重组
- 缺失包补静音，优先产出完整时长 `wav`
- 统计字段：`expected_packet_count` / `received_packet_count` / `missing_packet_count` / `received_pcm_bytes` / `duration_seconds`
- 关键串口观测字段：`serial_stream_start_count` / `serial_stream_audio_count` / `serial_stream_stop_count` / `serial_upload_begin_count` / `serial_upload_end_count` / `serial_upload_skipped_count`

### 回归三档结果

- `pass`：wav 生成、时长正常、丢包 <= 2%、音频分析通过（`best_corr >= 0.20`、`peak >= 450`、`active_frame_count >= 5`）
- `warning`：wav 生成、丢包 > 2% 且 <= 12%
- `fail`：无有效音频 / 无 wav / 时长异常 / 丢包 > 12% / 分析失败

## 当前收敛参数

- 设备端：
  - `AUDIO_CAPTURE_STREAM_BATCH_FRAMES = 3`
  - 音频连接参数 `supervision_timeout = 800`
  - `MTU 517` 时目标 `value_max=500 / payload_max=480`
- 主机端：
  - `BLE_NOTIFY_CCCD_TIMEOUT_SECONDS = 5`
  - `BLE_NOTIFY_PRE_CCCD_SETTLE_SECONDS = 0.35`
  - `notify_ready_timeout_seconds = 45`
  - `NOTIFY_READY_SETTLE_SECONDS = 1.2`

这些值不是为了“调到勉强能过”，而是当前实测下在吞吐、恢复和 WinRT 时序之间更稳定的收敛点。

## 稳定性修复归档

已关闭的 BLE 音频产品矩阵稳定性修复计划，其耐久结论归档到这里。

- 自动矩阵收敛目标已完成：
  - `P1-P10` 在 `standard` 口径下多随机种子通过
  - `P1-P10` 在 `realistic` 口径下多随机种子通过
  - 当前自动矩阵结论为 `matrix_failed=0`、`matrix_warning=0`
- 设备端发送策略：
  - 没有完全回退旧窗口模型
  - 保留 `tx done` 路径，但把批量从 `4` 帧收敛到 `3` 帧
  - 继续复用 `NimBLE` 官方 notify 能力，不自造 BLE 栈
- 恢复 / 重连兼容：
  - 保留 `subscribe` 早于 `connect` 的暂存恢复
  - 保留 `mtu update` 早于 `connect` 的 deferred 恢复
  - 恢复场景目标仍是 `MTU 517` 下 `value_max=500 / payload_max=480`
- 按键与启动稳定性：
  - `xl9555` 探测加有限重试
  - 扩展器不可用时保留 `direct.gpio0` 保底路径
  - `P11` 真实 `KEY1 -> KEY1` 已按人工听感口径通过
- 测试真实性修正：
  - 随机性从录音时长扩展到使用节奏
  - `P8/P9` 改为同一条串口会话内完成取消和恢复录音
  - `--no-reset-before-capture` 会压住 `DTR/RTS` 并检查意外 boot marker
  - 参考音升级为可复现的 `speech-like loop`
- 矩阵外剩余项：
  - `P12` 已有当前桌面 ambient RF baseline；受控距离 / 遮挡 / 强干扰实验仍需外场补测
  - `P13` 后端闭环端到端验收
  - `P10` 仍需要更长时间的量产级 soak

## 主机侧订阅顺序（不要改）

1. `GattSession.MaintainConnection`
2. 服务发现走 `CACHED` 优先
3. 按 `UUID` 过滤
4. **先写 `CCCD notify`**
5. **成功后再挂 `ValueChanged`**
6. `OSError(22)` 走 warm-up / rebuild fallback

不要改回"先挂 ValueChanged 再写 CCCD"，会落入 `OSError(22)`。

## 设备端竞态兼容（不要删）

Windows 自动恢复订阅时 `BLE_GAP_EVENT_SUBSCRIBE` 可能早于 `BLE_GAP_EVENT_CONNECT`。设备端暂存提前到来的订阅状态，connect 后恢复。删掉会导致主机侧 `OSError(22)` / `status=1`。

## 关键约束

- 不要把 `audio_data` 重新解释回 "chunk + fragment"
- 不要把"零缺 chunk"重新引回自动验收主口径
- 不要改主机侧订阅顺序
- 不要删设备端 "subscribe 早于 connect" 兼容
- 统一 `UTF-8`
- 同一时间只能一条自动回归独占一个 `COM` 口

## 已知限制

- 还不是最终常驻 Windows 语音输入 app
- `WinRT OSError(22)` 可恢复但不保证不出现
- 复位后自动重连比"已连接直接抓音"更脆
- 音频声学验证受环境音量影响，传输层验证更可靠
- 旧 `R1-R5` 已收敛为当前 `P` 系列产品使用面矩阵中的兼容入口

## 测试准确性约束

- 自动矩阵的 `pass/fail` 依据是真实采集结果：
  - 实际 `wav`
  - 实际 `expected_packet_count / received_packet_count / missing_packet_count`
  - 实际串口日志
- `--no-reset-before-capture` 现在会显式在串口 `open()` 前把 `DTR/RTS` 拉低：
  - 目标是避免 `pyserial` 默认控制线抖动把板子偷偷复位
  - 这是为了保证“连续使用 / 不重启设备”场景真的不被脚本自己污染
- 当 `--no-reset-before-capture` 生效时，脚本现在会主动检查是否出现意外 boot marker：
  - 一旦看到 `rst:0x` / `ESP-ROM` / `boot: ESP-IDF`
  - 会直接判成失败，而不是继续跑完再把结果误当成有效样本
- 每轮都会打印 `random_seed`、随机窗口和实际计划，失败后可以按同一 seed 复现
- `P8/P9` 现在必须在同一条串口会话里完成：
  - 先 `cancel`
  - 再做下一轮正式录音
  - 目标是避免“二次开串口把板子重启了，导致恢复验证失真”
- notify 就绪判定除了看 `audio notify subscription changed` / `restored before connect` 之外，也接受
  `audio notify subscribed:`：
  - 这是为了避免串口日志偶发互相穿插时，把已经就绪的链路误判成未就绪
  - 但仍然要求它来自设备端真实日志，不会用脚本内部状态冒充通过
- 当前参考音已经从“固定 880Hz 周期蜂鸣”升级成“可复现的 speech-like loop”：
  - 仍然是脚本可控、可复现的合成源
  - 但包络、停顿、音强变化更接近真实说话节奏
  - 比之前更适合验证首句、连续多句、取消后恢复这类场景
- `P7` 的自动 recover 重试只允许用于主机 / 传输类失败，不再对任意失败都自动重试
- `P11/P12/P13` 不会被自动矩阵冒充成已通过：
  - `P11` 已按真实按键人工验收口径通过，但仍不计入自动矩阵
  - `P12` 已形成 ambient RF baseline，但受控弱环境实验仍需单独记录
  - `P13` 仍需要后端联调

## 产品级使用面测试矩阵

下面这组矩阵用于回答：

- 当前 `BLE` 音频上传链路离“产品级使用面”还差什么
- 现有 `P1-P13` 到底覆盖了哪些风险
- 后续该优先补哪些自动化或半自动化验证

当前推荐把覆盖状态分成三档：

- `已覆盖`
  当前已有明确脚本或稳定手工路径，且目标场景已经被当前回归直接命中
- `部分覆盖`
  当前有相近脚本或局部验证，但还不能代表真实产品使用面已经被稳定证明
- `未覆盖`
  当前没有正式回归入口，或现有验证不足以支持产品级判断

### 随机化口径

当前自动矩阵默认已经不是“每次固定录同样长的片段”，而是更贴近真实使用节奏的随机化口径：

- 录音时长随机：
  - 短会话默认在 `capture_seconds-1` 到 `capture_seconds+2`
  - 长会话默认在 `long_capture_seconds-5` 到 `long_capture_seconds+5`
- 使用节奏随机：
  - 开始前等待 `pre_start_delay`
  - 多轮 session 间隔 `inter_session_gap`
  - 空闲后再说的等待时长 `idle_wait`
  - 中途取消停留 `cancel_hold`
  - 短异常结束停留 `short_cancel_hold`
- 每次回归都会打印 `random_seed`、随机窗口和实际计划，保证失败后可按同一 seed 复现
- `P9` 的“短取消后下一轮恢复”已改成单串口会话连续执行，避免测试自己因为二次开串口把板子重启，导致恢复验证失真

### 连续使用口径

除了默认的 `standard` 口径外，矩阵现在还支持更贴近真实连续使用的 `realistic` 口径：

- `standard`：
  - 自动矩阵默认口径
  - 每个 case 前都允许做主机侧 recover
  - 更适合回归、bisect 和定位单点回退
- `realistic`：
  - 等价于 `--realistic-usage-profile`
  - 会自动启用：
    - `shuffle_auto_case_order`
    - `preflight_recover_mode=initial-only`
    - `--no-reset-before-capture`
  - 目标是更接近“设备一直在线、用户连续使用、主机不每轮都帮你擦桌子”的真实产品场景

这两种口径都保留是刻意的：

- `standard` 更利于稳定复现和快速回归
- `realistic` 更利于评估量产前的真实连续使用稳定性

### 当前覆盖判断

如果只讨论“嵌入式前端 + BLE 上传 + Windows 接收重组”这条链路，当前自动矩阵已经能覆盖大部分日常真实使用主路径：

- 单句
- 连续多句
- 长句
- 主机恢复
- 断开重连
- 空闲后首句
- 中途取消
- 异常结束后下一句
- 短时间连续 soak

但如果把“量产产品真实世界”算进去，当前还不能说覆盖了大部分最终场景。还没被当前自动矩阵充分证明的，至少包括：

- 真实 `KEY1` 物理按键多轮路径
- 弱环境 / 距离 / 干扰
- 多小时 / 全天 soak
- 真实人声强弱、说话距离、角度变化
- 后端闭环后的整链路时延与恢复
- 上电 / 断电 / brownout / watchdog / crash dump 这类量产故障面

更准确的表述应该是：

- 当前自动矩阵已经覆盖了大部分“BLE 前端主使用面”
- 还没有覆盖大部分“整机量产真实世界场景”

### 嵌入侧下一步

如果接下来只看嵌入式侧，我建议优先补这几件事：

- `P11` 后续只保留为回归抽检项：
  - 真实 `KEY1 -> KEY1` 人工复验已经通过
  - 后续量产前可继续抽检按键抖动、长按/短按节奏、取消后下一次还能不能用
- 长时 soak：
  - 现在 `P10` 还是短 soak
  - 需要补 `1h+` 甚至更长的连续使用验证
- 设备侧故障可观测性：
  - 固化 reset reason
  - 固化 disconnect reason
  - 固化 dropped packet 统计
  - 为量产和售后保留定位依据
- 弱环境实验配套：
  - 在距离、遮挡、干扰条件下看 reconnect、首句恢复和 session 完整性
- 电源 / 异常恢复：
  - 补上上电、断电、brownout、watchdog、异常重启后的 bond / reconnect / session 恢复行为

### 矩阵总览

| ID | 场景 | 用户视角目标 | 当前入口 | 覆盖状态 | 当前结论 | 优先级 |
|---|---|---|---|---|---|---|
| P1 | 单轮标准录音 | 说一句话并正常收到完整音频 | `verify_audio_ble_upload_end_to_end.py` | 已覆盖 | `standard + realistic` 均已通过 | 高 |
| P2 | 连续短句多轮输入 | 连续说多句后仍稳定 | `verify_audio_ble_upload_multi_round.py` | 已覆盖 | `standard + realistic` 均已通过 | 最高 |
| P3 | 长句 / 长时间说话 | 一次较长录音后仍完整 | `verify_audio_ble_upload_long_session.py` | 已覆盖 | `standard + realistic` 均已通过 | 高 |
| P4 | 蓝牙恢复后再次录音 | 系统恢复后还能继续使用 | `verify_audio_ble_upload_recovery.py` | 已覆盖 | `standard + realistic` 均已通过 | 最高 |
| P5 | 断开重连后再次录音 | 设备重连后继续使用 | `verify_audio_ble_upload_reconnect.py` | 已覆盖 | `standard + realistic` 均已通过 | 最高 |
| P6 | 长时间空闲后首句输入 | 放一段时间再说第一句仍正常 | `verify_audio_ble_product_matrix.py` | 已覆盖 | `standard + realistic` 均已通过 | 高 |
| P7 | 主机侧接收进程重启 | Windows 接收端重启后继续录音 | `verify_audio_ble_product_matrix.py` | 已覆盖 | `standard + realistic` 均已通过 | 高 |
| P8 | 录音中途取消 | 用户取消后状态机与后续录音正常 | `verify_audio_ble_product_matrix.py` | 已覆盖 | `standard + realistic` 均已通过，且已改成同串口真实恢复 | 中高 |
| P9 | 超时 / 异常结束 | 录音未正常结束时能正确超时与恢复 | `verify_audio_ble_product_matrix.py` | 已覆盖 | `standard + realistic` 均已通过，且已改成同串口真实恢复 | 中高 |
| P10 | 长时间 soak | 多轮长时间连续使用后仍稳定 | `verify_audio_ble_product_matrix.py` | 已覆盖 | 短 soak 在 `standard + realistic` 均已通过 | 高 |
| P11 | 真实按键录音复验 | 真实 `KEY1 -> KEY1` 录音成功，听感内容符合预期 | `physical-key` | 人工验收已覆盖 | 两轮人工复验通过，保留 `source=key1` 基线；非自动矩阵项 | 高 |
| P12 | 弱环境 / 边界环境 | 干扰、距离变化时仍可用 | ambient RF formal record + 受控外场 | 部分覆盖 | 当前桌面 ambient RF baseline 已通过；受控距离 / 遮挡 / 强干扰待补 | 中 |
| P13 | 后端联调端到端 | 音频送到后端后整体闭环可接受 | 后端联调 | 未覆盖 | 当前仅前端基线完成 | 高 |

### 现有回归与场景映射

- `verify_audio_ble_product_matrix.py`
  对应 `P1-P10`，验证自动化产品使用面主路径
- `verify_audio_ble_upload_end_to_end.py`
  对应 `P1`，保留单轮标准自动触发链路的兼容入口
- `verify_audio_ble_upload_multi_round.py`
  对应 `P2`，保留连续多轮会话回归入口
- `verify_audio_ble_upload_long_session.py`
  对应 `P3`，保留长时录音完整性回归入口
- `verify_audio_ble_upload_recovery.py`
  对应 `P4`，保留主机恢复动作后的回归入口
- `verify_audio_ble_upload_reconnect.py`
  对应 `P5`，保留断开 / 重连后的回归入口
- `physical-key`
  对应 `P11`，验证真实用户按键路径

### 目前已经回答的问题

- 单轮链路能不能跑通：
  已回答，`P1` 和 `physical-key / P11` 都已经提供了基线
- 当前会话模型能不能支撑后端联调：
  已回答，单轮可联调
- 真实按键路径是不是假通：
  已复验到 `source=key1` 的真实 `KEY1 -> KEY1` 路径，并完成第二轮人工听感确认，不是串口 `usb` 触发假通

### 目前还没被量产级证明的问题

- `P11` 真实按键路径已完成两轮人工复验，但还不等于按键寿命、长时间误触发、极端按压节奏这些量产耐久项已覆盖
- `P12` 已有 ambient RF baseline 正式数据，但还没有受控距离 / 遮挡 / 强干扰实验数据
- `P13` 后端闭环还没有接入正式验收
- `P10` 当前仍是短 soak，不等于全天 / 多小时量产 soak
- `WinRT notify_enable_fallback=winrt_warmup` 日志偶发仍会出现，虽然当前未再造成矩阵失败
- 真正接上后端后，整体时延、容错和会话结束语义是否仍然满足产品目标

### 当前优先级建议

#### 第一优先级：先把“每天都会撞到”的产品主路径补齐

- `P2` 连续短句多轮输入
- `P4` 蓝牙恢复后再次录音
- `P5` 断开重连后再次录音
- `P6` 长时间空闲后首句输入
- `P7` 主机侧接收进程重启

原因：

- 这些场景最接近真实用户每天会遇到的“第二句、下一句、下一次还能不能用”
- 当前版本的真实脆点也正集中在这类状态清理与恢复问题上

#### 第二优先级：补齐异常结束与长时间使用

- `P8` 录音中途取消
- `P9` 超时 / 异常结束
- `P10` 长时间 soak

原因：

- 这些问题更容易在产品上线后演化成“偶发但难定位”的稳定性投诉

#### 第三优先级：补齐环境与后端闭环

- `P12` 弱环境 / 边界环境
- `P13` 后端联调端到端

原因：

- 这些更偏系统级验收，不一定阻塞当前前端链路继续收敛

### 当前建议的产品级准入门槛

如果后面要把 `BLE` 音频上传说成“达到产品级使用面基线”，建议至少满足下面这些条件：

- `P1-P10` 自动矩阵在多随机种子下稳定通过
- `P1-P10` 最好同时在 `standard` 和 `realistic` 两种口径下通过
- `P8/P9` 确认是同串口真实恢复验证，不是二次开串口伪恢复
- `P11` 真实按键路径完成重复人工复验
- `P12` 至少有一轮正式环境记录；量产前还需要受控距离 / 遮挡 / 强干扰记录
- `P13` 有至少一轮后端闭环记录

在这之前，更准确的表述仍然应该是：

- `BLE 自动化产品矩阵主路径已收敛`
- `BLE 量产级验收仍缺受控弱环境实验和后端闭环；真实按键路径已按人工验收口径通过`

## 验证方式

```powershell
# 直接抓一段 BLE 录音
python .\tools\capture_audio_ble_wav.py --port COM3 --capture-seconds 5

# KEY1 物理按键模式
python .\tools\capture_audio_ble_wav.py --port COM3 --capture-seconds 5 --trigger-mode physical-key --no-reset-before-capture

# KEY1 物理按键模式 + 明确超时预算
python .\tools\capture_audio_ble_wav.py --port COM3 --capture-seconds 10 --trigger-mode physical-key --no-reset-before-capture --timeout-seconds 120

# KEY1 物理按键模式验收
python .\tools\verify_audio_capture_session_end_to_end.py --port COM3 --capture-seconds 10 --artifacts-dir .\tests\artifacts\audio --trigger-mode physical-key --no-reset-before-capture --timeout-seconds 120

# 产品级使用面自动矩阵（P1/P2/P3/P4/P5/P6/P7/P8/P9/P10，默认随机录音时长 + 随机使用节奏）
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --capture-seconds 5 --long-capture-seconds 30 --round-count 3 --idle-seconds 30 --soak-round-count 5

# 指定随机种子，便于复现
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --capture-seconds 5 --long-capture-seconds 30 --round-count 3 --idle-seconds 30 --soak-round-count 5 --random-seed 20260520

# 更贴近真实连续使用的口径
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --capture-seconds 5 --long-capture-seconds 30 --round-count 3 --idle-seconds 30 --soak-round-count 5 --realistic-usage-profile --random-seed 20260521

# 当前门禁口径：失败或 warning 都不允许悄悄通过，并保留 JSON 结果
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases P1,P2,P3,P4,P5,P6,P7,P8,P9,P10 --realistic-usage-profile --fail-on-warning --matrix-result-json tests\artifacts\ble_product_matrix\matrix_result_realistic_latest.json

# P12 ambient RF baseline 记录口径；受控距离 / 干扰实验需要另行注明环境条件
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --cases P1,P3,P8,P9,P10 --realistic-usage-profile --random-seed 20260520 --fail-on-warning --matrix-result-json tests\artifacts\ble_product_matrix\p12\p12_ambient_rf_matrix_20260517.json

# P1：标准端到端回归
python .\tools\verify_audio_ble_upload_end_to_end.py --port COM3 --capture-seconds 5 --no-reset-before-capture

# P2 / P3 / P4 / P5：兼容旧单项入口
python .\tools\verify_audio_ble_upload_multi_round.py --port COM3 --capture-seconds 10
python .\tools\verify_audio_ble_upload_long_session.py --port COM3 --capture-seconds 60
python .\tools\verify_audio_ble_upload_recovery.py --port COM3 --capture-seconds 10
python .\tools\verify_audio_ble_upload_reconnect.py --port COM3 --capture-seconds 10
```

`physical-key` 模式下，录音时长以两次真实 `EC11_KEY` 按下之间的时间为准。`--capture-seconds` 只是默认提示 / 参考参数，不要求实际录音严格落在目标秒数附近。脚本验收重点是：

- 日志必须包含 `recording start source=ec11_key`
- 日志必须包含 `recording stop source=ec11_key`
- BLE session 传输完整
- WAV 有有效声音活动
- 录音时长在 `--timeout-seconds` 预算内

这样可以避免把真实用户多说几秒误判成失败，同时仍然避免串口 `usb` 假触发冒充 `P11`。

### 最近一次已记录的 physical-key 基线

- 最近一次已记录的 `EC11_KEY -> EC11_KEY` 真实按键日志基线：
  - `session_id=75`
  - `recording start source=ec11_key`
  - `recording stop source=ec11_key`
  - `expected_packet_count=1067`
  - `received_packet_count=1067`
  - `missing_packet_count=0`
  - `received_pcm_bytes=512000`
  - `duration_seconds=16.000`
  - `rate=16000`
  - `channels=1`
  - `width=2`
- 对应产物：
  - `tests/artifacts/p11_rerun_20260515/capture_ble_latest_16k_mono.wav`
  - `tests/artifacts/p11_rerun_20260515/verify_audio_capture_session_latest.log`
- 说明：
  - 这是一条真实 `KEY1` 按下开始、再次按下结束的按键窗口基线
  - 日志里明确是 `source=key1`，不是串口 `usb` 触发
  - 本轮 `packet_loss_ratio=0.0000`，BLE 传输零丢包
  - `duration_seconds=16.000` 是两次真实按键之间的录音时长，不是失败信号
  - `P11` 不计入 `P1-P10` 自动矩阵，也不代表 `P12/P13` 已通过

### 当前回归结论更新

- 截至 `2026-05-17`，BLE 音频产品矩阵稳定性修复计划 Step 1-5 已完成当前代码与工作台验证闭环。
- `P1-P10` 自动矩阵在 `standard` 口径下多种子通过：
  - `tests/artifacts/ble_product_matrix/matrix_result_standard_final.json`
  - `tests/artifacts/ble_product_matrix/matrix_result_standard_seed_20260518.json`
- `P1-P10` 自动矩阵在 `realistic` 口径下多种子通过：
  - `tests/artifacts/ble_product_matrix/matrix_result_realistic_final.json`
  - `tests/artifacts/ble_product_matrix/matrix_result_realistic_seed_20260519.json`
- 上述四轮主矩阵均为：
  - `matrix_total=10`
  - `matrix_failed=0`
  - `matrix_warning=0`
  - `matrix_skipped=0`
- P12 当前已有 ambient RF baseline 正式记录：
  - `tests/artifacts/ble_product_matrix/p12/p12_ambient_rf_matrix_20260517.json`
  - `tests/artifacts/ble_product_matrix/p12/p12_ambient_rf_20260517.md`
  - 覆盖 `P1/P3/P8/P9/P10` representative subset，`matrix_total=5`，`matrix_failed=0`，`matrix_warning=0`
- 当前仍需明确区分：
  - `P1-P10` 已是自动化真实通过
  - `P11` 真实按键已按人工验收口径通过，但不是自动矩阵通过
  - `P12` ambient RF baseline 已通过，但不是受控距离 / 遮挡 / 强干扰通过
  - `P13` 还没有后端闭环正式验收数据

## 产物

- 常规回归产物为临时文件，运行脚本后会重新生成：
  - `tests/capture_ble_latest_16k_mono.wav`
  - `tests/capture_ble_latest.log`
  - `tests/artifacts/audio_regression_sources/`
  - `tests/artifacts/ble_product_matrix/`
- 当前长期保留的人工验收证据：
  - `tests/artifacts/p11_single/capture_ble_latest_16k_mono.wav`
  - `tests/artifacts/p11_single/verify_audio_capture_session_latest.log`
  - `tests/artifacts/p11_rerun_20260515/capture_ble_latest_16k_mono.wav`
  - `tests/artifacts/p11_rerun_20260515/verify_audio_capture_session_latest.log`
- 当前长期保留的 P12 baseline 证据：
  - `tests/artifacts/ble_product_matrix/p12/p12_ambient_rf_matrix_20260517.json`
  - `tests/artifacts/ble_product_matrix/p12/p12_ambient_rf_20260517.md`

## 关键代码位置

- [ports/esp32/audio_capture/audio_capture_esp32.c](../../ports/esp32/audio_capture/audio_capture_esp32.c) — 采样与录音
- [components/voice_recording_control/voice_recording_control.c](../../components/voice_recording_control/voice_recording_control.c) — 录音控制状态机
- [ports/esp32/voice_key_input/voice_key_input_esp32.c](../../ports/esp32/voice_key_input/voice_key_input_esp32.c) — 语音键输入
- [ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c](../../ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c) — BLE 音频上行
- [protocols/listener_proto/include/listener_audio_proto.h](../../protocols/listener_proto/include/listener_audio_proto.h) — 协议头
- [tools/capture_audio_ble_wav.py](../../tools/capture_audio_ble_wav.py) — 主机端抓音与重组
- [tools/ble_audio_regression_common.py](../../tools/ble_audio_regression_common.py) — 回归公共逻辑
- [tools/verify_audio_ble_product_matrix.py](../../tools/verify_audio_ble_product_matrix.py) — 产品使用面矩阵入口
- [tools/verify_audio_ble_upload_end_to_end.py](../../tools/verify_audio_ble_upload_end_to_end.py)
- [tools/verify_audio_ble_upload_multi_round.py](../../tools/verify_audio_ble_upload_multi_round.py)
- [tools/verify_audio_ble_upload_long_session.py](../../tools/verify_audio_ble_upload_long_session.py)
- [tools/verify_audio_ble_upload_recovery.py](../../tools/verify_audio_ble_upload_recovery.py)
- [tools/verify_audio_ble_upload_reconnect.py](../../tools/verify_audio_ble_upload_reconnect.py)
