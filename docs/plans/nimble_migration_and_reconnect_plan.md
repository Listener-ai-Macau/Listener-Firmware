# BLE HID NimBLE 迁移与自动回连修复计划

## 目标

把当前 `ESP32-S3` 上的 BLE HID 键盘实现从 `Bluedroid` 迁移到 `NimBLE`，解决设备 `reset` 后 Windows 主机不能自动恢复 BLE HID 连接的问题，并恢复端到端输入验证链路，使下面两个脚本在当前机器上可重复验收：

- `powershell -ExecutionPolicy Bypass -File .\tools\verify_ble_hid.ps1 -Port COM3 -Text "ab"`
- `powershell -ExecutionPolicy Bypass -File .\tools\verify_ble_hid_end_to_end.ps1 -Port COM3 -Text "ab"`

最终验收目标：

- 设备 `reset` 后无需人工在 Windows 蓝牙界面点击重连
- 固件日志能稳定出现连接、加密、订阅和断开相关关键标记
- `verify_ble_hid_end_to_end.ps1` 返回成功，并包含 `hid_keyboard: send_ascii done`

## 当前问题

当前仓库虽然已经完成 BLE HID 构建、刷写、串口注入和固件侧发送链路验证，但在 `2026-05-13` 的这台 Windows 机器上仍存在以下问题：

- `reset` 后 Windows 没有自动恢复 BLE HID 连接
- `tools/verify_ble_hid.ps1` 日志显示 `connected=no` 和 `Device Not Connected`
- `tools/verify_ble_hid_end_to_end.ps1` 缺少 `hid_keyboard: send_ascii done`
- 启动日志持续出现 `BTM_BleWriteAdvData, Partial data write into ADV`
- 当前实现实际走的是 `Bluedroid`，不是 `NimBLE`

补充背景：

- `sdkconfig` 当前为 `CONFIG_BT_BLUEDROID_ENABLED=y`
- `CONFIG_BT_NIMBLE_ENABLED` 当前未开启
- `reset` 后抓取约 `180` 秒日志，没有看到明确的 `connect` / `auth` / `encrypt` 相关日志
- Windows UI 中“已连接”状态不可靠，设备属性里有 `AlwaysShowDeviceAsConnected=True`

## 范围

- 评估并迁移当前 BLE HID 路径到 `NimBLE`
- 调整 GAP 广告配置与广告启动时机，确保和 `NimBLE` 工作流一致
- 在固件中补齐连接、断开、订阅、加密相关日志
- 以当前 `ESP-IDF` 本机示例为主要参考，修正仓库实现
- 构建、刷写到 `COM3`，并使用现有脚本完成 reset 后自动回连和端到端输入验证
- 更新 `docs/features/ble_hid_keyboard_input.md`

## 不在本次范围内

- 新增真实语音输入、按键扫描或其他产品功能
- 修改 `STM32` 端结构或实现
- 引入与 BLE HID 无关的目录重构
- 重写现有验证脚本的整体交互模型，除非为本次验收必须做最小修复

## 假设与依赖

- 当前开发板仍为 `ESP32-S3`，可通过 `COM3` 稳定刷写和抓日志
- 本机 `ESP-IDF` 已安装在 `C:\Users\Billy\esp\esp-idf`
- 当前仓库工作区干净，基线为 `master` / `b115b85`
- Windows 主机保留已有 BLE 配对记录，可用于验证自动回连
- 本机官方示例可作为优先参考：
  - `C:\Users\Billy\esp\esp-idf\examples\bluetooth\esp_hid_device`
  - `C:\Users\Billy\esp\esp-idf\examples\bluetooth\bluedroid\ble\ble_hid_device_demo`

## 已知怀疑点

### 怀疑点 1：Bluedroid 广告包被裁剪

当前 `ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c` 在 `Bluedroid` 路径里同时往广告数据放入以下内容：

