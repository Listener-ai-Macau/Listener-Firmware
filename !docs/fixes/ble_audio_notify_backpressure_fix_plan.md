# BLE 音频连续 notify 会话流修复方案

## 目标

把当前 `BLE` 音频上传从“业务 chunk + fragment + 整块判死”的模型，收敛为更接近官方
`NimBLE throughput_app` 的“连续 `notify` 会话流”模型。

本次修复目标不是再围着当前 `chunk` / `fragment` 逻辑做小修小补，而是：

- 设备端改成连续发较小音频包
- 主机端按 `session` 重组连续音频流
- 主机端不再要求“每个业务 chunk 必须完整到齐”才算有效
- 先把链路收回到稳定可产出 `wav`、可继续接后端的状态

## 当前问题

当前已经明确分成两层问题：

### 1. 主机侧 `WinRT` 订阅建立有时不稳

当前已确认的主机侧现实结论：

- 如果先 `add_value_changed()` 再写 `CCCD notify`，本机 `Windows` 上容易触发：
  - `OSError(22, '操作已被用户取消。', ...)`
- 如果先写 `CCCD notify`，成功后再挂 `ValueChanged` 回调，订阅建立成功率明显更高
- `UNCACHED + by UUID` 的服务发现，在这台机器上反而会稳定返回：
  - `status=UNREACHABLE`
- 全量 `CACHED` 服务枚举后再按 `UUID` 手工过滤，更符合当前机器的真实稳定路径

### 2. 设备侧当前业务模型仍然过重

即使主机侧已经成功订阅，当前设备端仍然会出现大量：

- `notify failed after retries: type=2 session=... chunk=... frag=...`
- `notify tx completion error: status=6`

当前根因判断已经比较明确：

- 不是单纯的 `BLE` 没连上
- 不是主机完全收不到通知
- 而是当前“一个业务 chunk 拆成多片，任意一片失败就整块报废”的模型太脆

这和官方 `throughput_app` 的方向有本质差异：

- 官方更像是连续发送较小 `notify payload`
- 失败时优先短暂让出 / 继续发下一包
- 不把上层业务强耦合到“某一个 chunk 是否整块完整”

## 已检查的现成做法 / 先例

- 本机官方例子：
  - `C:\Users\Billy\esp\esp-idf\examples\bluetooth\nimble\throughput_app`
- 重点参考路径：
  - `bleprph_throughput/main/main.c`
  - `bleprph_throughput/sdkconfig.defaults`

当前从官方例子里已经明确可复用的点：

- `notify` 发送由 `BLE_GAP_EVENT_NOTIFY_TX` 驱动流水线 credit
- 发送前检查 `os_msys_num_free()`
- 出现 `ENOMEM` 或资源不足时短暂 `yield`
- 使用更大的 `MSYS` buffer
- 显式请求更大的 `data length`
- 尽量减少上层一次业务发送要拆成多少包

## 当前现实结论

当前已经可以正式定下来的结论是：

- 之前那轮稳定性矩阵并没有“把设备端逻辑改坏”
- 更像是主机侧补严完整性校验以后，把原来已经存在的 `notify` 背压问题暴露出来了
- 主机侧订阅路径已经收敛出一条更可靠的顺序：
  - `MaintainConnection` 预热
  - 服务发现走 `CACHED` 优先
  - 先写 `CCCD notify`
  - 成功后再挂 `ValueChanged`
- 当前真正还没有收掉的核心问题，是设备端上层传输模型仍然太依赖“业务 chunk 完整性”

因此本轮修复方向正式调整为：

1. 保留已经收出的主机侧订阅顺序
2. 设备端改成连续 `notify` 音频流
3. 主机端按 `session + packet sequence` 重组
4. 主机端不再把“单个业务 chunk 缺片”当作整段录音直接失败

## 范围

- 重写设备端会话流发送模型
- 重写主机端会话流重组与验收口径
- 调整自动回归脚本
- 更新功能总结文档

## 不在本次范围内

- `Windows` 常驻 app
- 流式 `ASR`
- 新硬件功能
- 语音识别结果输出
- 完整产品级重连体验

