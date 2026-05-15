# 语音输入前端到后端的输入契约

## 状态

- 状态：`可联调`
- 定位：`后端接入语音输入链路时应依赖的稳定输入边界`
- 当前阶段结论：`单轮 BLE 录音上传链路已经可用，可作为后端联调基线；多轮、恢复、重连稳定性仍未完全产品化`

## 文档目的

这份文档不定义 `BLE` 底层实现细节的所有行为，而定义后端当前应该依赖的输入契约。

后端接入时，优先依赖下面这些稳定语义：

- 音频格式
- 会话起止语义
- 顺序包重组语义
- 成功 / 失败口径
- 超时与错误处理边界

不要把当前 `BLE` 的承载细节直接等同于最终后端协议本体。

## 当前链路分层

当前已跑通的链路是：

`KEY1 / 串口 toggle -> voice_recording_control -> audio_capture -> ble_audio_stream -> Windows 主机重组 -> wav / 上层接入点`

面向后端时，建议按两层理解：

- 传输承载层：
  当前是 `BLE notify -> Windows 主机接收`
- 语音输入会话层：
  当前已经形成稳定的 `session_start -> audio_data -> session_stop/session_cancel` 语义

后端应尽量依赖第二层，而不是绑定第一层。

## 当前建议冻结的输入契约

### 音频格式

- 编码：`PCM`
- 字节序：`little-endian`
- 采样率：`16 kHz`
- 声道：`mono`
- 位宽：`16-bit`

当前基础分片量级：

- `20 ms`
- `320` 样本
- `640 bytes`

对后端的实际含义是：

- 可以按 `16k mono 16-bit PCM` 直接做流式识别输入
- 不需要先等待完整 `wav` 文件生成
- 不要把当前输入当作压缩音频流

### 会话语义

当前推荐后端按下面的上层语义建模：

`session_start -> audio_data(packet_sequence...) -> session_stop`

异常结束时：

`session_start -> audio_data(packet_sequence...) -> session_cancel`

当前含义：

- `session_start`
  一次新的录音会话开始
- `audio_data`
  会话内连续音频包
- `session_stop`
  会话正常结束，并携带本次期望总包数
- `session_cancel`
  会话异常取消，也会携带本次期望总包数

### 核心字段语义

当前主机侧和设备侧已经稳定依赖这些字段语义：

- `session_id`
  区分一次完整录音会话
- `packet_sequence`
  会话内顺序包编号，从 `0` 连续递增
- `expected_packet_count`
  当前会话应有的总包数，由 `session_stop / session_cancel` 给出
- `payload_len`
  当前包有效负载长度
- `chunk_pcm_bytes`
  当前包对应的有效 `PCM` 字节数

当前 wire layout 还保留旧字段名兼容，但后端不要再按旧语义理解：

- `chunk_index`
  现在等价于 `packet_sequence`
- `fragment_index`
  当前阶段固定为 `0`
- `fragment_count`
  当前阶段固定为 `1`

也就是说，当前已经不是旧的 `chunk + fragment` 模型。

## 主机重组与后端输入口径

当前 Windows 主机重组规则是：

- 以 `session_id` 为主键聚合
- 按 `packet_sequence` 顺序重组
- 以 `expected_packet_count` 判断会话完整度
- 缺失包补静音，优先保留时间轴完整性

当前主机侧会输出这些重要统计字段：

- `received_packet_count`
- `expected_packet_count`
- `missing_packet_count`
- `missing_packet_indices`
- `received_pcm_bytes`
- `duration_seconds`

对后端更推荐的输入口径是：

- 接收一个明确开始和结束的音频会话
- 每个会话内部按顺序消费 `PCM` 数据
- 如果中间丢包，允许由前端或中转层补静音并上报缺失统计

不建议后端直接依赖：

- `BLE` 连接生命周期事件
- `WinRT` 的具体异常字符串
- 当前主机工具脚本里的历史兼容字段名

## 成功 / 失败口径

### 当前可视为成功的联调口径

一轮会话当前至少应满足：

- 收到 `session_start`
- 收到 `session_stop`
- 能输出完整 `wav` 或等效 `PCM`
- `duration_seconds` 与实际录音时长大体一致
- `missing_packet_count` 可统计

当前最近一次物理按键成功基线：

- `received_packet_count=1524`
- `expected_packet_count=1524`
- `missing_packet_count=0`
- `received_pcm_bytes=650240`
- `duration_seconds=20.320`

这说明：

- 单轮主链路已经可用
- 当前音频格式和会话模型已经足够支撑后端联调

### 当前不应误判为产品级完成的部分

下面这些场景当前还没有完全收敛：