- `include_name`
- `include_txpower`
- `appearance`
- `128-bit HID service UUID`

这很可能超过 `31` 字节传统广告长度，且日志已经出现 `Partial data write into ADV`，说明广告内容被截断，可能导致主机无法稳定识别或回连。

### 怀疑点 2：广告启动时机不对

当前实现是在 `ESP_HIDD_START_EVENT` 里直接调用 `start_advertising()`。根据官方 `Bluedroid` HID 示例的常见时序，更稳妥的方式是等 `ADV_DATA_SET_COMPLETE` 之后再启动广告。虽然本次主要方向是迁移 `NimBLE`，但这个时序问题仍需要在迁移时一并校正。

## 执行步骤

### 第一步：补齐迁移设计与参考实现对照

状态：`completed`

工作内容：

- 对照本机 `ESP-IDF` 官方示例，确认 `NimBLE HID` 推荐初始化顺序、广告字段组织、配对安全参数、日志点位和重新开始广告的时机
- 对照仓库当前 `Bluedroid` 路径，列出必须迁移的文件、配置和接口边界
- 如发现计划假设不成立，先回写本计划文档再继续

当前已确认的迁移差异清单：

- `sdkconfig.defaults` 当前声明 `CONFIG_BT_NIMBLE_ENABLED=y`，但 `sdkconfig.defaults.esp32s3` 又显式打开 `CONFIG_BT_BLUEDROID_ENABLED=y` 和 `CONFIG_BT_BLE_ENABLED=y`，两份默认配置互相冲突，构建时实际落回了 `Bluedroid`
- 仓库当前 `ports/esp32/ble_hid/ble_hid.c` 已经具备 `NimBLE` 分支骨架，包含：
  - `ble_store_config_init()`
  - `ble_hs_cfg.store_status_cb = ble_store_util_status_rr`
  - `esp_nimble_enable(ble_hid_host_task)`
- 仓库当前 `ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c` 已经包含 `NimBLE` 的 GAP / SM / advertising 分支，但日志、广告字段组织和回连观察点还没有按“键盘自动回连”目标收紧
- 当前 `Bluedroid` 路径的广告数据同时包含 `name`、`tx power`、`appearance`、`128-bit HID UUID`，并持续触发 `Partial data write into ADV`；迁移到 `NimBLE` 后也需要明确限制主广告内容，避免把旧问题原样带过去
- 当前仓库在 `ESP_HIDD_START_EVENT` 中立即开始广告；迁移后应优先对齐 `NimBLE` 参考流程，确保 advertising、connect、subscribe、encryption 日志都能稳定观察
- 官方 `esp_hid_device` 示例虽然默认 `esp32s3` 仍走 `Bluedroid`，但仓库本地 `sdkconfig.ci.nimble` 与源码中的 `CONFIG_BT_NIMBLE_ENABLED` 分支已经提供了可参考的 `NimBLE` 路径，需要从示例源码而不是默认配置直接抽取实现模式
- 迁移时需要重点收口的仓库文件：
  - `sdkconfig.defaults`
  - `sdkconfig.defaults.esp32s3`
  - `ports/esp32/ble_hid/ble_hid.c`
  - `ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c`
  - 视验证结果决定是否最小调整 `tools/verify_ble_hid*.ps1`

验收方式：

- 形成一份明确的迁移差异清单
- 计划文档中记录关键实现方向且无需再依赖模糊猜测

人工检查点：

- 你确认继续按 `NimBLE` 迁移方向执行，而不是先在 `Bluedroid` 上做最小修补验证

### 第二步：切换配置并完成 NimBLE 侧实现迁移

状态：`completed`

工作内容：

- 调整 `sdkconfig` 或相关默认配置，关闭 `Bluedroid`、开启 `NimBLE`
- 更新 `ports/esp32/ble_hid/` 与 `ports/esp32/ble_hid_gap/` 中的初始化、广告、配对和回连逻辑
- 保留并增强连接、断开、订阅、加密、广告启停等关键日志
- 保持仓库平台边界不被破坏，尽量把 `ESP-IDF` 细节留在 `ports/esp32/`