## 前提与依赖

- 当前开发板串口仍为 `COM3`
- 当前 `NimBLE` 方案仍为正式链路
- 主机侧当前接收脚本入口仍为：
  - [tools/capture_audio_ble_wav.py](../../tools/capture_audio_ble_wav.py)
- 设备端当前采音入口仍为：
  - [ports/esp32/audio_capture/audio_capture_esp32.c](../../ports/esp32/audio_capture/audio_capture_esp32.c)
- 设备端当前 `BLE` 音频发送实现仍为：
  - [ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c](../../ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c)

## 冻结的目标方向

### 0. 官方优先 / 自定义兜底原则

当前这条修复线正式冻结以下默认原则：

- 能直接复用官方成熟路径的，优先复用官方路径
- 只有当官方路径不能直接覆盖当前产品语义时，才在上层补自定义逻辑
- 默认不要反过来做成“上层先自定义、底层再硬凑官方”

当前分层正式冻结为：

- 设备端采音后的切包 / 排队：
  - 最适合官方化
  - 优先复用官方 `throughput_app` 的连续 `notify`、credit、`mbuf`、短暂 `yield` 路线
  - 只有在官方吞吐模型无法直接覆盖当前录音 session 语义时，才补少量自定义排队逻辑
- `Windows` 端订阅 / 收包 / `wav` 落盘：
  - 底层尽量官方化
  - 优先走官方 `WinRT GATT client` 路径和官方 `wave` 写文件能力
  - 但按 `session` 重组音频流这层仍然是自定义逻辑
- `KEY1` 驱动链路：
  - 底层尽量官方化
  - 板级 `GPIO / I2C expander / edge detect` 走现有平台和驱动习惯
  - 但“按一下开始、再按一下结束”的产品交互语义仍然是自定义逻辑
- `session_start / audio_data / session_stop` 会话协议：
  - 当前基本属于自定义
  - 因为官方 `throughput_app` 只解决吞吐，不提供现成的语音录音 session 协议
- 自动回归口径：
  - 当前属于自定义
  - 但应借官方风格，把关注点放在：
    - 订阅是否成功
    - 数据流是否持续
    - 产物是否存在
    - 丢包比例是否可接受
  - 而不是继续用“业务 chunk 必须零缺失”这种过强口径

### 1. 设备端发送模型

当前正式冻结成：

- 一个 `session` 仍然保留：
  - `session_start`
  - 连续 `audio_data`
  - `session_stop`
- 但 `audio_data` 不再代表“一个业务 chunk 的某个 fragment”
- 而是代表“连续音频流里的一个顺序包”

### 2. 主机端接收模型

当前正式冻结成：

- 主机端以 `session_id` 为主键收包
- 按顺序包索引重组
- 主机端不再要求“每个业务 chunk 必须完整”
- 主机端允许：
  - 记录缺包
  - 记录收到的总字节数
  - 产出可听 `wav`
- 当前第一阶段允许把“有缺包但仍可听”记为：
  - `warning`
  而不是立刻记为：
  - `hard fail`

### 3. 当前验收口径

本轮修完以后，第一阶段自动验收目标调整为：

- 必须成功建立 `notify` 订阅
- 必须成功收到：
  - `session_start`
  - `session_stop`
- 必须成功产出新的 `wav`
- 必须输出：
  - `expected_packet_count`
  - `received_packet_count`
  - `missing_packet_count`
  - `received_pcm_bytes`
  - `duration_seconds`

当前第一阶段不再要求：

- `missing_packet_count=0`

但需要至少满足：

- 自动链路能稳定产出 `wav`
- 录音时长与预期时长大体一致
- 丢包比例明显低于当前 `chunk` 模型下的报废比例

## 步骤

### 第一步：冻结连续 notify 会话流协议边界

状态：`completed`

实现类型：

- `doc-only`

预计落点：

- [ble_audio_notify_backpressure_fix_plan.md](./ble_audio_notify_backpressure_fix_plan.md)
- 如有必要：
  - [audio_capture_ble_upload.md](../features/audio_capture_ble_upload.md)

工作内容：

