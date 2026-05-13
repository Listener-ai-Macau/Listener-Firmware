# Windows 主机侧 Unicode 文本注入方案

## 目标

先不依赖真实语音输入，先把“主机收到一段字符串后，稳定把这段字符串输入到 `Windows` 当前输入框”这条链路打通。

当前最小目标是：

1. 先用本地 mock 的“后端返回文本”代替真实语音识别结果
2. 先验证中文 `测试` 能稳定进入 `Windows` 输入框
3. 再把这条注入链路扩展成“后端返回什么字符串，就输出什么字符串”
4. 最后再把设备侧语音输入和真实识别结果接进来

一句话说，这次先验证的是：

`任意 Unicode 字符串 -> Windows 主机侧注入 -> 当前输入框实际收到文本`

而不是：

`语音输入 -> 识别 -> 中文输出`

## 当前问题

当前仓库已经完成的稳定能力主要是：

- `ESP32-S3` 上的 `BLE HID` 键盘链路
- `NimBLE` 自动回连
- 固件侧 ASCII / 键码路径验证

但这条链路不适合作为最终中文输出主方案，原因是：

- 标准 `BLE HID` 更接近按键码，不是高层 Unicode 文本提交接口
- 即使某些中文输入法场景能“间接打出中文”，也不等于“后端返回什么字符串，就稳定输出什么字符串”
- 对“类似闪电说”的目标来说，最终需要的是主机侧对任意 Unicode 文本的稳定注入能力

所以当前真正要先搞定的，不是继续扩 `BLE HID` 发键逻辑，而是：

- 在 `Windows` 主机侧建立一条正式的 Unicode 文本注入路线

## 范围

- 明确 `Windows` 主机侧中文 / Unicode 文本注入的可行技术路线
- 选择一个最适合当前 MVP 的方案
- 定义“mock 后端返回 `测试` -> 输入框收到 `测试`”的最小验证链路
- 定义后续与设备侧语音输入、后端返回文本的接口边界

## 不在本次范围内

- 真实语音采集
- 真实语音识别
- 云端协议冻结
- 上下文理解
- 技能执行
- 产品级输入法兼容性穷举

## 前提与依赖

- 当前主机为 `Windows`
- 当前最终产品方向参考：
  [shandianshuo_like_embedded_voice_assistant_plan.md](./shandianshuo_like_embedded_voice_assistant_plan.md)
- 当前如果需要保底输入链路，仍可复用：
  [ble_hid_keyboard_input.md](../features/ble_hid_keyboard_input.md)
- 当前先默认主机侧 MVP 可以放在本仓库的 `tools/` 目录下，后续如果主机能力持续扩大，再考虑拆分独立仓库

## 现成方案 / 先例检查

这类事情以前并不是没人做，当前最值得直接参考的现成路线有三类：

### 1. Win32 官方路线：`SendInput + KEYEVENTF_UNICODE`

这是 `Windows` 官方 API 路线，适合做真正的主机侧注入器。

已确认的官方信息：

- `SendInput` 用于合成输入事件
- `INPUT_KEYBOARD` 支持 `KEYEVENTF_UNICODE`
- 这条路可以把文本按 Unicode 方式送进前台输入流

适合当前用途：

- 做自己的最小主机注入器
- 后续接“后端返回任意字符串 -> 注入当前输入框”

### 2. 现成成熟工具路线：`AutoHotkey`

这是目前最不该忽视的现成轮子之一。

从公开文档可确认：

- `Send` / `SendInput` / `SendEvent` 能发送文本和按键
- `SendText` 明确就是面向“按文本发送”的现成能力
- Unicode 发送已经是成熟常见用法

适合当前用途：

- 先做最小验证原型
- 先验证 `测试` 能不能稳定进当前目标输入框
- 如果效果足够好，甚至可以先把它作为 MVP，而不是马上自写 Win32 注入器

### 3. 现成桌面自动化路线：`Power Automate Desktop`

这是微软现成的桌面自动化产品路线。

官方文档表明：

- 它有 `Send keys` 动作
- 能向前台窗口或目标窗口发送文本
- 还能控制焦点和窗口定位

适合当前用途：

- 快速验证某些应用场景的可行性
- 做非开发同学也能复现的桌面自动化原型

不太适合作为当前主产品注入核心，原因是：

- 自动化工具味道更重
- 分发、集成和产品化不如一个轻量主机伴随程序直接

## 当前先例结论

当前不应该从“完全自写一套全新注入方案”起步。

更合理的顺序应该是：

1. 先用现成轮子做最小验证
2. 能直接复用就优先复用
3. 只有在现成方案不满足产品边界时，再补自定义实现

对当前任务，推荐顺序是：

