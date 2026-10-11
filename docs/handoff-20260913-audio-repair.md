# 2026-09-13 音频与唤醒修复候选

## 最新：有线安装成功

用户明确指定有线后，通过 tools/flash.ps1 -Port COM5 -NoBuild -PreserveOtaData 刷入连续监听候选，退出码 0，写入校验与 OTA 启动槽核对成功。当前 build_id=cefb802eed6e36d1，bin SHA-256 5920782cf962b4d40e73cb02120583d27e95aa668d2884a6ca409e7c60521b86，Type 仍 73D0869D。旧「固件未安装」状态已过时，以下作为历史保留。

六轮新实机详见 Type docs/architecture/wired-drain-check-2026-09-13.md。原背景后唤醒失败素材现能开录，尾包期间监听未关闭、DMA=0，但仍漏「今天」；本人另一轮自动短句仅输出「1」，同一正文手动开始能完整输出。5 个正例正文 3 通过 / 2 失败，另一个无口令负例无输出。正常输入恢复、调试关闭；A–G 及蓝牙升级尚未全部验收，不放行发布。

最新补充：Type 已安装 73D0869DC317CD9648D8C22326D12FC3EEEC095F7E3C2EB5E048DD2EAC321DF9，固件仍 d592，连续监听候选未安装。新实机 clean-1（Type session 63aedd36）在旧隐藏候选结束后 monitoring 关闭 1510 ms，ACTIVATE 因 idle 被拒；最终正文缺开头，归档 WAV 本地识别同样缺该段，详见 Type docs/architecture/wake-intent-check-2026-09-13.md。用户再次要求代理自己执行蓝牙 OTA 后，同一路径再试仍被执行工具在进程创建前以 blocked by policy 拒绝，无具体原因；没有刷入或改走有线。Type 日常输入已恢复，调试录音及 9222 关闭。下文旧 Type 哈希均为历史状态。

补充验证（Luna 执行、主代理审查）：监控测试现在直接抽取真实 BLE 状态映射和 is_type_link_ready 生产函数，覆盖 DRAINING 继续监听、心跳过期关闭及恢复、DISCONNECTED 关闭。编译与测试通过，退出码 0；未改产品逻辑或刷机。Windows 临时目录清理采用尽力而为，真实编译和测试失败仍抛错。

## 13:47 待安装候选：尾包发送期间保留唤醒监听

已构建但**未刷入**。执行工具在进程启动前按策略拒绝了通过已安装 Type 执行 BLE OTA 的组合命令，仅返回 `blocked by policy`，没有更详细原因；没有改走串口或重试升级。当前键盘仍为下文 `d592fff3e2e09bd0`，Type 已安装 `008367353BA6DCBFD29DEB07639222171D254209D33319FEAD623BCCCEA43435`，正常输入设置已恢复，9222 关闭。

真实失败 `terminal-reject-overlap`：无效语音 4.3 秒 + 0.7 秒间隔 + 已验证的完整唤醒/正文，在隐藏候选 max-duration 后没有唤醒。串口 9438932–9440512 ms 关闭 monitoring，9439232–9440512 ms 进入 audio idle power-save；下一段预录仅 1180 ms。关闭监听使省电路径关闭 I2S 并清空环形预录，跨段唤醒词不能靠“保留旧缓存”恢复。

修复将 IDLE/TRANSFERRING 的自动监听条件从“可立即启动新流”改为“Type 连接仍有效且心跳新鲜”。真正开始录音仍走原 stream-ready 检查；ACTIVE 电源、OTA 排除、自动唤醒开关和手动停止语音监控条件保持。没有添加电源阻塞或重置空闲计时。

`tools/test_voice_monitoring_continuity.py` 编译生产函数，验证录音→传输→空闲不关闭监听，同时覆盖真正低功耗、断连、OTA 两种入口、关闭自动唤醒、手动录音、声纹登记及捕获层被独立省电后的重同步，已加入 release_check。该行为测试、voice-recording FSM、charging-awake 静态检查、OTA 回归检查与固件构建均通过。新固件尚无实机验收，不能宣称唤醒空窗已修好。

开发 OTA：`.cache/repair-20260913-drain-monitor/listener-ota-1.0.5-20260913-134543.zip`，ZIP SHA-256 `16AE2E44B4548D0301D93D28FC34147017C9900DD718E3D1AD3AF651A8351854`。固件二进制 1701216 字节，SHA-256 `5920782cf962b4d40e73cb02120583d27e95aa668d2884a6ca409e7c60521b86`。版本 1.0.5，development 渠道，不上传稳定更新渠道。