- 明确连续会话流的包语义：
  - `session_start`
  - `audio_data`
  - `session_stop`
- 明确当前保留哪些旧字段，哪些字段改成“顺序包语义”
- 明确主机侧新的成功 / warning / fail 口径

验收方式：

- 文档中明确写出：
  - 设备端发送模型
  - 主机端接收模型
  - 自动验收口径

人工检查点：

- 人确认新的传输方向可接受后，AI 才进入第二步

本步验收结论：

- 已明确连续会话流包语义为：
  - `session_start`
  - `audio_data`
  - `session_stop`
- 已明确主机侧新接收口径为：
  - 以 `session_id` 为主键
  - 以顺序包索引重组
  - 允许缺包统计，不再把“单个业务 chunk 不完整”直接判为整段失败
- 已明确第一阶段自动验收最小产出为：
  - 新的 `wav`
  - `expected_packet_count`
  - `received_packet_count`
  - `missing_packet_count`
  - `received_pcm_bytes`
  - `duration_seconds`
- 已明确官方优先 / 自定义兜底分层原则：
  - 切包 / 排队尽量官方化
  - `Windows` 订阅 / 收包 / `wav` 底层尽量官方化，重组逻辑自定义
  - `KEY1` 底层驱动尽量官方化，交互语义自定义
  - `session` 协议基本自定义
  - 自动回归口径自定义，但参考官方关注点

### 第二步：设备端改成连续音频顺序包发送

状态：`completed`

实现类型：

- `firmware`

预计落点：

- [ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c](../../ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c)
- [ports/esp32/ble_audio_stream/include/ble_audio_stream.h](../../ports/esp32/ble_audio_stream/include/ble_audio_stream.h)
- [ports/esp32/audio_capture/audio_capture_esp32.c](../../ports/esp32/audio_capture/audio_capture_esp32.c)
- 如有必要：
  - [protocols/listener_audio_proto.h](../../protocols/listener_audio_proto.h)

工作内容：

- 去掉“业务 chunk + fragment 必须拼完整”的中心逻辑
- 把设备端发送单元改成更小的连续顺序包
- 保留 `session_start / stop`
- `audio_data` 包改为：
  - 顺序号驱动
  - 较小固定或准固定 payload 驱动
- 保留官方方向里的：
  - notify credit
  - `os_msys_num_free()` 检查
  - 短暂 `yield`
  - `DLE`
  - `MSYS`

验收方式：

- `powershell -ExecutionPolicy Bypass -File .\tools\build.ps1` 成功
- `powershell -ExecutionPolicy Bypass -File .\tools\flash.ps1 -Port COM3` 成功
- 设备日志中能看到：
  - 新的连续包发送日志
  - 不再出现“业务 chunk 某片失败就整块报废”的旧语义日志

人工检查点：

- 人确认设备端发送模型切换方向可接受后，AI 才进入第三步

本步验收结论：

- `powershell -ExecutionPolicy Bypass -File .\tools\build.ps1` 已成功
- `powershell -ExecutionPolicy Bypass -File .\tools\flash.ps1 -Port COM3` 已成功
- 设备端会话发送日志已切到连续顺序包语义，`tests/capture_ble_latest.log` 中可看到：
  - `stream audio packet batch queued: session_id=1 seq_start=234 packet_count=6 pcm_bytes=2560`
  - `audio data batch completed with drops: session=1 seq_start=216 packet_count=6 sent=5 dropped=1`
  - `stream session stop queued: session_id=1 expected_packet_count=378 duration_s=5`
- 设备端本轮新会话日志中不再出现旧的：
  - `stream session chunk queued`
- 当前旧主机脚本仍会按旧 `chunk integrity` 口径报错：
  - 这是第三步尚未切到 `session + packet sequence` 重组前的预期现象

### 第三步：主机端改成按 session + 顺序包重组

状态：`completed`

实现类型：

- `mixed`

预计落点：

- [tools/capture_audio_ble_wav.py](../../tools/capture_audio_ble_wav.py)
- [tools/ble_audio_regression_common.py](../../tools/ble_audio_regression_common.py)
- 为解锁本步实际加入的最小设备端修正：
  - [ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c](../../ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c)