- `P2` 多轮连续回归
- `P4` 恢复场景
- `P5` 重连场景
- 长时间空闲后的首句输入
- 主机侧接收进程重启后的继续录音
- 取消 / 超时 / 异常结束后的下一轮恢复
- 长时间 soak 与弱环境稳定性

最近一次回归结论：

- `P2`
  第 1 轮通过，但第 2 轮出现高丢包，最近一次实测 `packet_loss_ratio=0.5178`
- `P4 / P5`
  最近一次失败点集中在串口 / 设备重新枚举后的 `COM3` 重新打开不稳定

所以当前应把结论写成：

- `BLE 单轮录音上传链路已可联调`
- `BLE 多轮、恢复、重连稳定性仍在产品化收敛中`
- `产品级使用面测试矩阵已切到 P 系列，当前不能把旧 R1-R5 等同于完整产品级覆盖`

## 超时与错误处理建议

后端或中转层现在就应该冻结这些处理原则：

- 所有抓音、上传、回归脚本必须带明确时间预算
- 超过预算应按 `timeout` 处理，而不是无限等待
- 会话超时应区分：
  - 未收到 `session_start`
  - 已开始但未收到 `session_stop`
  - 已结束但包数不完整

当前更推荐保留的错误信息类别：

- `timeout`
- `transport_loss`
- `session_cancelled`
- `reconnect_required`
- `device_unavailable`

不建议后端直接把当前平台相关错误原样固化为协议级错误码，例如：

- `OSError(22)`
- `ClearCommError failed`
- `FileNotFoundError(2, '系统找不到指定的文件。')`

这些更适合作为诊断日志，而不是正式业务协议语义。

## 当前保证与不保证

### 当前可以依赖的部分

- 音频输入格式：`16k / mono / 16-bit / PCM little-endian`
- 上层会话模型：`session_start -> audio_data -> session_stop/session_cancel`
- 会话内顺序编号语义：`packet_sequence`
- 会话结束总包数字段：`expected_packet_count`
- 单轮会话从设备到主机的基本传输主干

### 当前不要假设已经成立的部分

- 多轮连续录音一定稳定
- 恢复后第一次抓音一定稳定
- 断开重连后自动回归一定稳定
- `BLE` 承载一定是最终产品的唯一传输层

## 面向后端的接口建议

如果后端现在要开始对接，建议按“语音输入前端输出契约”建模，而不是按“BLE 包协议”建模。

建议至少冻结这些输入字段：

- `protocol_version`
- `device_id`
- `session_id`
- `audio_format`
- `sample_rate_hz`
- `channels`
- `sample_width_bits`
- `packet_sequence`
- `expected_packet_count`
- `payload_pcm_bytes`
- `is_final`
- `session_end_reason`

其中：

- `audio_format`
  当前建议固定为 `pcm_s16le`
- `is_final`
  表示该会话是否已经进入最终结束状态
- `session_end_reason`
  当前建议至少区分：
  - `stop`
  - `cancel`
  - `timeout`
  - `transport_error`

正式后端协议文档仍应由后端维护，但建议以上层契约为冻结基线。

## 未来切到 Wi-Fi 时的复用原则

未来如果从 `BLE` 切到 `Wi-Fi` 传音频到后端，建议：

- 保留当前上层会话语义
- 保留当前音频格式契约
- 保留 `session_id / packet_sequence / expected_packet_count` 这些上层字段含义
- 只替换底层承载

也就是说，推荐复用的是：

- 会话模型
- 音频格式
- 成功 / 失败口径
- 超时与重试原则

不一定需要原样复用的是：

- `BLE notify` 本身
- 历史兼容字段名，如 `chunk_index / fragment_index / fragment_count`
- 当前 Windows 抓音脚本的诊断输出格式

如果未来走 `Wi-Fi`，更适合的封装通常会是：

- 元信息 + 二进制音频负载
- 或显式版本化的消息结构

不建议继续背着只为 `BLE` 兼容留下的线协议包袱。

## 当前项目建议

从当前产品目标看，建议这样推进：

- `BLE` 继续作为当前 bring-up 和前端联调主路径
- 后端现在就基于这份输入契约开始联调
- 后端正式协议文档由后端维护，但应以这里的音频和会话语义为冻结边界
- 后续如果切 `Wi-Fi`，优先迁移承载层，不推翻会话层

一句话总结：

当前真正应该冻结给后端的，不是 `BLE` 本身，而是 `16k PCM + 明确会话边界 + 顺序包重组 + 超时/结束语义` 这一层契约。

## 关联文档

- [设备端音频采集、录音控制与 BLE 上传链路](./audio_capture_ble_upload.md)
- [产品方案](../product_solutions.md)
