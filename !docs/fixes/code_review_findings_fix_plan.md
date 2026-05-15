# Code Review 关键发现修复计划

## 状态

- 状态：`待验证`
- 关联：code review 关键发现

## 目标

修复 code review 发现的中等级别问题，提升代码健壮性。

## 当前问题

| # | 文件 | 问题 |
|---|------|------|
| 1 | `ble_audio_stream_esp32.c` | `_locked` 后缀函数实际没持锁，命名误导 |
| 2 | `ble_hid_gap_esp32.c` | `assert(rc == 0)` 在 REPEAT_PAIRING 中可能 crash 设备 |
| 3 | `ble_audio_stream_esp32.c` | 重试循环不检测断链，空转最多 80 秒 |
| 4 | `capture_audio_ble_wav.py` | warmup fallback 路径跟文档顺序相反，缺注释 |
| 5 | `capture_audio_ble_wav.py` | ValueChanged 回调线程安全问题 |
| 6 | `capture_audio_ble_wav.py` | PowerShell 命令注入风险 |
| 7 | `audio_capture_esp32.c` | `audio_capture_start()` 部分失败时资源泄漏 |
| 8 | `tools/capture_audio_wav.py` 及其包装脚本 | 仍依赖已删除的 `~ACAP` / `PCM64` 固定导出链路 |
| 9 | `.codex/skills/voice-keyboard-firmware/SKILL.md` | 指向不存在的 `references/start_here.md`，仓库入口漂移 |
| 10 | `capture_audio_ble_wav.py` | 首轮 `notify` 建链在明显失败场景下仍常白等满 `8s`，抬高物理按键与回归等待成本 |

## 步骤

### 步骤 1：修复设备端 C 代码（问题 1-3, 7）

改动范围：
- `ble_audio_stream_esp32.c`：重命名 `_locked` 函数去掉误导后缀；重试循环加断链检测
- `ble_hid_gap_esp32.c`：`assert` 改为软错误处理
- `audio_capture_esp32.c`：`audio_capture_start()` 加部分失败清理

验收：`idf.py build` 构建通过

### 步骤 2：修复主机端 Python 代码（问题 4-6）

改动范围：
- `capture_audio_ble_wav.py`：warmup fallback 加注释说明；SessionCollector 加线程安全（`threading.Lock`）；PowerShell 命令参数转义

验收：`python -m py_compile` 语法检查通过；`--help` 正常输出

### 步骤 3：运行 R1 回归验证无回归

验收：`python tools/verify_audio_ble_upload_end_to_end.py --port COM3 --capture-seconds 5 --no-reset-before-capture` 结果为 pass

### 步骤 4：收口过时工具入口与仓库 skill 入口（问题 8-9）

改动范围：
- `tools/capture_audio_wav.py`：不再使用已删除的固定导出协议，改为复用当前 BLE 会话抓音链路
- `tools/capture_audio_wav.ps1` / `tools/capture_audio_session_wav.ps1` / `tools/verify_audio_capture_session_end_to_end.py` / `tools/verify_audio_capture_session_end_to_end.ps1`：统一切到当前 `capture_audio_ble_wav.py` 能力面
- `.codex/skills/voice-keyboard-firmware/SKILL.md`：不再引用不存在的入口
- `references/start_here.md`：补上仓库真实快速入口

验收：`python -m py_compile` 相关 Python 脚本通过；包装脚本的 `--help` 或参数透传能落到当前链路；skill 引用路径存在

### 步骤 5：优化主机侧 notify 建链耗时（问题 10）

改动范围：
- `capture_audio_ble_wav.py`：缩短明显异常时的首轮 `CCCD` 等待；把更快的 fallback / retry 路径前置到不影响正确性的范围内；保留当前成功路径和订阅顺序约束
- 如有必要：同步 `!docs/features/audio_capture_ble_upload.md` 中的主机侧行为说明

验收：`python -m py_compile` 通过；物理按键或 `R1` 抓音时仍能成功生成 `wav`；首轮 `notify_enable_primary_timeout=1` 的等待成本下降或日志能明确显示更快进入 fallback

## 不修的项（记录原因）

- **export packet count mismatch**：EXPORT 路径（`ble_audio_stream_send_export`）无活跃调用者，SESSION 路径无此问题
- **endian assumption**：ESP32 + x86 都是 LE，产品范围内不构成风险
- **fragment_index/fragment_count parsed but unused**：协议兼容字段，保留解析是正确的

## 人工检查点

- 步骤 1 完成后：构建通过 → 批准进入步骤 2
- 步骤 2 完成后：语法检查通过 → 批准进入步骤 3
- 步骤 3 完成后：回归结果 review → 完结
- 步骤 4 完成后：工具入口与仓库入口 review → 完结
- 步骤 5 完成后：主机侧建链耗时与无回归 review → 完结