验收方式：

- `powershell -ExecutionPolicy Bypass -File .\tools\build.ps1` 成功
- 启动日志中不再出现 `Partial data write into ADV`
- 启动日志出现明确的广告初始化与启动标记

当前验收记录：

- `2026-05-13` 已切换 `sdkconfig.defaults.esp32s3` 到 `CONFIG_BT_NIMBLE_ENABLED=y` 和 `CONFIG_BT_NIMBLE_HID_SERVICE=y`
- 生成的 `sdkconfig` 显示 `# CONFIG_BT_BLUEDROID_ENABLED is not set`、`CONFIG_BT_NIMBLE_ENABLED=y`、`CONFIG_BT_NIMBLE_HID_SERVICE=y`
- `powershell -ExecutionPolicy Bypass -File .\tools\build.ps1` 已成功
- `powershell -ExecutionPolicy Bypass -File .\tools\flash.ps1 -Port COM3` 已成功
- 启动日志已出现 `NimBLE advertising configured`、`NimBLE advertising started`
- 启动日志不再出现 `BTM_BleWriteAdvData, Partial data write into ADV`
- 主广告包已限制为 `flags + appearance + 16-bit HID UUID + Listener Keyboard`，当前日志为 `name_in_adv=yes`
- `NimBLE` 安全参数已调整为更贴近无屏设备的 `bonding=1`、`mitm=0`、`sc=1`、`io_cap=NO_IO`

人工检查点：

- 仅当迁移过程中发现官方 `esp_hidd` 的 `NimBLE` 路径本身无法满足 HID 回连要求，或必须改动脚本/架构边界时再请你决策

### 第三步：刷写并验证 reset 后自动回连日志

状态：`blocked`

工作内容：

- 将迁移后的固件刷写到 `COM3`
- 通过现有抓串口脚本在 `reset` 后观察广告、连接、订阅、加密、断开和重新广播日志
- 必要时重复多轮 reset，确认不是一次性偶然成功

验收方式：

- `powershell -ExecutionPolicy Bypass -File .\tools\flash.ps1 -Port COM3` 成功
- `powershell -ExecutionPolicy Bypass -File .\tools\capture_serial.ps1 -Port COM3 -ResetBeforeRead` 输出包含期望连接阶段日志
- 至少一次 reset 后无需人工点击，日志出现成功连接与加密相关标记

当前验收记录：

- `2026-05-13` 抓取 `50` 秒 reset 后启动日志，设备正常进入 `NimBLE` 广告
- 固件日志显示 `NimBLE bonded peers=0`
- Windows PnP 仍保留 `BTHLE\DEV_DCB4D91112CE` 的 `Listener Keyboard` 设备记录，且 `DEVPKEY_DeviceContainer_AlwaysShowDeviceAsConnected=True`
- `60` 秒 `verify_ble_hid.ps1` 仍未看到 `connect`、`subscribe`、`encryption` 事件，发送时仍为 `connected=no`
- `2026-05-13` 已启用 `CONFIG_BT_NIMBLE_NVS_PERSIST=y` 与 `CONFIG_BT_NIMBLE_SM_LVL=2`，重新 `build` 与 `flash` 到 `COM3` 均成功
- 新固件首次验证仍显示 `NimBLE bonded peers=0`，并反复出现 `security already in progress`、`encryption change event; status=7`、`disconnect; reason=531`
- `2026-05-13` Windows 侧出现 `Listener Keyboard` 电量 `0%` 提醒；根因是 NimBLE Battery Service 默认初始化值为 `0`，当前 seed 固件没有真实电量计
- 已在固件初始化后调用 `esp_hidd_dev_battery_set(..., 100)` 设置占位电量，避免 Windows 把默认值误判为低电量
- `2026-05-13` 再次运行 `powershell -ExecutionPolicy Bypass -File .\tools\build.ps1` 成功
- `2026-05-13` 再次运行 `powershell -ExecutionPolicy Bypass -File .\tools\flash.ps1 -Port COM3` 成功
- `2026-05-13` 抓取启动日志已确认出现 `ble_hid: battery level placeholder=100`