1. 先试 `AutoHotkey` 的文本发送能力
2. 再试是否需要原生 `Win32 SendInput` 小工具
3. `Power Automate Desktop` 作为补充参考，不作为首选产品内核

## 核心判断

### 一、当前主路不应继续押在 BLE HID 直接出中文

当前如果目标是：

- 说一句中文“测试”，最终输入框里就是 `测试`
- 后端返回什么字符串，就尽量原样进入当前输入框

那么最合适的控制点不是固件侧 HID，而是 `Windows` 主机侧注入。

设备侧更适合负责：

- 语音键
- 音频采集
- 与主机通信

主机侧更适合负责：

- 接收文本结果
- 处理 Unicode 文本注入
- 处理焦点、权限、兼容性和 fallback

### 二、当前最适合的“产品内核主路”是 `SendInput + KEYEVENTF_UNICODE`

根据 Microsoft 官方 Win32 文档：

- `INPUT` / `SendInput` 可以合成键盘输入
- `INPUT_KEYBOARD` 支持使用 `KEYEVENTF_UNICODE` 做“像文本输入一样”的非物理键盘输入
- `KEYEVENTF_UNICODE` 路径会向前台线程消息队列发送 `VK_PACKET`
- `SendInput` 受 `UIPI` 约束，只能注入到完整性级别相等或更低的目标应用

这条路线当前最适合做 MVP 的原因是：

- 直接面向 Unicode 文本，不依赖当前输入法布局
- 不要求把文本先翻译成物理按键序列
- 不污染系统剪贴板
- 很适合先用 mock 文本做验证

当前推荐结论：

- 现成原型优先：`AutoHotkey SendText / SendInput`
- 产品内核主路：`SendInput + KEYEVENTF_UNICODE`
- fallback：剪贴板 `CF_UNICODETEXT` + 粘贴

### 三、剪贴板粘贴适合作为兼容 fallback，不适合作为唯一主路

剪贴板路线的好处：

- 对很多标准文本框兼容性高
- 遇到某些应用不接受 `VK_PACKET` 时，可能更容易成功

但缺点也很明显：

- 会污染用户当前剪贴板
- 需要依赖 `Ctrl+V` 或等价粘贴行为
- 在某些终端、远程桌面、游戏或自定义控件里不一定稳定

所以当前更合理的策略不是二选一，而是：

- 优先尝试 `SendInput + KEYEVENTF_UNICODE`
- 如果目标应用不接受，再切剪贴板粘贴 fallback

### 四、深度 IME / TSF 集成当前不适合做第一步

更深的 `IME / TSF` 集成理论上可以做得更强，但当前阶段不适合先上，原因是：

- 复杂度明显更高
- bring-up 成本太高
- 当前目标只是先证明“任意 Unicode 文本能稳定进入输入框”

所以第一版先不走 `IME / TSF` 深度集成。

## 推荐 MVP 结构

当前建议先做一个最小主机侧注入器，逻辑顺序如下：

1. 本地 mock 一个“后端返回文本”
2. 先固定使用：
   - `测试`
   - `测试abc123`
3. 主机侧注入器把这段字符串送到当前前台输入框
4. 优先走 `SendInput + KEYEVENTF_UNICODE`
5. 如果失败，再走剪贴板 fallback

最小数据流建议是：

`mock 文本源 -> Windows 主机侧注入器 -> 前台输入框`

而不是一开始就做：

`设备语音 -> BLE/Wi-Fi -> 主机 -> ASR -> 注入`

## 实现建议

### 方案 A：先用 `AutoHotkey` 做最小原型

当前最不容易造轮子的做法是：

- 先用 `AutoHotkey` 做一版最小原型
- 先验证中文 `测试` 和混合文本 `测试abc123`
- 先验证当前目标输入框对这类文本注入的真实兼容性

推荐原因：

- 这是成熟现成工具
- 实验成本最低
- 很适合快速筛掉“目标应用根本不接受这类注入”的情况
- 如果效果足够好，短期甚至不一定需要马上自写注入器

### 方案 B：Windows 控制台注入器

如果 `AutoHotkey` 原型验证通过，但产品上仍不希望依赖它，下一步再做自己的最小主机注入器。

当前推荐的最小实现是：

- 在 `tools/` 下做一个 `Windows` 专用小工具
- 输入参数至少支持：
  - `--text`
  - `--mode sendinput`
  - `--mode clipboard`
  - `--mode auto`
- `auto` 先尝试 `sendinput`，失败时再提示切 `clipboard`

推荐原因：

- 最容易先打通
- 最适合做 mock 验证
- 后面也容易被“后端返回文本”或“设备转发结果”复用

### 方案 C：先做一个验证脚本，再决定是否升级成常驻伴随程序

当前不一定要一开始就做常驻托盘程序。