安装后先核对编译身份，再原样重放 `unmatched-prefix-then-wake.wav`；检查排空窗口不再出现 microphone power-save、预录完整、新候选唤醒和正文完整。随后重跑正常长句、约 1.5 秒句中停顿、手动结束、断连/OTA/真正低功耗。原失败保留在任务目录，不能只换素材验收。

## 后续候选（覆盖下文早期版本状态）

当前已刷入 `d592fff3e2e09bd0`，1.0.5，1701200 字节，SHA-256 `01d62ffb6d0489e0bc487e167436767c1ffdd6458fca29bb1625a67ce8a98407`。OTA 包 `.cache/repair-20260913-preview-pacing/listener-ota-1.0.5-20260913-105155.zip`，安装版 Type 确认成功；OTA 31161 ms，传输 25318 ms，重连 1005 ms。

新增修复：小包的主动让步计入同一媒体速率预算，避免在 38.4 KB/s 限流外逐包再等一个 tick。实际旧吞吐 28229 B/s、STOP 后积压约 5.6 秒；新实机 34208 B/s，完整 80 字最终结果、DMA=0、缺包=0。`tools/test_audio_pacing.py` 编译生产 C 实现，覆盖 30 组小包与混合包，检查总等待和有界让步，已加入 release_check。手动另一轮仍有 2880 字节 DMA 溢出，不宣称所有收音场景零丢失。

Type 后续证据见 `Listener-Type/docs/architecture/preview-and-live-owner-2026-09-13.md`。G 仍失败；Billy 已有一段现场自然说话结果，开头误识别，未计完全通过；真实拔线项目未完成。以下保留早期候选记录，其哈希与环境恢复状态仅适用于当时阶段。

当前开发固件 1.0.5，build_id `bda62bd598407581`，OTA binary SHA-256 `5cd032e51e967e50ff8644f964052ceacfa8464f005d7cb4b31d63f79eaf4d22`。经安装版 Type 完成实机 BLE OTA，1701184 字节，确认及重连成功，尚未公开发布。

- 隐藏候选保留 4 秒有界窗口，防止主机 ACTIVATE 到达前就因 1 秒静音退回 idle。只改隐藏候选的最短会话，手动/可见结束参数不变。实际 C 状态机测试在 `tools/test_hidden_wake_window.py`。
- 增加 I2S 接收队列溢出字节及生产端压缩规划耗时统计。旧实机 wall=22561 ms、PCM=20280 ms、DMA overrun=15360 bytes，规划 CPU=4.632 s。旧传输 audio_drops=0 并不覆盖这类 DMA 损失。
- 压缩规划回退从密集线性试探改为有界减半搜索；仍逐包编码并校验容量，共享规划路径保留发送/计数一致性。真实录音对比编码尝试 8634→3635，包数 1808→1853，wire bytes 329575→329800；PCM 全部逐字节还原。新实机 DMA=0。测试 `tools/test_audio_packet_planner.py` 覆盖 codec 1/2/3、payload 20/224/480。
- 关闭 WebRTC NS，保留神经 VAD、单一 AGC、增益治理和限幅。同一失败素材、同样外放与 Type，NS 开启多次为空白/一句，关闭后实机恢复 79 字。不能把这一场景等同于所有噪声环境已经通过。
- 电源沿用本轮过滤外部供电、充满锁存纠正及关机前复核；用户原始 board、电源与拔线工作保留。充电显示矛盾已有实机修复前后对照，真实拔线低功耗/自动关机尚待现场动作。`tools/test_power_source_filter.py` 验证实际 C 过滤行为。

Type 对应候选及 A–G 验收边界见 Listener-Type 的 `docs/architecture/repair-evidence-2026-09-13.md`。不得把部分静态检查、构建成功或合成音回放称作全部产品验收。完整 release_check 要求工作树洁净，发布前应完成现场验收并整理提交；不要丢弃用户未提交工作来满足这个检查。

## 最终补测与环境恢复

Type 40D52C44 最新安装版连续正文再次输入 80 字；23.22 秒正常电平背景谈话（原素材增益 3 倍、无削波）两个候选被拒，随后唤醒成功并输入完整 80 字，首正文预览距唤醒素材起点 4156 ms。G 的真人二次外放及两组合成干扰对照仍失败，未宣布验收通过。本人现场和真实拔线项目待配合。已恢复 recordAudioForDebug=false 并读回确认，关闭本任务测试窗口，正常重启安装版 Type，runtime gate 通过。