当前判断：

- 迁移到 `NimBLE` 后，设备端没有可用的 `NimBLE` bond；Windows 端仍保留旧 `Bluedroid` 时代的配对/设备容器
- BLE 外设无法主动连接 Windows，只能广告等待中心设备连接；因此这个状态下需要一次 Windows 侧“删除设备并重新配对”，让双方重新建立 `NimBLE` bond
- 重新配对完成后再验证 reset 自动回连，目标仍然是后续 reset 无需人工重连
- 新发现：生成的 `sdkconfig` 中 `CONFIG_BT_NIMBLE_NVS_PERSIST` 仍为关闭状态，意味着即使完成配对，`NimBLE` bond key 也不会持久保存到 NVS；这会直接导致设备 `reset` 后无法用旧 bond 恢复加密连接
- 修正方向：在默认配置中启用 `CONFIG_BT_NIMBLE_NVS_PERSIST=y`，并把 `CONFIG_BT_NIMBLE_SM_LVL` 调整为 `2`，要求 HID GATT 访问使用加密连接，但不强制无屏键盘无法完成的 MITM
- 当前阻塞已经收敛为 Windows 侧配对缓存问题：需要在已刷入 NVS 持久化固件之后，再删除并重新配对一次 `Listener Keyboard`
- Battery Service 的 `100%` 当前只是 seed 阶段占位值，后续接入真实电源/电量检测后应替换为真实读数

人工检查点：

- 仅当需要人工执行 Windows 蓝牙忘记设备、重新配对、或确认系统 UI 状态时再请你介入

### 第四步：验证端到端输入链路

状态：`pending`

工作内容：

- 运行 `tools/verify_ble_hid.ps1` 确认固件侧链路与主机连接状态都已恢复
- 运行 `tools/verify_ble_hid_end_to_end.ps1` 确认主机实际收到测试字符
- 如失败，基于日志继续做最小必要修复并回到本步骤复验

验收方式：

- `tools/verify_ble_hid.ps1 -Port COM3 -Text "ab"` 输出主机已连接分支
- `tools/verify_ble_hid_end_to_end.ps1 -Port COM3 -Text "ab"` 成功返回
- 固件日志包含 `hid_keyboard: send_ascii done`

人工检查点：

- 仅当必须有人为系统输入焦点、蓝牙系统弹窗或权限提示做确认时再请你介入

### 第五步：更新文档并交接

状态：`pending`

工作内容：

- 更新 `docs/features/ble_hid_keyboard_input.md`
- 记录 `NimBLE` 路径下的关键代码位置、验证命令、日志标记、限制与后续风险
- 说明本次迁移对未来 `ESP32` / `STM32` 可移植性的影响

验收方式：

- 文档与当前实现一致
- 文档明确写出 `COM3` 验证命令与 reset 后自动回连验收方式

人工检查点：

- 无；完成后直接交付复核

## 当前阻塞项

- Windows 侧需要一次性删除旧的 `Listener Keyboard` 配对记录并重新配对，以建立新的 `NimBLE` bond
- 完成重新配对前，固件端 `NimBLE bonded peers=0`，无法验证 reset 后自动回连和端到端输入
- 如果重新配对后仍不能自动回连，再继续调查 Windows HID GATT 缓存、NimBLE bond 持久化和 directed advertising 是否需要增强

## 当前批准边界

你已经批准按 `NimBLE` 迁移方向继续执行。当前代码迁移和构建刷写已完成，下一步需要一次 Windows 侧重新配对后继续第三步验收。
