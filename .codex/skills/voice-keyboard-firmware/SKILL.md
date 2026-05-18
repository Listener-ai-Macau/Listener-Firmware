---
name: voice-keyboard-firmware
description: Firmware repo adapter. Three agents collaborate, all roles equal, whoever is free picks up work.
---

# Voice Keyboard Firmware

## 协作

三个 AI（Codex / Tai / Claude）角色平等，谁闲谁做。任务由人工分配或空闲者认领。

## 【硬约束】开工前必做

1. **读状态**：`cat C:\Users\Billy\Desktop\listener\docs\plans\*_status.json`
2. **确认无人占用**你要做的步骤（status != in_progress 或 assignee == 你）
3. **认领**：`powershell -File C:\Users\Billy\Desktop\listener\voice-keyboard-firmware\tools\update_plan_status.ps1 -Plan p13 -StepId N -Status in_progress -Assignee <你>`
4. **硬件加锁**：`powershell -File C:\Users\Billy\Desktop\listener\voice-keyboard-firmware\tools\lock_resource.ps1 -Resource COM3 -Owner <你>`

以上必须在开始编码前完成。不做就开工导致冲突的，由冲突方负责修复。

## 工作流

1. 确认身份：先看启动器注入身份（Tai 会有 `AI_AGENT_ID=Tai` / `TAI_AGENT_ID=Tai`），再用工作目录路径校验：`codex-p13` = Codex，`tai-p13` = Tai，`claude-review-p13` = Claude
2. 读协议：`!docs/ai_collaboration_protocol.md`
3. 读状态：`C:\Users\Billy\Desktop\listener\docs\plans\*_status.json`
4. 读参考文档：`C:\Users\Billy\Desktop\listener\docs\plans\*.md`
5. 找到分配给自己或 unassigned 的步骤
6. **硬约束**：认领 → `update_plan_status.ps1` 标记 assignee + in_progress
7. **硬约束**：硬件 → `lock_resource.ps1` 获取锁
8. 只在自己的 worktree 内工作
9. 完成后 `update_plan_status.ps1` 标记 completed，清空 assignee
10. 硬件用完 `unlock_resource.ps1` 释放锁
11. 遇到人工阻塞标记 blocked + blocked_reason
12. 推送 origin 前执行推送前审核 gate，通过后直接推送，不再默认等待人工确认

## 推送前审核 gate

push 类步骤不算人工阻塞。执行前必须检查：

1. 共享状态：目标步骤已由自己认领，且相关前置步骤 completed
2. 分支：目标本地分支没有 behind，推送目标明确
3. 待推提交：列出将推送的 commits，确认不包含临时 agent 分支
4. 未提交改动：确认不会把未提交改动混入 push；若存在，只报告并隔离
5. 基础验证：执行该步骤已有验收或轻量检查
6. secret scan：确认用户提供的 token/API key 未写入仓库

只有物理操作、人工决策、凭据缺失等必须人类参与的事项才标记 blocked。

## 【硬约束】写计划必须遵守

任何 AI 写计划时必须按以下格式，确保步骤可并行分工：

### JSON 状态

```json
{
  "plan": "<name>",
  "phases": [
    {
      "id": 1,
      "title": "阶段名",
      "status": "completed | in_progress | pending",
      "steps": [
        {
          "id": "1.1",
          "title": "步骤名",
          "repo": "firmware | Listener-Type",
          "status": "pending | in_progress | completed | blocked",
          "assignee": null,
          "parallel_group": "A"
        }
      ]
    }
  ]
}
```

### 规则

1. **分阶段**：不同阶段串行，同阶段内步骤尽量并行
2. **parallel_group**：同组可同时做，不同组互不依赖
3. **只追踪未完成**：已完成阶段折叠一行，不展开步骤
4. **步骤粒度**：单次会话可完成并验收的大小
5. **repo 标注**：每个步骤必须标注涉及哪个仓库

## 仓库约束

### 目录边界

- `main/`：精简的 app_main 和初始化
- `components/`：跨平台产品逻辑
- `protocols/`：协议、编解码、错误码
- `drivers/`：语义化外设驱动
- `ports/esp32/`：ESP-IDF 绑定
- 不新建 `services/`、`platform/`、`common/`、`misc/`

### 命名

- 目录、文件、函数、变量：`snake_case`
- 公开函数加模块前缀：`keyboard_start()`、`ble_hid_init()`

### 构建

```powershell
idf.py build
idf.py flash
python .\tools\capture_audio_ble_wav.py --port COM3 --capture-seconds 5
python .\tools\verify_audio_ble_product_matrix.py --port COM3 --capture-seconds 5 --long-capture-seconds 30 --round-count 3 --fail-on-warning
```

### 关键不变量

- 不重解释 `audio_data` 为旧的 `chunk + fragment` 模型
- 不改变 host 订阅顺序：`CCCD notify -> ValueValueChanged`
- 不移除 `subscribe` 先于 `connect` 到达的兼容性