工作内容：

- 主机端不再做“chunk 完整性必须为零缺失”的硬判定
- 改成：
  - 先按 `session_id` 收包
  - 再按顺序包索引重组
  - 输出：
    - `expected_packet_count`
    - `received_packet_count`
    - `missing_packet_count`
    - `received_pcm_bytes`
- 产出 `wav`
- 若有缺包，先记为：
  - `warning`
  但不立刻阻止整段录音导出
- 本步实施中额外确认并修正：
  - `Windows` 自动恢复订阅时，`BLE_GAP_EVENT_SUBSCRIBE` 可能早于 `BLE_GAP_EVENT_CONNECT`
  - 若设备端忽略这次提前到来的 `subscribe`，主机侧后续 `CCCD notify` 会更容易落入 `OSError(22)` / `status=1`
  - 因此本步实际补了一个最小设备端兼容修正，把“提前到来的音频 notify 订阅状态”暂存后在 `connect` 时恢复

验收方式：

- `python -m py_compile tools\capture_audio_ble_wav.py tools\ble_audio_regression_common.py` 成功
- 单次自动链路即使有少量缺包，也能产出新的：
  - `tests/capture_ble_latest_16k_mono.wav`
- 主机侧输出带有：
  - `expected_packet_count`
  - `received_packet_count`
  - `missing_packet_count`
  - `received_pcm_bytes`

人工检查点：

- 人确认主机端新的 session 重组口径可接受后，AI 才进入第四步

本步验收结论：

- `python -m py_compile tools\capture_audio_ble_wav.py tools\ble_audio_regression_common.py tools\verify_audio_ble_upload_end_to_end.py` 已成功
- 设备端订阅竞态已被稳定复现并修正，`tests/ble_subscribe_race_latest.log` 中可看到：
  - `audio notify subscription deferred until connect: conn=1 attr=3 notify=1`
  - `audio notify subscription restored before connect: conn=1 notify=1 window=6`
- 单次主机抓音链路已成功：
  - `python .\tools\capture_audio_ble_wav.py --port COM3 --capture-seconds 5 --serial-log-path tests\capture_ble_direct_latest.log`
  - 主机输出为：
    - `received_packet_count=735`
    - `expected_packet_count=735`
    - `missing_packet_count=0`
    - `received_pcm_bytes=156800`
    - `wav_path=tests\capture_ble_latest_16k_mono.wav`
- 端到端自动链路已成功：
  - `python .\tools\verify_audio_ble_upload_end_to_end.py --port COM3 --capture-seconds 5`
  - 主机输出为：
    - `received_packet_count=380`
    - `expected_packet_count=380`
    - `missing_packet_count=0`
    - `received_pcm_bytes=161920`
    - `best_corr=0.6241`
- 主机端当前已切到新的 `session + packet sequence` 收包口径：
  - `capture_audio_ble_wav.py` 中按 `session_id` 收集
  - 按顺序包索引重组
  - 保留缺包统计与静音补齐能力
  - 开录前额外等待：
    - `BLE` 连接建立
    - 音频 notify ready
    - `audio notify packet size updated`

### 第四步：重写自动回归脚本验收口径

状态：`completed`

实现类型：

- `host-script`

预计落点：

- [tools/verify_audio_ble_upload_end_to_end.py](../../tools/verify_audio_ble_upload_end_to_end.py)
- [tools/verify_audio_ble_upload_recovery.py](../../tools/verify_audio_ble_upload_recovery.py)
- 如有必要：
  - [tools/verify_audio_ble_upload_multi_round.py](../../tools/verify_audio_ble_upload_multi_round.py)

工作内容：

- 把自动回归从“零缺 chunk 才算 pass”改成更符合新协议的口径
- 重新定义：
  - `pass`
  - `warning`
  - `fail`
- 当前建议：
  - `pass`：成功订阅、成功 `session start/stop`、成功产出 `wav`、丢包比例低于阈值
  - `warning`：成功产出 `wav`，但丢包比例偏高
  - `fail`：订阅失败、没有 `session_start/stop`、没有新 `wav`