更轻的顺序是：

1. 先做一次性注入工具
2. 验证稳定后
3. 再升级成真正的主机伴随程序

这样更符合当前 bring-up 阶段。

## 步骤

### 第一步：确认主机侧 Unicode 注入技术路线

状态：`completed`

工作内容：

- 检查现成方案是否已足够：
  - `AutoHotkey`
  - `Power Automate Desktop`
  - Win32 官方 `SendInput`
- 对比 `SendInput + KEYEVENTF_UNICODE`
- 对比剪贴板 `CF_UNICODETEXT + 粘贴`
- 明确当前 MVP 不继续押注 `BLE HID` 直接做中文输出
- 定义主路和 fallback

本步结论：

- 当前中文输出应主要由 `Windows` 主机侧承担
- 当前不应忽视 `AutoHotkey` 这类成熟现成工具
- 当前最适合作为产品内核主路的是 `SendInput + KEYEVENTF_UNICODE`
- 当前建议保留剪贴板粘贴作为 fallback
- 当前不建议先做深度 `IME / TSF` 集成

验收方式：

- 本文档明确写出主路、fallback、适用原因和限制

人工检查点：

- 人确认当前先做 `Windows` 主机侧 Unicode 注入，而不是继续扩 `BLE HID` 中文路径

### 第二步：实现最小主机侧 mock 文本注入器

状态：`pending`

工作内容：

- 先做最小现成原型验证，不急着自写工具
- 优先尝试：
  - `AutoHotkey`
  - 必要时再补原生 `Win32` 注入器
- 先不接设备，不接语音，不接真实后端
- 先用固定字符串：
  - `测试`
  - 或 `测试abc123`
- 优先验证现成方案是否已经满足需求
- 如果现成方案不够，再实现 `SendInput + KEYEVENTF_UNICODE`
- 根据需要补一个剪贴板 fallback
- 保留必要日志，明确本次用了哪条注入路径

验收方式：

- 在 `Windows` 标准输入框中，运行原型后可稳定出现 `测试`
- 记录本次使用的是：
  - `AutoHotkey`
  - 原生 `Win32`
  - 或剪贴板 fallback
- 若首选路线失败，能够明确给出失败信息或切换依据

人工检查点：

- 人确认当前主机工具形态和验证结果可接受，AI 才进入第三步

### 第三步：定义“后端返回字符串 -> 主机注入”的稳定接口

状态：`pending`

工作内容：

- 把第二步的注入工具整理成稳定接口
- 明确后续最小输入方式，例如：
  - 命令行参数
  - 标准输入
  - 本地 socket / named pipe
- 明确字符串编码要求
- 明确取消、重试和 fallback 处理策略

验收方式：

- 文档或工具接口中明确写出：
  - 输入方式
  - 文本编码
  - 成功 / 失败标记

人工检查点：

- 人确认“后端返回啥字符串，我们就输出啥字符串”的接口边界可接受，AI 才进入第四步

### 第四步：把主机侧 Unicode 注入路线接回整体产品路径

状态：`pending`

工作内容：

- 在 roadmap 中把这条路线放到语音输入之前
- 明确后续：
  - 设备侧负责语音与传输
  - 主机侧负责 Unicode 注入
- 明确后端或 mock 返回文本如何复用当前主机注入器

验收方式：

- 相关 roadmap / 总方案文档与当前优先级一致
- 文档中明确“先中文输出、后语音输入”的顺序

人工检查点：

- 人确认整体顺序后，AI 才开始主机工具实现

## 当前阻塞项

- 还没有真正的 `Windows` 主机侧注入工具实现
- 还没有验证 `SendInput + KEYEVENTF_UNICODE` 在当前目标输入框上的实际表现
- 还没有决定主机工具先用哪种实现语言

## 当前建议

按你刚刚确认的新优先级，当前最合理的顺序是：

1. 先完成 `Windows` 主机侧 Unicode 文本注入
2. 先用 mock 文本 `测试` 做验证
3. 再做语音输入
4. 后续只要后端返回任意字符串，就直接复用当前主机注入链路

## 参考资料

- Microsoft Learn: `SendInput`
  https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput
- Microsoft Learn: `INPUT`
  https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-input
- Microsoft Learn: `KEYBDINPUT`
  https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-keybdinput
- Microsoft Learn: `Clipboard`
  https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard
- Microsoft Learn: `Clipboard Formats`
  https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats
- AutoHotkey documentation: `Send / SendInput / SendText`
  https://www.autohotkey.com/docs/
- Microsoft Learn: `Power Automate Desktop - Mouse and keyboard actions`
  https://learn.microsoft.com/en-us/power-automate/desktop-flows/actions-reference/mouseandkeyboard