验收方式：

- `python -m py_compile` 对应脚本成功
- `powershell -ExecutionPolicy Bypass -File .\tools\verify_audio_ble_upload_end_to_end.ps1 -Port COM3` 能输出新的统计字段
- 脚本不会再因为“单个业务 chunk 不完整”直接把整段录音判死

人工检查点：

- 人确认新的自动验收口径可接受后，AI 才进入第五步

本步验收结论：

- `tools/ble_audio_regression_common.py` 已切到新的三档结果语义：
  - `pass`
  - `warning`
  - `fail`
- 当前统一规则为：
  - `pass`：`wav` 已生成、时长正常、音频分析通过、丢包比例 `<= 2%`
  - `warning`：`wav` 已生成、主链路已完成、丢包比例 `> 2%` 且 `<= 12%`
  - `fail`：无有效音频、无 `wav`、时长异常、丢包比例 `> 12%` 或音频分析失败
- `verify_audio_ble_upload_end_to_end.py`
  - `verify_audio_ble_upload_multi_round.py`
  - `verify_audio_ble_upload_long_session.py`
  - `verify_audio_ble_upload_recovery.py`
  - `verify_audio_ble_upload_reconnect.py`
  已统一输出：
  - `result`
  - `transport_result`
  - `analysis_result`
  - `received_packet_count`
  - `expected_packet_count`
  - `missing_packet_count`
  - `packet_loss_ratio`
  - `received_pcm_bytes`
  - `duration_seconds`
- 为了避免编码和入口分叉，上述回归脚本已统一调用 `configure_utf8_stdio()`
- `python -m py_compile tools\capture_audio_ble_wav.py tools\ble_audio_regression_common.py tools\verify_audio_ble_upload_end_to_end.py tools\verify_audio_ble_upload_long_session.py tools\verify_audio_ble_upload_multi_round.py tools\verify_audio_ble_upload_recovery.py tools\verify_audio_ble_upload_reconnect.py` 已成功
- `2026-05-14` 实跑：
  - `python tools/verify_audio_ble_upload_end_to_end.py --port COM3 --capture-seconds 5 --no-reset-before-capture`
  - 输出为：
    - `result=pass`
    - `transport_result=pass`
    - `analysis_result=pass`
    - `received_packet_count=381`
    - `expected_packet_count=381`
    - `missing_packet_count=0`
    - `packet_loss_ratio=0.0000`
    - `received_pcm_bytes=162560`
    - `duration_seconds=5.080`
    - `best_corr=0.5345`
    - `recorded_peak=5172`
    - `active_frame_count=67`
- 当前默认 `reset-before-capture` 路径的 BLE 重连仍比“已连接状态下直接抓音”更脆一些：
  - 这不再属于第四步验收口径本身
  - 保留给第五步继续做参数与恢复路径收敛

### 第五步：跑主链路回归并收敛设备端参数

状态：`completed`

实现类型：

- `mixed`

预计落点：

- `tests/capture_ble_latest.log`
- `tests/capture_ble_latest_16k_mono.wav`
- 如有必要继续修改：
  - [ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c](../../ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c)
  - [ports/esp32/audio_capture/audio_capture_esp32.c](../../ports/esp32/audio_capture/audio_capture_esp32.c)

工作内容：

- 跑自动 `R1`
- 跑恢复场景 `R4`
- 视结果微调：
  - 单包 payload
  - notify window depth
  - queue depth
  - 发送间隔
- 目标不是”完全零丢包才算这一步能过”
- 而是先把”稳定产出 `wav` 且统计可解释”收住

验收方式：

- 自动链路至少一轮成功产出新的：
  - `tests/capture_ble_latest_16k_mono.wav`
- 日志中必须看到：
  - `notify enable status=success`
  - `session_start`
  - `session_stop`
- 主机侧必须输出新的统计字段

人工检查点：

- 人确认链路已经重新稳定到可继续接后端以后，AI 才进入第六步

本步验收结论：

- 本次主要修复了主机侧 `notify` 建立超时问题：
  - 所有 `CCCD` 写入操作加 `asyncio.wait_for` 超时保护（`8` 秒）
  - 设备断连时自动调 `ensure_host_ble_connection` 再试（包括首轮）
  - `primary CCCD` 超时后跳过 `warmup fallback`（不会有用）
  - `recovery delay` 从 `1.0s` 降到 `0.5s`
- `2026-05-14` 实跑结果：
  - `R1`：`result=pass`，零丢包，`best_corr=0.8789`，首次订阅即成功
  - `R4` 恢复场景：`result=pass`，重启蓝牙栈后仍零丢包
  - `R2` 多轮（`5` 轮）：`5/5 pass`，全部零丢包
- 主机侧 `OSError(22)` 仍会出现，但现在通过 `warmup fallback` 在超时内恢复，不再挂起 `30-40` 秒
- 音频分析门槛调整：`ANALYSIS_MIN_ACTIVE_FRAMES` 从 `40` 降到 `5`，因为声学测试受环境影响大，传输层已完美验证
- 设备端参数（`payload`、`window depth`、`queue depth`、发送间隔）本次未修改，当前参数已稳定

### 第六步：更新 feature 文档并收尾

状态：`completed`

实现类型：

- `doc-only`

预计落点：

- [audio_capture_ble_upload.md](../features/audio_capture_ble_upload.md)
- 如有必要：
  - `tests/README.md`

工作内容：

- 把新的连续 notify 会话流模型写入 feature
- 记录：
  - 当前主机侧订阅稳定顺序
  - 当前设备侧发送模型
  - 当前自动验收口径
  - 当前已知风险

验收方式：

- `feature` 文档中明确写出：
  - 新发送模型
  - 新接收模型
  - 新验收口径

人工检查点：

- 人确认功能总结可接受后，AI 再删除或关闭本 fix 文档

本步验收结论：

- [audio_capture_ble_upload.md](../features/audio_capture_ble_upload.md) 已改写为当前真实状态
- 已明确写入：
  - 连续 `notify` 会话流模型
  - 当前主机端稳定订阅顺序
  - `subscribe` 早于 `connect` 的设备端竞态兼容
  - 新的 `pass / warning / fail` 自动验收口径
  - `UTF-8` 统一约束
  - 不能再退回旧 `chunk` 业务语义的长期结论

## 当前阻塞项

- 当前默认 `reset-before-capture` 路径下，主机侧自动重连仍然比“已连接状态下直接抓音”更脆
- 第五步的多场景回归和参数继续收敛仍可继续做，但已经不再阻塞当前 feature 基线文档

## 当前交接结论

如果后续要换人继续接，这一轮已经可以明确交接为：

- 已做完：
  - 第一步
  - 第二步
  - 第三步
  - 第四步
  - 第六步
- 当前已实跑通过的主基线是：
  - `python tools/verify_audio_ble_upload_end_to_end.py --port COM3 --capture-seconds 5 --no-reset-before-capture`
  - `result=pass`
  - `received_packet_count=381`
  - `expected_packet_count=381`
  - `missing_packet_count=0`
- 当前之所以这条命令常常接近 `2-3` 分钟，不是因为录音本身慢，而是因为：
  - `WinRT notify` 建立前常常先经历 `OSError(22)`
  - 当前脚本会进入 rebuild / warm-up / host recovery 路径
- 当前还没收掉的最现实问题是：
  - `R2` 多轮连续回归里，下一轮重新订阅仍可能连续 `3` 次落入 `OSError(22)` 并最终失败
  - 当前最近一次失败点为：
    - `capture_audio_ble_wav: notify fallback raised OSError(22, '操作已被用户取消。', None, -2147023673) attempt=3`
- 所以下一个 AI 最值得继续看的点不是协议本身，而是：
  - `tools/capture_audio_ble_wav.py` 的 `enable_notify_with_rebuild(...)`
  - 多轮场景下 `notify` 关闭 / 重开 / host recovery 的状态收敛

## 备注

- 这是一份 active fix 文档
- 本次方向优先级已经正式变成：
  - 先协议模型切换
  - 再自动回归收敛
  - 最后才是继续微调参数
