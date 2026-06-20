[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Port = "COMx",
    [int]$Baud = 115200,
    [string]$OutputDir = "",
    [ValidateSet("Foundation", "Scenes", "Complex", "Volume", "Product", "RootCause", "StaticRoot", "Repro", "TailOnly", "ComboOnly", "Full")]
    [string]$Mode = "Foundation",
    [int]$CommandReadMs = 700,
    [int]$InitialReadMs = 1200,
    [int]$PreclearReadMs = 350,
    [switch]$PlanOnly,
    [switch]$NoPrompt
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repoRoot "docs/validation/status-led-human-effect-review-$stamp"
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir = Join-Path $repoRoot $OutputDir
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path

$planPath = Join-Path $OutputDir "human-effect-plan.md"
$sessionPath = Join-Path $OutputDir "human-effect-session.jsonl"
$serialPath = Join-Path $OutputDir "human-effect-serial.log"
$summaryJsonPath = Join-Path $OutputDir "human-effect-summary.json"
$summaryMdPath = Join-Path $OutputDir "human-effect-summary.md"

$serialLines = [System.Collections.Generic.List[string]]::new()
$records = [System.Collections.Generic.List[object]]::new()

function New-LedReviewStep {
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][string[]]$Commands,
        [Parameter(Mandatory = $true)][string]$Expected,
        [Parameter(Mandatory = $true)][string]$HumanFocus,
        [string]$When = "",
        [string]$PassRule = "",
        [string[]]$PostCommands = @()
    )

    [PSCustomObject]@{
        id = $Id
        title = $Title
        commands = @($Commands)
        post_commands = @($PostCommands)
        when = $When
        expected = $Expected
        human_focus = $HumanFocus
        pass_rule = $PassRule
    }
}

function Get-FoundationSteps {
    $steps = @(
        New-LedReviewStep `
            -Id "foundation-off" `
            -Title "全灭基线" `
            -Commands @("~LED:OFF", "~LED:STATUS") `
            -Expected "所有状态灯、旋钮灯、按键灯、边框灯都应熄灭。" `
            -HumanFocus "看是否还有暗亮、残影、随机闪，尤其是状态灯 5/6。" `
            -PassRule "无任何可见残光或不规则闪烁。"
        New-LedReviewStep `
            -Id "status-led1-white-35" `
            -Title "状态 LED1/PWR 单灯白色 35%" `
            -Commands @("~LED:TEST:PIXEL status LED1 white 35", "~LED:STATUS") `
            -Expected "只看到 LED1 白色中低亮度；LED2-LED6 都不应亮。" `
            -HumanFocus "确认单灯隔离，5/6 不能跟着亮。"
        New-LedReviewStep `
            -Id "status-led2-blue-35" `
            -Title "状态 LED2/BLE 单灯蓝色 35%" `
            -Commands @("~LED:TEST:PIXEL status LED2 blue 35", "~LED:STATUS") `
            -Expected "只看到 LED2 蓝色；LED1、LED3-LED6 都不应亮。" `
            -HumanFocus "确认蓝色位置正确，没有串到 5/6。"
        New-LedReviewStep `
            -Id "status-led3-white-10" `
            -Title "状态 LED3/REC 亮度 10%" `
            -Commands @("~LED:TEST:PIXEL status LED3 white 10", "~LED:STATUS") `
            -Expected "只看到 LED3 很低亮度白色。" `
            -HumanFocus "记录 10% 是否可见、是否稳定。"
        New-LedReviewStep `
            -Id "status-led3-white-35" `
            -Title "状态 LED3/REC 亮度 35%" `
            -Commands @("~LED:TEST:PIXEL status LED3 white 35", "~LED:STATUS") `
            -Expected "只看到 LED3 白色，明显比 10% 亮。" `
            -HumanFocus "确认亮度递增，不闪、不拖尾到 LED4/5/6。"
        New-LedReviewStep `
            -Id "status-led3-white-70" `
            -Title "状态 LED3/REC 亮度 70%" `
            -Commands @("~LED:TEST:PIXEL status LED3 white 70", "~LED:STATUS") `
            -Expected "只看到 LED3 白色，明显比 35% 亮但不过曝刺眼。" `
            -HumanFocus "确认亮度递增；如果 35% 和 70% 人眼差别不大，需要记录。"
        New-LedReviewStep `
            -Id "status-led4-blue-35" `
            -Title "状态 LED4/AI 单灯蓝色 35%" `
            -Commands @("~LED:TEST:PIXEL status LED4 blue 35", "~LED:STATUS") `
            -Expected "只看到 LED4 蓝色；LED5/6 不应出现同色闪烁。" `
            -HumanFocus "重点看 5/6 是否跟前灯闪。"
        New-LedReviewStep `
            -Id "status-led5-green-35" `
            -Title "状态 LED5/OK 单灯绿色 35%" `
            -Commands @("~LED:TEST:PIXEL status LED5 green 35", "~LED:STATUS") `
            -Expected "只看到 LED5 绿色；LED6 不应跟亮。" `
            -HumanFocus "确认 OK 灯本身可见且位置正确。"
        New-LedReviewStep `
            -Id "status-led6-red-35" `
            -Title "状态 LED6/WARN 单灯红色 35%" `
            -Commands @("~LED:TEST:PIXEL status LED6 red 35", "~LED:STATUS") `
            -Expected "只看到 LED6 红色；LED5 不应跟亮。" `
            -HumanFocus "确认 WARN 灯本身可见且位置正确。"
        New-LedReviewStep `
            -Id "recording-processing-tail" `
            -Title "录音+处理中状态尾灯隔离" `
            -Commands @("~LED:PREVIEW recording_processing_status_led_only", "~LED:STATUS", "~LED:STATUS", "~LED:STATUS") `
            -Expected "专用 effect-only 状态轨：只有 LED3 金色录音和 LED4 固定紫色处理参与；PWR/BLE、旋钮、边框不参与；LED5/OK 和 LED6/WARN 必须保持熄灭。" `
            -HumanFocus "重点观察 5/6 是否不规则跟着 3/4 闪同样颜色；~LED:STATUS 必须显示 preview_effect_only=1。"
        New-LedReviewStep `
            -Id "key-led11-white-35" `
            -Title "按键 LED11 单灯白色 35%" `
            -Commands @("~LED:TEST:PIXEL key LED11 white 35", "~LED:STATUS") `
            -Expected "只看到第一个按键灯白色；状态灯不应被污染。" `
            -HumanFocus "确认按键灯独立，不影响状态灯 5/6。"
        New-LedReviewStep `
            -Id "ec11-led7-white-35" `
            -Title "旋钮 LED7 单灯白色 35%" `
            -Commands @("~LED:TEST:PIXEL ec11 LED7 white 35", "~LED:STATUS") `
            -Expected "只看到旋钮环一个点白色；状态灯不应被污染。" `
            -HumanFocus "确认旋钮灯独立、亮度可接受。"
        New-LedReviewStep `
            -Id "edge-led17-white-35" `
            -Title "边框 LED17 单灯白色 35%" `
            -Commands @("~LED:TEST:PIXEL edge LED17 white 35", "~LED:STATUS") `
            -Expected "只看到边框/板框一个点白色；状态灯不应被污染。" `
            -HumanFocus "确认边框灯独立、亮度可接受。"
        New-LedReviewStep `
            -Id "foundation-final-off" `
            -Title "结束全灭" `
            -Commands @("~LED:OFF", "~LED:STATUS") `
            -Expected "所有灯回到熄灭。" `
            -HumanFocus "确认没有收尾残留、尾灯不再闪。"
    )
    return @($steps)
}

function Get-SceneSteps {
    $steps = @(
        New-LedReviewStep `
            -Id "scene-ready" `
            -Title "产品场景：就绪" `
            -Commands @("~LED:PREVIEW ready", "WAIT 250", "~LED:STATUS", "WAIT 1600", "~LED:STATUS") `
            -When "设备已开机且 BLE 已连接，当前没有录音、AI 处理、错误或成功确认；PWR/BLE 用于告诉用户设备在线但不要抢注意力。" `
            -Expected "PWR/BLE 清楚但不抢眼；BLE 连接初始可有一次短蓝色成功确认，随后回到安静在线状态；其它语义灯不亮。" `
            -HumanFocus "确认蓝牙连接成功提示只在 BLE 语义灯上完成，不带动旋钮/板框，也不像跑马灯。"
        New-LedReviewStep `
            -Id "scene-pairing" `
            -Title "产品场景：蓝牙等待配对" `
            -Commands @("~LED:PREVIEW pairing", "WAIT 120", "~LED:STATUS", "WAIT 780", "~LED:STATUS", "WAIT 900", "~LED:STATUS") `
            -When "新设备首次开机、清除绑定完成、或没有可重连主机时进入 BLE 可发现配对窗口；只表达可以配对，不是错误。" `
            -Expected "LED2/BLE 蓝色低频脉冲；旋钮和板框不参与配对提示；若插电 PWR 可保持独立充电/在线基线；REC、AI、OK、WARN 不应乱入。" `
            -HumanFocus "确认蓝牙灯明确但不刺眼；旋钮/板框应保持灭，OK/WARN 必须灭。"
        New-LedReviewStep `
            -Id "scene-reconnecting" `
            -Title "产品场景：蓝牙重连中" `
            -Commands @("~LED:PREVIEW reconnecting", "WAIT 80", "~LED:STATUS", "WAIT 240", "~LED:STATUS", "WAIT 700", "~LED:STATUS") `
            -When "设备从睡眠/断链恢复，正在尝试找回已绑定主机；这是暂态连接状态，不应该亮错误灯。" `
            -Expected "LED2/BLE 蓝色双脉冲；旋钮和板框不参与重连提示；若插电 PWR 可保持独立充电/在线基线；REC、AI、OK、WARN 不应乱入。" `
            -HumanFocus "确认能和普通 pairing 区分，但仍然安静；旋钮/板框应保持灭，LED6/WARN 不能参与。"
        New-LedReviewStep `
            -Id "scene-ble-repairing" `
            -Title "产品场景：双击旋钮重新配对" `
            -Commands @("~LED:PREVIEW repairing", "WAIT 80", "~LED:STATUS", "WAIT 570", "~LED:STATUS", "WAIT 850", "~LED:STATUS") `
            -When "用户双击 EC11 旋钮或发 `~VREC:RECOVERY` 清除旧绑定并重新进入配对；这是用户主动重配确认，不是故障。" `
            -Expected "LED2/BLE 较明显蓝色双脉冲；EC11 旋钮全圈低亮蓝色，并且和 BLE 同步闪烁，不再绕圈旋转；板框不参与；REC、AI、OK、WARN 必须灭。" `
            -HumanFocus "确认这是用户主动重配：BLE 语义灯明确，旋钮只做同步低亮蓝色确认，板框保持灭，不要误读为错误或录音。"
        New-LedReviewStep `
            -Id "scene-charging" `
            -Title "产品场景：插电充电中" `
            -Commands @("~LED:PREVIEW charging", "WAIT 1700", "~LED:STATUS", "WAIT 900", "~LED:STATUS") `
            -When "USB/VBUS 在位、充电芯片显示正在充电且未确认满电；只由 PWR 表达外接电源/充电。" `
            -Expected "PWR 应是白色、慢速、成熟的充电呼吸；亮度范围要看得出来但不能像警告闪烁；BLE、REC、AI、OK、WARN 不应乱入。" `
            -HumanFocus "重点看充电呼吸是否舒服：最暗和最亮之间要有可感知差异，节奏要慢；如果人眼看到绿灯或蓝灯，请记录是哪一颗。"
        New-LedReviewStep `
            -Id "scene-full" `
            -Title "产品场景：插电满电" `
            -Commands @("~LED:PREVIEW full", "~LED:STATUS") `
            -When "USB/VBUS 在位，充电完成信号和电量经过 debounce 后本轮插电锁定为满电；只由 PWR 表达满电。" `
            -Expected "PWR 应是稳定白色满电状态；不是 OK 绿灯，也不是 BLE 蓝灯。" `
            -HumanFocus "确认满电语义只在 PWR，不误读为录音结束 OK。"
        New-LedReviewStep `
            -Id "scene-low-battery" `
            -Title "产品场景：低电量" `
            -Commands @("~LED:PREVIEW low_battery", "~LED:STATUS") `
            -When "未插电且电量进入低电阈值；由 PWR 用 amber/red 系提示电源风险，不代表录音或 BLE 状态。" `
            -Expected "PWR 应是稳定低亮红琥珀低电提示，不做呼吸或双闪；其它工作态灯不应出现。" `
            -HumanFocus "确认低电提示可读、安静，不像严重低电双闪、充电呼吸或录音；如果看起来像 LED3 也亮，请记录。"
        New-LedReviewStep `
            -Id "scene-critical-battery" `
            -Title "产品场景：严重低电" `
            -Commands @("~LED:PREVIEW critical_battery", "WAIT 900", "~LED:STATUS", "WAIT 900", "~LED:STATUS") `
            -When "未插电且电量进入严重低电阈值；这是电源安全提示，优先于普通氛围灯。" `
            -Expected "PWR 应给出明确红色双闪严重低电警示；其它工作态灯不应出现。" `
            -HumanFocus "确认严重低电是红色双闪，不是黄色/琥珀常亮，也没有 LED5/6 乱跟闪。"
        New-LedReviewStep `
            -Id "scene-recording-processing-live" `
            -Title "产品场景：真实录音+处理叠加（含 PWR/BLE）" `
            -Commands @("~LED:PREVIEW recording_processing", "~LED:REC_LEVEL 75 60000", "~LED:STATUS", "~LED:STATUS", "~LED:STATUS") `
            -When "设备正在录音并且桌面端已经发 `VREC:PROCESSING:START`；PWR/BLE 仍表达电源和连接，REC/AI 同时表达工作状态。" `
            -Expected "真实产品预览：PWR/BLE 应保持独立可读，REC 暖金和 AI 紫色同时存在；旋钮/边框用低亮暖金底光加慢速低幅流动参与，不跟 PCM 变亮、不高频跳；LED5/OK 和 LED6/WARN 不应出现。" `
            -HumanFocus "这一步重点复查 5/6 闪烁：状态 REC 可随音量变化，AI 固定紫色并只做哒  停顿  紧凑哒哒的思考节奏；旋钮/边框应有一点固定变化，OK/WARN 必须保持灭。"
        New-LedReviewStep `
            -Id "scene-processing-live" `
            -Title "产品场景：AI 处理中（未录音）" `
            -Commands @("~LED:PREVIEW processing", "WAIT 900", "~LED:STATUS", "WAIT 900", "~LED:STATUS") `
            -When "桌面端进入 ASR/AI/OTA 处理阶段但当前没有本地录音；AI 灯只由 host-confirmed processing/OTA 开始触发。" `
            -Expected "PWR/BLE 保持独立可读；LED4/AI 紫色低频活性；旋钮和边框用低亮紫色顺时针转动；REC、OK、WARN 必须灭。" `
            -HumanFocus "确认 AI 灯的出现时机明确，旋钮/边框方向是顺时针；LED5/6 不应跟闪。"
        New-LedReviewStep `
            -Id "scene-ok" `
            -Title "产品场景：OK 确认" `
            -Commands @("~LED:PREVIEW ok", "~LED:STATUS") `
            -When "本地录音停止/会话完成，或桌面端发 `VREC:PROCESSING:DONE`；只表示成功确认，持续约 2.0 秒。" `
            -Expected "PWR/BLE 保持就绪基线；LED5 绿色短确认，旋钮/边框可有低亮绿色辅助；不应长时间常亮。" `
            -HumanFocus "确认 OK 位置和持续感，不能消失太快或太暗，也不能被 PWR/BLE 误读。"
        New-LedReviewStep `
            -Id "scene-rec-not-available" `
            -Title "产品场景：录音不可用警告" `
            -Commands @("~LED:PREVIEW rec_not_available", "~LED:STATUS") `
            -When "用户请求录音但当前没有可用录音源、权限/传输不满足或录音被拒绝；这是错误/警告语义，只用 WARN。" `
            -Expected "只用 LED6/WARN 提示录音不可用；REC、OK、旋钮、按键、边框不应参与。" `
            -HumanFocus "确认错误语义干净：只看 error/WARN 灯，不要被误读成正常录音或其它状态。"
        New-LedReviewStep `
            -Id "scene-shutdown-confirm" `
            -Title "产品场景：长按关机确认" `
            -Commands @("~LED:PREVIEW shutdown_confirm", "WAIT 600", "~LED:STATUS", "WAIT 1800", "~LED:STATUS") `
            -When "用户长按 EC11 到达关机确认阈值但尚未真正断电；用于告诉用户继续按住会关机。" `
            -Expected "暖琥珀 PWR + 旋钮顺时针进度感；板框不参与；绕满一圈后保持全圈亮，不应自己灭，直到最终长按关机/睡眠/clear 才灭。" `
            -HumanFocus "确认这是关机确认感，不像错误或录音；第二次状态读取时旋钮应已经满圈且仍亮，板框应保持灭。"
        New-LedReviewStep `
            -Id "scene-ec11-short-press" `
            -Title "产品场景：短按旋钮反馈" `
            -Commands @("WAIT 2200", "~LED:STATUS") `
            -When "点击确定后马上短按一次 EC11 旋钮；这是用户输入反馈，不代表 OK 成功、错误或 BLE 状态。" `
            -Expected "EC11 旋钮出现一次短白色确认；PWR/BLE 保持自己的状态；LED5/OK、LED6/WARN、板框和按键不应被点亮。" `
            -HumanFocus "点确定后立刻短按旋钮一次，确认旋钮有干净短反馈，不能像成功 OK 或错误 WARN。"
        New-LedReviewStep `
            -Id "scene-ec11-rotate" `
            -Title "产品场景：旋转旋钮反馈" `
            -Commands @("WAIT 2600", "~LED:STATUS") `
            -When "点击确定后先顺时针旋转 EC11 一格，再逆时针旋转一格；这是音量/亮度等 HID 动作成功排队后的输入反馈。" `
            -Expected "EC11 旋钮出现短白色方向性反馈，顺/逆方向可区分；PWR/BLE 保持自己的状态；LED5/OK、LED6/WARN、板框和按键不应乱入。" `
            -HumanFocus "点确定后马上顺时针、逆时针各转一下，确认有方向感但不抢眼，不能触发 OK/WARN。"
        New-LedReviewStep `
            -Id "scene-key-feedback" `
            -Title "产品场景：按键反馈" `
            -Commands @("WAIT 3000", "~LED:STATUS") `
            -When "点击确定后依次短按 KEY1、KEY2、KEY3、KEY4；这是本地按键输入反馈，不代表录音/处理/错误。" `
            -Expected "只有被按下的 KEY 灯有短白色反馈；PWR/BLE 保持自己的状态；REC、AI、OK、WARN、EC11、板框不应被错误点亮。" `
            -HumanFocus "点确定后依次按四个按键，确认每颗按键反馈位置正确、时间短、不会带动 5/6 或旋钮/板框。"
        New-LedReviewStep `
            -Id "scene-sleep" `
            -Title "产品场景：睡眠/低功耗熄灯" `
            -Commands @("~LED:PREVIEW sleep", "~LED:STATUS") `
            -When "空闲超时、低功耗策略或硬件关机准备阶段；除非有唤醒/错误/充电状态，灯应进入明确的低功耗熄灭。" `
            -Expected "所有状态灯、旋钮灯、按键灯、边框灯都应熄灭；不应残留蓝牙、AI、OK 或错误灯。" `
            -HumanFocus "确认睡眠不是暗闪或随机残光，尤其确认 BLE/AI/OK/WARN 都灭。"
    )
    return @($steps)
}

function Get-ComplexSteps {
    $steps = @(
        New-LedReviewStep `
            -Id "complex-recording-processing-product" `
            -Title "复杂灯效：录音+处理 专用动态效果" `
            -Commands @("~LED:PREVIEW recording_processing_led_only", "~LED:REC_LEVEL 100 60000", "~LED:STATUS", "~LED:STATUS", "~LED:STATUS") `
            -When "调校录音+AI 同时存在时的高级灯效，不让 PWR/BLE/按键参与；用于验收 REC/AI、旋钮底座、板框三者组合是否不闪、不串色。" `
            -Expected "专用 effect-only 预览：状态灯只看 REC/AI，PWR/BLE 不参与；按键灯不参与；旋钮 12 颗和边框 6 颗用低亮暖金底光加慢速低幅流动参与，不跟 PCM 变亮、不高频跳；LED5/OK 和 LED6/WARN 不应跟闪。" `
            -HumanFocus "按整体灯效判断：REC/AI 状态灯要有轻微活性但不乱闪；旋钮和边框应低亮、慢速、有一点流动、不抢眼；~LED:STATUS 必须显示 preview_effect_only=1。"
        New-LedReviewStep `
            -Id "complex-recording-processing-status-only" `
            -Title "复杂灯效：录音+处理 仅状态灯动态" `
            -Commands @("~LED:PREVIEW recording_processing_status_led_only", "~LED:STATUS", "~LED:STATUS", "~LED:STATUS") `
            -When "隔离检查状态灯 LED3/REC 与 LED4/AI 的动态本体，排除旋钮/板框/按键干扰，用于判断 LED5/6 跟闪根因。" `
            -Expected "专用 effect-only 状态轨：只保留 LED3/REC 和 LED4/AI 的低频量化动态；PWR/BLE、旋钮和边框应熄灭；LED5/OK 和 LED6/WARN 不应跟闪。" `
            -HumanFocus "这里验证动态状态轨本身：如果这里稳定，说明状态灯高级动态可用；如果仍闪，问题在状态灯物理链路、日志或刷新策略。"
        New-LedReviewStep `
            -Id "complex-capture-only" `
            -Title "复杂灯效：仅录音 暖金旋转底光" `
            -Commands @("~LED:PREVIEW capture_led_only", "~LED:REC_LEVEL 75 60000", "~LED:STATUS", "~LED:STATUS", "~LED:STATUS") `
            -When "只调录音中的高级灯效；真实产品中对应用户按下录音后、AI 还未开始处理时的 REC/旋钮/板框表达。" `
            -Expected "专用 effect-only 预览：LED3 暖金录音应按模拟 rec_level 平滑亮起；按键灯不参与；旋钮 12 颗、边框 6 颗为低亮暖金底光加慢速固定流动，不跟 PCM 亮度跳变；LED5/6 应保持熄灭。" `
            -HumanFocus "看单独录音是否稳定且不是死灯：颜色偏金黄而不是红，旋钮/边框有一点固定变化但不能跳闪，~LED:STATUS 必须显示 preview_effect_only=1。"
        New-LedReviewStep `
            -Id "complex-processing-only" `
            -Title "复杂灯效：仅处理 紫色环绕" `
            -Commands @("~LED:PREVIEW processing_led_only", "~LED:STATUS", "~LED:STATUS", "~LED:STATUS") `
            -When "只调 AI/处理中的高级灯效；真实产品中对应 host-confirmed processing/OTA 阶段的 AI 紫色语义和旋钮/板框辅助。" `
            -Expected "专用 effect-only 预览：LED4 紫色处理为哒  哒哒的 AI 思考节奏；按键灯不参与；旋钮底座和边框都应低亮紫色顺时针转动；LED5/6 应保持熄灭，且查询状态不应导致重启。" `
            -HumanFocus "看单独处理是否顺时针、克制、稳定；状态灯不能是死灯，也不能让 LED5/6 跟闪；~LED:STATUS 必须显示 preview_effect_only=1。"
        New-LedReviewStep `
            -Id "complex-ai-status-only" `
            -Title "复杂灯效：仅 AI 状态灯哒 哒哒" `
            -Commands @("~LED:PREVIEW processing_status_led_only", "~LED:STATUS", "~LED:STATUS", "~LED:STATUS") `
            -When "隔离检查 AI 单灯高级灯效；不让 PWR/BLE、REC、旋钮、按键、板框参与。" `
            -Expected "专用 effect-only 状态轨：只有 LED4/AI 紫色哒  哒哒节奏，然后回到低亮保持；LED1/2/3/5/6、旋钮、按键、边框都应熄灭。" `
            -HumanFocus "重点看 AI 单灯是否是哒、停顿、哒哒的节奏，高级但不刺眼、不像故障闪烁；LED5/6 不能跟着 LED4 闪，~LED:STATUS 必须显示 preview_effect_only=1 且 OK/WARN 为 0。"
        New-LedReviewStep `
            -Id "complex-shutdown-confirm" `
            -Title "复杂灯效：长按关机确认" `
            -Commands @("~LED:PREVIEW shutdown_confirm", "WAIT 400", "~LED:STATUS", "WAIT 900", "~LED:STATUS", "WAIT 1200", "~LED:STATUS") `
            -When "调校长按关机确认的高级提示；真实产品中对应 EC11 长按超过确认阈值、尚未进入最终断电。" `
            -Expected "暖琥珀 PWR + 旋钮顺时针填充；板框不参与；旋钮绕满一圈后保持全圈亮，不应自己灭，直到最终长按关机/睡眠/clear 才灭。" `
            -HumanFocus "确认长按关机确认灯效是否能被人眼理解；重点看方向是否顺时针、超过 1.8 秒后是否仍保持亮、板框是否保持灭。"
    )
    return @($steps)
}

function Get-VolumeSteps {
    $steps = @(
        New-LedReviewStep `
            -Id "volume-capture-sweep" `
            -Title "音量响应：录音灯按音量变亮抗闪" `
            -Commands @(
                "~LED:PREVIEW capture",
                "WAIT 12000",
                "~LED:STATUS") `
            -When "真实录音场景；不发送 REC_LEVEL 档位，直接用麦克风实时音量驱动 LED3，通过限幅路径验证录音灯音量响应。" `
            -Expected "真实录音场景预览：安静时 LED3/REC 保持低亮或弱亮，靠近麦克风说话、拍手或敲击时 LED3 明显变亮，回到安静后变暗；过渡不能突跳或高频闪；PWR/BLE 可保持自己的在线状态；旋钮全圈和边框 6 颗保持低亮暖金底光加慢速固定流动，不跟音量跳变；按键灯不参与；LED5/6 必须灭。" `
            -HumanFocus "这是真实音量响应抗闪验收：点确定后先安静约 2 秒，再对麦克风说话、拍手或轻敲约 6 秒，再安静约 2 秒；重点看 LED3 是否直接跟真实声音大小变亮变暗、5/6 是否还会绿/红闪、旋钮/边框是否稳定，~LED:STATUS 必须显示 preview_effect_only=0 和 recording_level_reactive=1。"
        New-LedReviewStep `
            -Id "volume-overlap-high" `
            -Title "音量响应：录音+处理 高音量叠加" `
            -Commands @("~LED:PREVIEW recording_processing_led_only", "~LED:REC_LEVEL 100 60000", "WAIT 5200", "~LED:STATUS", "WAIT 900", "~LED:STATUS", "WAIT 900", "~LED:STATUS") `
            -Expected "专用 effect-only 预览：REC 暖金应保持高音量亮度，AI 应一直是紫色，只用亮度做哒  哒哒思考节奏；按键灯、PWR/BLE 不参与；旋钮/边框保持低亮暖金底光和慢速低幅流动，不做同区金紫混色或高频扫动；LED5/6 必须灭，不应跟 LED3/4 闪。" `
            -HumanFocus "这是最接近之前闪烁痛点的稳定性验收：点确定后先看 LED4 是否固定紫色、是否像哒、停顿、哒哒的 AI 思考节奏而不是普通呼吸灯，再看 LED3 高音量、旋钮/边框低亮慢速流动，以及是否有 5/6 跟闪或随机绿闪；~LED:STATUS 必须显示 preview_effect_only=1。"
    )
    return @($steps)
}

function Get-RootCauseSteps {
    $steps = @(
        New-LedReviewStep `
            -Id "root-status-only-brightness-25" `
            -Title "根因验证：状态灯 REC+AI 25% 全局亮度" `
            -Commands @("~LED:BRIGHTNESS 25", "~LED:PREVIEW recording_processing_status_only", "~LED:STATUS") `
            -Expected "全局亮度临时降到 25%；只保留状态灯 PWR/BLE/REC/AI，旋钮和边框熄灭；LED5/OK 和 LED6/WARN 不应跟闪。" `
            -HumanFocus "重点看 5/6 是否还会跟 3/4 同色闪。如果这里不闪，问题高度指向亮度/电平阈值，而不是 OK/WARN 逻辑。"
        New-LedReviewStep `
            -Id "root-capture-brightness-25" `
            -Title "根因验证：仅录音 25% 全局亮度" `
            -Commands @("~LED:PREVIEW capture", "~LED:STATUS") `
            -Expected "LED3 金色录音稳定可见；LED5/6 应保持熄灭。" `
            -HumanFocus "看单独 REC 动态在低亮度下是否仍带出 5。"
        New-LedReviewStep `
            -Id "root-processing-brightness-25" `
            -Title "根因验证：仅处理 25% 全局亮度" `
            -Commands @("~LED:PREVIEW processing", "~LED:STATUS") `
            -Expected "LED4 紫色处理稳定可见；LED5/6 应保持熄灭。" `
            -HumanFocus "看单独 AI 动态在低亮度下是否仍带出 6。"
        New-LedReviewStep `
            -Id "root-restore-brightness-50" `
            -Title "恢复亮度并关灯" `
            -Commands @("~LED:BRIGHTNESS 50", "~LED:OFF", "~LED:STATUS") `
            -Expected "全局亮度恢复到 50%，所有灯熄灭。" `
            -HumanFocus "确认测试后没有留在低亮度，也没有残留闪烁。"
    )
    return @($steps)
}

function Get-StaticRootSteps {
    $steps = @(
        New-LedReviewStep `
            -Id "static-status-clean-50" `
            -Title "静态根因：固定 REC+AI，不查询状态" `
            -Commands @("~LED:BRIGHTNESS 50", "~LED:PREVIEW recording_processing_static_status_only") `
            -Expected "全局亮度 50%；只固定点亮状态灯 LED3/REC 金色和 LED4/AI 紫色；LED5/OK、LED6/WARN、旋钮和边框都应熄灭。" `
            -HumanFocus "这一步不发送 ~LED:STATUS。看固定不动的 3/4 是否还会让 5/6 闪。"
        New-LedReviewStep `
            -Id "static-status-query-50" `
            -Title "静态根因：固定 REC+AI，查询状态" `
            -Commands @("~LED:PREVIEW recording_processing_static_status_only", "~LED:STATUS") `
            -Expected "和上一步同样的固定 LED3/LED4；LED5/6 不应闪。" `
            -HumanFocus "如果上一步不闪、这一步闪，说明查询/日志时序也在扰动；如果两步都不闪，问题就是动态刷新。"
        New-LedReviewStep `
            -Id "static-root-restore-brightness-50" `
            -Title "恢复亮度并关灯" `
            -Commands @("~LED:BRIGHTNESS 50", "~LED:OFF", "~LED:STATUS") `
            -Expected "全局亮度保持 50%，所有灯熄灭。" `
            -HumanFocus "确认测试结束后没有残留闪烁。"
    )
    return @($steps)
}

function Get-ReproSteps {
    $steps = @(
        New-LedReviewStep `
            -Id "repro-status-only-dynamic" `
            -Title "复现对照：仅状态灯 REC+AI 动态" `
            -Commands @("~LED:PREVIEW recording_processing_status_led_only", "~LED:STATUS", "WAIT 1600", "~LED:STATUS") `
            -Expected "专用 effect-only 状态轨：只保留 LED3/REC 和 LED4/AI 动态；PWR/BLE、旋钮、按键、边框都应熄灭；LED5/OK 和 LED6/WARN 在软件状态里必须为 0。" `
            -HumanFocus "先看对照：只跑状态灯动态时，5/6 是否跟着 3/4 闪同色或随机绿/红闪。"
        New-LedReviewStep `
            -Id "repro-led3-key3-to-led5" `
            -Title "复现压力：只跑状态 LED3 + KEY1，看 LED5" `
            -Commands @("~LED:PREVIEW status_key_stress3", "~LED:STATUS", "WAIT 2200", "~LED:STATUS", "WAIT 2200", "~LED:STATUS") `
            -Expected "专用 effect-only 复现：只有状态 LED3/REC 和 KEY1 做同色同亮度暖金动态；LED4、LED5、LED6、KEY2、KEY3、KEY4、旋钮、边框都应熄灭；软件帧里 OK/WARN 必须保持 0。" `
            -HumanFocus "重点看 LED5：如果 LED3/KEY1 动态时 LED5 跟着闪暖金或绿色，就记录为复现；同时确认按键灯是否只有 KEY1 在动。"
        New-LedReviewStep `
            -Id "repro-led4-key2-to-led6" `
            -Title "复现压力：只跑状态 LED4 + KEY2，看 LED6" `
            -Commands @("~LED:PREVIEW status_key_stress4", "~LED:STATUS", "WAIT 2200", "~LED:STATUS", "WAIT 2200", "~LED:STATUS") `
            -Expected "专用 effect-only 复现：只有状态 LED4/AI 和 KEY2 做同色同亮度紫色动态；LED3、LED5、LED6、KEY1、KEY3、KEY4、旋钮、边框都应熄灭；软件帧里 OK/WARN 必须保持 0。" `
            -HumanFocus "重点看 LED6：如果 LED4/KEY2 动态时 LED6 跟着闪紫色或红色，就记录为复现；同时确认按键灯是否只有 KEY2 在动。"
        New-LedReviewStep `
            -Id "repro-led34-key12-to-led56" `
            -Title "复现压力：状态 LED3/4 + KEY1/2，看 LED5/6" `
            -Commands @("~LED:PREVIEW status_key_stress34", "~LED:STATUS", "WAIT 2200", "~LED:STATUS", "WAIT 2200", "~LED:STATUS") `
            -Expected "专用 effect-only 复现：状态 LED3/REC 对 KEY1 暖金动态，状态 LED4/AI 对 KEY2 紫色动态；KEY3/KEY4、旋钮、边框都熄灭；软件帧里 OK/WARN 必须保持 0。" `
            -HumanFocus "重点看 LED5/LED6：如果两个状态灯和两个按键同时动态后才出现跟闪，说明单灯没问题、双通道动态开始接近触发条件。"
        New-LedReviewStep `
            -Id "repro-led34-all-key-to-led56" `
            -Title "复现压力：状态 LED3/4 + 四个按键，看 LED5/6" `
            -Commands @("~LED:PREVIEW recording_processing_status_key_stress", "~LED:STATUS", "WAIT 2200", "~LED:STATUS", "WAIT 2200", "~LED:STATUS") `
            -Expected "专用 effect-only 复现：状态 LED3/REC 与 LED4/AI 动态，KEY1/KEY3 镜像 REC，KEY2/KEY4 镜像 AI；旋钮、边框不参与；软件帧里 OK/WARN 必须保持 0。" `
            -HumanFocus "这是按键灯最大负载复现：如果这里才闪，问题更像动态负载阈值；如果这里仍不闪，后续要加回旋钮/板框。"
        New-LedReviewStep `
            -Id "repro-final-off" `
            -Title "复现结束：全灭" `
            -Commands @("~LED:OFF", "~LED:STATUS") `
            -Expected "所有灯熄灭。" `
            -HumanFocus "确认没有残留闪烁。"
    )
    return @($steps)
}

function Get-TailOnlySteps {
    $steps = @(
        New-LedReviewStep `
            -Id "tail-only-ai-da-dada-grouped" `
            -Title "1.9目标确认：AI 单灯哒 哒哒" `
            -Commands @("~LED:BRIGHTNESS 100", "~LED:PREVIEW processing_status_led_only", "WAIT 6200") `
            -Expected "专用 effect-only 状态轨：只有 LED4/AI 固定紫色，亮度呈短促哒、明显停顿、紧凑哒哒，然后到下一轮一闪前有更长停顿；LED1/2/3/5/6、旋钮、按键、边框都应熄灭。" `
            -HumanFocus "重点看 LED4 是否有清楚的哒、停顿、后两闪连在一起成一组哒哒、再长停顿到下一轮一闪的 AI 思考节奏，而不是普通呼吸或故障闪烁；LED5/6 不能跟着 LED4 亮或闪。" `
            -PassRule "LED4 固定紫色哒  哒哒节奏可读，双闪到下一轮一闪之间的停顿清楚，后两闪像一个小组，不抖不刺眼；LED5/6 全程不跟闪，~LED:STATUS 显示 preview_effect_only=1 且 OK/WARN 为 0。" `
            -PostCommands @("~LED:STATUS", "~POWER:STATUS")
    )
    return @($steps)
}

function Get-ComboOnlySteps {
    $steps = @(
        New-LedReviewStep `
            -Id "combo-recording-processing-dynamic" `
            -Title "1.9目标确认：录音+处理与旋钮/板框低负载组合" `
            -Commands @("~LED:BRIGHTNESS 100", "~LED:PREVIEW recording_processing_led_only", "~LED:REC_LEVEL 100 60000", "WAIT 7000") `
            -When "调校录音+AI 同时存在时的组合灯效：状态 REC/AI、旋钮底座、板框一起参与，不让 PWR/BLE/按键参与。" `
            -Expected "专用 effect-only 组合预览：LED3/REC 暖金保持高音量亮度，LED4/AI 固定紫色短促哒、明显停顿、紧凑哒哒；旋钮 12 颗和边框 6 颗有低亮暖金底光加慢速低幅流动；按键、PWR/BLE 不参与；LED5/OK 和 LED6/WARN 必须保持熄灭。" `
            -HumanFocus "重点看整体是否稳定：状态 LED3 高音量应可读，LED4 应固定紫色并有哒、停顿、后两闪成组的哒哒、再停顿 AI 思考节奏但不抖；旋钮、板框应低亮慢速流动；LED5/6 不能跟闪、乱闪或被误点亮。" `
            -PassRule "状态 REC 高音量可读，AI 是固定紫色哒  哒哒节奏且停顿感清楚、后两闪成组、不刺眼，旋钮/板框低亮慢速流动，LED5/6 全程不跟闪，~LED:STATUS 显示 preview_effect_only=1 且 OK/WARN 为 0。" `
            -PostCommands @("~LED:STATUS", "~POWER:STATUS")
    )
    return @($steps)
}

function Get-ReviewSteps {
    $foundation = @(Get-FoundationSteps)
    if ($Mode -eq "Foundation") {
        return $foundation
    }
    $scenes = @(Get-SceneSteps)
    if ($Mode -eq "Scenes") {
        return $scenes
    }
    $complex = @(Get-ComplexSteps)
    if ($Mode -eq "Complex") {
        return $complex
    }
    $volume = @(Get-VolumeSteps)
    if ($Mode -eq "Volume") {
        return $volume
    }
    if ($Mode -eq "Product") {
        return @(
            $scenes | Where-Object { $_.id -eq "scene-charging" }
            $scenes | Where-Object { $_.id -eq "scene-low-battery" }
            $scenes | Where-Object { $_.id -eq "scene-ble-repairing" }
            $scenes | Where-Object { $_.id -eq "scene-recording-processing-live" }
            $scenes | Where-Object { $_.id -eq "scene-ec11-short-press" }
            $scenes | Where-Object { $_.id -eq "scene-ec11-rotate" }
            $scenes | Where-Object { $_.id -eq "scene-key-feedback" }
            $scenes | Where-Object { $_.id -eq "scene-sleep" }
        )
    }
    $rootCause = @(Get-RootCauseSteps)
    if ($Mode -eq "RootCause") {
        return $rootCause
    }
    $staticRoot = @(Get-StaticRootSteps)
    if ($Mode -eq "StaticRoot") {
        return $staticRoot
    }
    $repro = @(Get-ReproSteps)
    if ($Mode -eq "Repro") {
        return $repro
    }
    $tailOnly = @(Get-TailOnlySteps)
    if ($Mode -eq "TailOnly") {
        return $tailOnly
    }
    $comboOnly = @(Get-ComboOnlySteps)
    if ($Mode -eq "ComboOnly") {
        return $comboOnly
    }
    return @($foundation + $scenes + $complex)
}

function Write-PlanMarkdown {
    param([Parameter(Mandatory = $true)][object[]]$Steps)

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("# Status LED human effect review") | Out-Null
    $lines.Add("") | Out-Null
    $lines.Add("This run is the foundation pass before more LED program changes. Human-eye observations are the source of truth for physical brightness, flicker, tail-follow, and zone independence.") | Out-Null
    $lines.Add("") | Out-Null
    $lines.Add("## Desired Effects") | Out-Null
    $lines.Add("") | Out-Null
    $lines.Add("- Foundation first: one commanded LED means one physical LED; all other LEDs stay off.") | Out-Null
    $lines.Add("- Brightness must be monotonic by eye: 10% < 35% < 70%, with no saturation plateau at normal settings.") | Out-Null
    $lines.Add("- During recording/processing, status LED3 and LED4 may use capped, slow, quantized brightness changes; LED5/OK and LED6/WARN must stay off unless success/error owns them.") | Out-Null
    $lines.Add("- Volume mode uses real capture preview and expects only status LED3 to follow the smoothed bounded live-audio envelope; `~LED:REC_LEVEL` remains only for the overlap/high-level diagnostic step.") | Out-Null
    $lines.Add("- EC11 and edge/frame LEDs are independent accent surfaces. For this pass, REC volume changes only the status REC LED through a bounded envelope, AI stays static purple and uses a brightness-only grouped da-dada thinking beat, REC+AI overlap keeps the warm-gold recording cue instead of same-zone color mixing, and physical EC11 input uses brief white knob feedback. Key LEDs stay off unless there is a real key event or a diagnostic stress step.") | Out-Null
    $lines.Add("- Complex mode uses effect-only preview commands for REC/AI, EC11, and edge/frame validation; ordinary product previews remain in Scenes, not in the LED-effect tuning pass.") | Out-Null
    $lines.Add("- RootCause mode temporarily lowers global brightness to test whether the visible 5/6 flicker is brightness/electrical-threshold sensitive, then restores brightness to 50%.") | Out-Null
    $lines.Add("- StaticRoot mode compares fixed REC+AI output with and without a status query, separating dynamic-refresh flicker from static physical bleed or query/log interference.") | Out-Null
    $lines.Add("- TailOnly and ComboOnly collect `~LED:STATUS` after the human observation result, so serial status sampling and log output do not disturb the visible effect while the operator is watching.") | Out-Null
    $lines.Add("- Repro mode intentionally drives the status rail and key LEDs with a known-bad broad dynamic pattern while keeping software OK/WARN at zero, so human observation can separate logical status from physical cross-zone disturbance.") | Out-Null
    $lines.Add("- TailOnly mode is a narrow 1.9 confirmation: AI-only and REC+AI status-tail effects remain visibly but gently dynamic while LED5/OK and LED6/WARN stay physically and logically off.") | Out-Null
    $lines.Add("- ComboOnly mode is a narrow 1.9 confirmation for the combined REC+AI, EC11, and edge/frame effect without PWR/BLE/key participation.") | Out-Null
    $lines.Add("- Product direction for this pass: quiet but alive semantic status rail; blue BLE for connected/pairing/reconnect states, user re-pair uses BLE plus a synced low blue EC11 blink with edge/frame off, recording status reacts smoothly to volume, processing shows a purple da-dada thinking beat, EC11 press/rotate uses short white feedback, green OK only for success, amber/red WARN only for errors, and warm amber PWR+EC11 for shutdown confirmation.") | Out-Null
    $lines.Add("") | Out-Null
    $lines.Add("## Review Steps") | Out-Null
    $lines.Add("") | Out-Null
    $lines.Add("| # | id | application timing | semantic lights | expected | human focus | commands |") | Out-Null
    $lines.Add("|---:|---|---|---|---|---|---|") | Out-Null
    for ($i = 0; $i -lt $Steps.Count; $i++) {
        $step = $Steps[$i]
        $commands = ($step.commands | ForEach-Object { "``$_``" }) -join "<br>"
        $postCommands = ($step.post_commands | ForEach-Object { "``$_``" }) -join "<br>"
        if (-not [string]::IsNullOrWhiteSpace($postCommands)) {
            $commands = "$commands<br>post-observation:<br>$postCommands"
        }
        $row = "| {0} | {1} | {2} | {3} | {4} | {5} | {6} |" -f @(
            ($i + 1),
            $step.id,
            $step.when,
            (Get-LedSemanticText -Step $step),
            $step.expected,
            $step.human_focus,
            $commands)
        $lines.Add($row) | Out-Null
    }
    $lines | Set-Content -LiteralPath $planPath -Encoding UTF8
}

function Resolve-SerialPortName {
    param([Parameter(Mandatory = $true)][string]$RequestedPort)
    if ($RequestedPort -and $RequestedPort.ToLowerInvariant() -ne "comx") {
        return $RequestedPort.ToUpperInvariant()
    }

    $ports = @([System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object)
    if ($ports.Count -ne 1) {
        throw "COMx requires exactly one serial port; found $($ports.Count): $($ports -join ', ')"
    }
    return $ports[0].ToUpperInvariant()
}

function Open-SerialNoReset {
    param([Parameter(Mandatory = $true)][string]$PortName)
    $serial = [System.IO.Ports.SerialPort]::new(
        $PortName,
        $Baud,
        [System.IO.Ports.Parity]::None,
        8,
        [System.IO.Ports.StopBits]::One)
    $serial.ReadTimeout = 80
    $serial.WriteTimeout = 5000
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    $serial.Open()
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    return $serial
}

function Close-SerialQuiet {
    param([AllowNull()][System.IO.Ports.SerialPort]$Serial)
    if ($null -eq $Serial) {
        return
    }
    if ($Serial.IsOpen) {
        try {
            $Serial.Close()
        } catch {
        }
    }
    try {
        $Serial.Dispose()
    } catch {
    }
}

function Send-FinalLedOff {
    param([Parameter(Mandatory = $true)][string]$PortName)
    $cleanupSerial = $null
    try {
        $cleanupSerial = Open-SerialNoReset -PortName $PortName
        $cleanupSerial.Write("~LED:PREVIEW clear`n")
        Start-Sleep -Milliseconds 150
        $cleanupSerial.Write("~LED:OFF`n")
        Start-Sleep -Milliseconds 200
    } catch {
    } finally {
        Close-SerialQuiet -Serial $cleanupSerial
    }
}

function Add-SerialText {
    param([string]$Text)
    if ([string]::IsNullOrEmpty($Text)) {
        return
    }
    foreach ($line in ($Text -split "`r?`n")) {
        if (-not [string]::IsNullOrWhiteSpace($line)) {
            $serialLines.Add($line) | Out-Null
        }
    }
}

function Read-SerialFor {
    param(
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$Serial,
        [Parameter(Mandatory = $true)][int]$Milliseconds
    )
    $deadline = (Get-Date).AddMilliseconds($Milliseconds)
    $chunks = [System.Collections.Generic.List[string]]::new()
    while ((Get-Date) -lt $deadline) {
        try {
            $text = $Serial.ReadExisting()
            if (-not [string]::IsNullOrEmpty($text)) {
                $chunks.Add($text) | Out-Null
            }
        } catch {
            $chunks.Add("READ_ERROR $($_.Exception.Message)") | Out-Null
            break
        }
        Start-Sleep -Milliseconds 40
    }
    $joined = ($chunks -join "")
    Add-SerialText $joined
    return $joined
}

function Send-SerialCommand {
    param(
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$Serial,
        [Parameter(Mandatory = $true)][string]$Command,
        [int]$ReadMilliseconds = $CommandReadMs
    )
    $serialLines.Add(("> {0}" -f $Command)) | Out-Null
    $Serial.Write(("{0}`n" -f $Command))
    return Read-SerialFor -Serial $Serial -Milliseconds $ReadMilliseconds
}

function Invoke-LedReviewCommands {
    param(
        [Parameter(Mandatory = $true)][System.IO.Ports.SerialPort]$Serial,
        [Parameter(Mandatory = $true)][string[]]$Commands
    )

    foreach ($command in @($Commands)) {
        if ($command -match "^(WAIT|SLEEP)\s+(\d+)$") {
            $waitMs = [int]$Matches[2]
            $serialLines.Add(("> {0}" -f $command)) | Out-Null
            $response = Read-SerialFor -Serial $Serial -Milliseconds $waitMs
        } else {
            $readMs = $CommandReadMs
            if ($command -like "~LED:STATUS*" -and $readMs -lt 1800) {
                $readMs = 1800
            }
            $response = Send-SerialCommand -Serial $Serial -Command $command -ReadMilliseconds $readMs
            if ($command -like "~LED:STATUS*" -and [string]::IsNullOrWhiteSpace($response)) {
                $extraResponse = Read-SerialFor -Serial $Serial -Milliseconds 1200
                if (-not [string]::IsNullOrWhiteSpace($extraResponse)) {
                    $response = $extraResponse
                }
            }
        }
        [PSCustomObject]@{
            command = $command
            response = $response
        }
        Start-Sleep -Milliseconds 100
    }
}

function Get-StatusLines {
    param([Parameter(Mandatory = $true)][object[]]$Responses)
    $groups = [System.Collections.Generic.List[object]]::new()
    for ($i = 0; $i -lt $Responses.Count; $i++) {
        $response = $Responses[$i]
        if ([string]$response.command -notlike "~LED:STATUS*") {
            continue
        }
        $statusRgb = ""
        $ec11Rgb = ""
        $keyRgb = ""
        $edgeRgb = ""
        $summary = ""
        foreach ($line in ([string]$response.response -split "`r?`n")) {
            if ($line -like "~LED:STATUS detail=rgb status_rgb=*") {
                $statusRgb = $line
            } elseif ($line -like "~LED:STATUS detail=rgb_ec11 ec11_rgb=*") {
                $ec11Rgb = $line
            } elseif ($line -like "~LED:STATUS detail=rgb_key key_rgb=*") {
                $keyRgb = $line
            } elseif ($line -like "~LED:STATUS detail=rgb_edge edge_rgb=*") {
                $edgeRgb = $line
            } elseif ($line -like "~LED:STATUS profile=* detail=summary*") {
                $summary = $line
            }
        }
        if ([string]::IsNullOrWhiteSpace($statusRgb) -and [string]::IsNullOrWhiteSpace($summary)) {
            continue
        }
        $score = $i
        if ($summary -match "active_flags=[^ ]*:1") {
            $score += 1000
        }
        if ($summary -match "active_flags=[^ ]*BLE:1") {
            $score += 160
        }
        if ($summary -match "ble_repair_ms_left=(\d+)" -and [int]$Matches[1] -gt 0) {
            $score += 1200
        }
        $groups.Add([PSCustomObject]@{
            status_rgb = $statusRgb
            ec11_rgb = $ec11Rgb
            key_rgb = $keyRgb
            edge_rgb = $edgeRgb
            summary = $summary
            selection = ("status-command-{0}" -f ($i + 1))
            selection_score = $score
        }) | Out-Null
    }
    $selected = $groups | Sort-Object -Property selection_score -Descending | Select-Object -First 1
    if (-not $selected) {
        $selected = [PSCustomObject]@{
            status_rgb = ""
            ec11_rgb = ""
            key_rgb = ""
            edge_rgb = ""
            summary = ""
            selection = "none"
            selection_score = 0
        }
    }
    [PSCustomObject]@{
        status_rgb = $selected.status_rgb
        ec11_rgb = $selected.ec11_rgb
        key_rgb = $selected.key_rgb
        edge_rgb = $selected.edge_rgb
        summary = $selected.summary
        selection = $selected.selection
        selection_score = $selected.selection_score
    }
}

function Get-LedSemanticText {
    param([Parameter(Mandatory = $true)][object]$Step)

    switch ([string]$Step.id) {
        "scene-ready" { return "PWR=电源在线；BLE=已连接/就绪；REC、AI、OK、WARN 都应灭。" }
        "scene-pairing" { return "BLE=等待配对；旋钮/板框不参与；PWR 可独立表达插电/电源；REC、AI、OK、WARN 都不参与，WARN 不代表配对。" }
        "scene-reconnecting" { return "BLE=找回已绑定主机；旋钮/板框不参与；PWR 可独立表达插电/电源；WARN/错误灯不亮，因为重连不是错误。" }
        "scene-ble-repairing" { return "BLE=用户主动重新配对确认；EC11=低亮蓝色同步闪烁确认；板框不参与；AI、OK、WARN 必须灭。" }
        "scene-charging" { return "PWR=插电充电；BLE、REC、AI、OK、WARN 都不用于表达充电。" }
        "scene-full" { return "PWR=插电满电；OK 绿灯不亮，避免把满电误读为会话成功。" }
        "scene-low-battery" { return "PWR=低电量提示；WARN 不亮，除非进入真实错误/安全保护。" }
        "scene-critical-battery" { return "PWR=严重低电提示；其它语义灯保持灭，避免和录音/BLE/错误混在一起。" }
        "scene-recording-processing-live" { return "PWR/BLE=基础在线状态；REC=正在录音且只做慢呼吸；AI=host-confirmed processing/OTA；OK/WARN 灭。" }
        "scene-processing-live" { return "AI=host-confirmed processing/OTA；PWR/BLE 保持基线；旋钮/边框做顺时针紫色辅助；REC、OK、WARN 灭。" }
        "scene-ok" { return "OK=成功短确认，只在录音/处理完成时出现；PWR/BLE 保持基线；WARN 灭。" }
        "scene-rec-not-available" { return "WARN=录音不可用/被拒绝/权限或传输不满足；REC、AI、OK、旋钮、边框都不参与。" }
        "scene-shutdown-confirm" { return "PWR/旋钮=长按关机确认；板框不参与；BLE、REC、AI、OK、WARN 不抢占。" }
        "complex-shutdown-confirm" { return "PWR/旋钮=长按关机确认；板框不参与；BLE、REC、AI、OK、WARN 不抢占。" }
        "scene-ec11-short-press" { return "EC11=短按输入白色确认；OK/WARN 不参与；PWR/BLE 仍按自身状态显示。" }
        "scene-ec11-rotate" { return "EC11=旋转输入方向性白色确认；OK/WARN 不参与；PWR/BLE 仍按自身状态显示。" }
        "scene-key-feedback" { return "KEY=本地按键短白色反馈；REC、AI、OK、WARN、EC11、板框不参与。" }
        "scene-sleep" { return "睡眠/低功耗=全灭；BLE、AI、OK、WARN 都不应残留。" }
        "complex-capture-only" { return "REC=录音高级灯效；旋钮/边框做暖金辅助；PWR/BLE/AI/OK/WARN/按键不参与。" }
        "volume-capture-sweep" { return "REC=真实录音音量响应；麦克风实时音量只驱动状态 LED3 亮度；PWR/BLE 可保持自己的在线状态；AI/OK/WARN 不参与。" }
        "complex-recording-processing-product" { return "REC=录音暖金；AI=处理紫色语义；旋钮/边框保持低亮暖金慢速流动；OK/WARN 必须灭。" }
        "complex-processing-only" { return "AI=处理/OTA 紫色语义；旋钮/边框做低亮紫色顺时针辅助；REC、OK、WARN、按键不参与。" }
        default {
            if ([string]::IsNullOrWhiteSpace([string]$Step.when)) {
                return "基础/诊断步骤：按本步骤预期判断，不代表产品常态语义。"
            }
            return "按本步骤应用时机判断语义归属；未在预期中点名的灯都不应乱入。"
        }
    }
}

function Ensure-FormsLoaded {
    if ($NoPrompt.IsPresent) {
        return
    }
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    [System.Windows.Forms.Application]::EnableVisualStyles()
}

function Show-TopMostMessageBox {
    param(
        [Parameter(Mandatory = $true)][string]$Message,
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][System.Windows.Forms.MessageBoxButtons]$Buttons,
        [Parameter(Mandatory = $true)][System.Windows.Forms.MessageBoxIcon]$Icon
    )

    Ensure-FormsLoaded
    $owner = [System.Windows.Forms.Form]::new()
    try {
        $owner.StartPosition = "CenterScreen"
        $owner.ShowInTaskbar = $false
        $owner.TopMost = $true
        $owner.WindowState = [System.Windows.Forms.FormWindowState]::Minimized
        $owner.Show()
        $owner.Activate()
        return [System.Windows.Forms.MessageBox]::Show($owner, $Message, $Title, $Buttons, $Icon)
    } finally {
        $owner.Close()
        $owner.Dispose()
    }
}

function Show-IntroPrompt {
    param([Parameter(Mandatory = $true)][int]$StepCount)
    if ($NoPrompt.IsPresent) {
        return $true
    }
    Ensure-FormsLoaded
    if ($Mode -eq "Product") {
        $message = @"
这次只做整体产品灯效验收，不测单颗静态灯。

验收顺序：
1. 插电充电呼吸节奏。
2. 双击旋钮重新配对。
3. 真实录音+处理叠加，包含 PWR/BLE 状态灯。
4. 短按旋钮、旋转旋钮、按键反馈。
5. 睡眠熄灯。

重点看人眼效果：LED5/6 是否乱闪，重配时 BLE 加低亮旋钮确认是否存在，LED3 是否可随真实音量变亮，LED4 是否保持紫色并有哒  哒哒 AI 节奏，录音时旋钮 12 颗和边框 6 颗是否低亮且顺时针固定流动，实际旋钮/按键输入是否有短反馈且旋转反馈不会每格复位。
每个场景弹窗都会写明应用时机和灯位语义：BLE 蓝灯只用于配对/重连/重配，AI 灯只用于 host-confirmed processing/OTA，OK 绿灯只用于成功确认，WARN 只用于错误/警告。

共 $StepCount 个整体场景。准备好看板子后点确定；不方便就点取消。
"@
        $result = Show-TopMostMessageBox `
            -Message $message `
            -Title "Listener 整体产品灯效验收" `
            -Buttons ([System.Windows.Forms.MessageBoxButtons]::OKCancel) `
            -Icon ([System.Windows.Forms.MessageBoxIcon]::Information)
        return $result -eq [System.Windows.Forms.DialogResult]::OK
    }
    $message = @"
这次先做人眼基础验收，不继续闷头改复杂效果。

我希望看到的基础效果：
1. 单灯命令只亮一个真实 LED，其他灯完全不跟亮。
2. 亮度 10% / 35% / 70% 人眼递增，不能抖、不能平台化。
3. 录音+处理中允许 LED3/REC 随音量变亮、LED4/AI 做固定紫色哒  哒哒节奏，LED5/OK 与 LED6/WARN 必须灭，不能不规则跟前灯闪。
4. 音量响应模式会使用真实麦克风音量；当前产品策略要求只有状态 LED3 跟随音量，旋钮/边框不跟 PCM 亮度跳变。
5. 旋钮灯、按键灯、边框灯是独立区域，不能污染状态灯语义。

接下来会逐步发串口命令，每一步弹窗让你记录看到的颜色、亮度、闪烁、串灯。
共 $StepCount 步。准备好看板子后点确定；不方便就点取消。
"@
    $result = Show-TopMostMessageBox `
        -Message $message `
        -Title "Listener 灯效基础人眼确认" `
        -Buttons ([System.Windows.Forms.MessageBoxButtons]::OKCancel) `
        -Icon ([System.Windows.Forms.MessageBoxIcon]::Information)
    return $result -eq [System.Windows.Forms.DialogResult]::OK
}

function Show-StepPrompt {
    param(
        [Parameter(Mandatory = $true)][object]$Step,
        [Parameter(Mandatory = $true)][int]$Index,
        [Parameter(Mandatory = $true)][int]$Total,
        [Parameter(Mandatory = $true)][object]$StatusLines
    )
    if ($NoPrompt.IsPresent) {
        return [PSCustomObject]@{
            result = "SKIP"
            observed = "NoPrompt mode; no human observation captured."
            brightness = "unknown"
            unexpected_leds = ""
            irregular_flicker = $false
            led56_follow = $false
            brightness_wrong = $false
            notes = ""
        }
    }

    Ensure-FormsLoaded
    $form = [System.Windows.Forms.Form]::new()
    $form.Text = "灯效确认 $Index/$Total - $($Step.title)"
    $form.StartPosition = "CenterScreen"
    $form.TopMost = $true
    $form.Width = 850
    $form.Height = 775
    $form.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 9)

    $y = 12
    $title = [System.Windows.Forms.Label]::new()
    $title.Text = "$Index/$Total  $($Step.title)"
    $title.Left = 12
    $title.Top = $y
    $title.Width = 800
    $title.Height = 28
    $title.Font = [System.Drawing.Font]::new("Microsoft YaHei UI", 11, [System.Drawing.FontStyle]::Bold)
    $form.Controls.Add($title)
    $y += 36

    $expected = [System.Windows.Forms.TextBox]::new()
    $expected.Left = 12
    $expected.Top = $y
    $expected.Width = 805
    $expected.Height = 165
    $expected.Multiline = $true
    $expected.ReadOnly = $true
    $expected.ScrollBars = "Vertical"
    $whenText = if ([string]::IsNullOrWhiteSpace([string]$Step.when)) {
        "应用时机：本步骤用于基础/诊断确认，没有单独产品时机。"
    } else {
        "应用时机：$($Step.when)"
    }
    $semanticText = "灯位语义：$(Get-LedSemanticText -Step $Step)"
    $expected.Text = "$whenText`r`n$semanticText`r`n预期：$($Step.expected)`r`n观察重点：$($Step.human_focus)`r`n通过规则：$($Step.pass_rule)"
    $form.Controls.Add($expected)
    $y += 175

    $cmdBox = [System.Windows.Forms.TextBox]::new()
    $cmdBox.Left = 12
    $cmdBox.Top = $y
    $cmdBox.Width = 805
    $cmdBox.Height = 72
    $cmdBox.Multiline = $true
    $cmdBox.ReadOnly = $true
    $cmdBox.ScrollBars = "Vertical"
    $commandText = "观察前命令：`r`n" + (($Step.commands | ForEach-Object { "  $_" }) -join "`r`n")
    if (@($Step.post_commands).Count -gt 0) {
        $commandText += "`r`n观察后证据命令：`r`n" + (($Step.post_commands | ForEach-Object { "  $_" }) -join "`r`n")
    }
    $cmdBox.Text = $commandText
    $form.Controls.Add($cmdBox)
    $y += 82

    $statusBox = [System.Windows.Forms.TextBox]::new()
    $statusBox.Left = 12
    $statusBox.Top = $y
    $statusBox.Width = 805
    $statusBox.Height = 85
    $statusBox.Multiline = $true
    $statusBox.ReadOnly = $true
    $statusBox.ScrollBars = "Vertical"
    if ($StatusLines.selection_score -le 0) {
        $statusBox.Text = "串口状态：`r`n观察期间不查询 STATUS；你点通过/失败后再采集状态，避免串口查询影响肉眼观察。"
    } else {
        $statusBox.Text = "串口状态（选中 $($StatusLines.selection)）：`r`n$($StatusLines.status_rgb)`r`n$($StatusLines.ec11_rgb)`r`n$($StatusLines.key_rgb)`r`n$($StatusLines.edge_rgb)`r`n$($StatusLines.summary)"
    }
    $form.Controls.Add($statusBox)
    $y += 96

    $observedLabel = [System.Windows.Forms.Label]::new()
    $observedLabel.Text = "你人眼看到的效果（颜色、位置、是否像预期）："
    $observedLabel.Left = 12
    $observedLabel.Top = $y
    $observedLabel.Width = 360
    $observedLabel.Height = 22
    $form.Controls.Add($observedLabel)
    $y += 24

    $observed = [System.Windows.Forms.TextBox]::new()
    $observed.Left = 12
    $observed.Top = $y
    $observed.Width = 805
    $observed.Height = 68
    $observed.Multiline = $true
    $observed.ScrollBars = "Vertical"
    $form.Controls.Add($observed)
    $y += 78

    $brightnessLabel = [System.Windows.Forms.Label]::new()
    $brightnessLabel.Text = "主观亮度："
    $brightnessLabel.Left = 12
    $brightnessLabel.Top = $y + 4
    $brightnessLabel.Width = 90
    $form.Controls.Add($brightnessLabel)

    $brightness = [System.Windows.Forms.ComboBox]::new()
    $brightness.Left = 100
    $brightness.Top = $y
    $brightness.Width = 190
    $brightness.DropDownStyle = "DropDownList"
    [void]$brightness.Items.AddRange(@("看不见/灭", "很暗", "偏暗但可用", "合适", "偏亮", "过亮刺眼", "无法判断"))
    $brightness.SelectedIndex = 6
    $form.Controls.Add($brightness)

    $unexpectedLabel = [System.Windows.Forms.Label]::new()
    $unexpectedLabel.Text = "意外亮起的灯："
    $unexpectedLabel.Left = 315
    $unexpectedLabel.Top = $y + 4
    $unexpectedLabel.Width = 110
    $form.Controls.Add($unexpectedLabel)

    $unexpected = [System.Windows.Forms.TextBox]::new()
    $unexpected.Left = 425
    $unexpected.Top = $y
    $unexpected.Width = 392
    $form.Controls.Add($unexpected)
    $y += 38

    $flicker = [System.Windows.Forms.CheckBox]::new()
    $flicker.Text = "有不规则闪烁/抖动"
    $flicker.Left = 12
    $flicker.Top = $y
    $flicker.Width = 180
    $form.Controls.Add($flicker)

    $tailFollow = [System.Windows.Forms.CheckBox]::new()
    $tailFollow.Text = "LED5/6 跟着前灯闪"
    $tailFollow.Left = 205
    $tailFollow.Top = $y
    $tailFollow.Width = 180
    $form.Controls.Add($tailFollow)

    $brightnessWrong = [System.Windows.Forms.CheckBox]::new()
    $brightnessWrong.Text = "亮度不符合/不递增"
    $brightnessWrong.Left = 405
    $brightnessWrong.Top = $y
    $brightnessWrong.Width = 180
    $form.Controls.Add($brightnessWrong)
    $y += 36

    $notesLabel = [System.Windows.Forms.Label]::new()
    $notesLabel.Text = "备注（比如：5/6 会随机同色闪一下；70% 和 35% 差不多）："
    $notesLabel.Left = 12
    $notesLabel.Top = $y
    $notesLabel.Width = 650
    $form.Controls.Add($notesLabel)
    $y += 22

    $notes = [System.Windows.Forms.TextBox]::new()
    $notes.Left = 12
    $notes.Top = $y
    $notes.Width = 805
    $notes.Height = 55
    $notes.Multiline = $true
    $notes.ScrollBars = "Vertical"
    $form.Controls.Add($notes)
    $y += 68

    $holder = @{ result = "ABORT" }
    $passButton = [System.Windows.Forms.Button]::new()
    $passButton.Text = "通过"
    $passButton.Left = 496
    $passButton.Top = $y
    $passButton.Width = 100
    $passButton.Add_Click({ $holder.result = "PASS"; $form.Close() })
    $form.Controls.Add($passButton)

    $failButton = [System.Windows.Forms.Button]::new()
    $failButton.Text = "失败"
    $failButton.Left = 606
    $failButton.Top = $y
    $failButton.Width = 100
    $failButton.Add_Click({ $holder.result = "FAIL"; $form.Close() })
    $form.Controls.Add($failButton)

    $skipButton = [System.Windows.Forms.Button]::new()
    $skipButton.Text = "跳过/不确定"
    $skipButton.Left = 716
    $skipButton.Top = $y
    $skipButton.Width = 100
    $skipButton.Add_Click({ $holder.result = "SKIP"; $form.Close() })
    $form.Controls.Add($skipButton)

    [void]$form.ShowDialog()
    return [PSCustomObject]@{
        result = $holder.result
        observed = $observed.Text
        brightness = [string]$brightness.SelectedItem
        unexpected_leds = $unexpected.Text
        irregular_flicker = $flicker.Checked
        led56_follow = $tailFollow.Checked
        brightness_wrong = $brightnessWrong.Checked
        notes = $notes.Text
    }
}

function Show-StepStartPrompt {
    param(
        [Parameter(Mandatory = $true)][object]$Step,
        [Parameter(Mandatory = $true)][int]$Index,
        [Parameter(Mandatory = $true)][int]$Total
    )
    if ($NoPrompt.IsPresent) {
        return $true
    }
    Ensure-FormsLoaded
    $whenText = if ([string]::IsNullOrWhiteSpace([string]$Step.when)) {
        "本步骤用于基础/诊断确认，没有单独产品时机。"
    } else {
        $Step.when
    }
    $message = @"
第 $Index / $Total 步即将开始：$($Step.title)

点确定后我会立刻发送这一组串口命令，请马上看板子。

应用时机：
$whenText

灯位语义：
$(Get-LedSemanticText -Step $Step)

预期：
$($Step.expected)

观察重点：
$($Step.human_focus)
"@
    $result = Show-TopMostMessageBox `
        -Message $message `
        -Title "即将播放灯效 $Index/$Total" `
        -Buttons ([System.Windows.Forms.MessageBoxButtons]::OKCancel) `
        -Icon ([System.Windows.Forms.MessageBoxIcon]::Information)
    return $result -eq [System.Windows.Forms.DialogResult]::OK
}

function Write-Outputs {
    param(
        [Parameter(Mandatory = $true)][object[]]$Steps,
        [Parameter(Mandatory = $true)][string]$Result,
        [string]$PortName = ""
    )
    $serialLines | Set-Content -LiteralPath $serialPath -Encoding UTF8
    $passCount = @($records | Where-Object { $_.human.result -eq "PASS" }).Count
    $failCount = @($records | Where-Object { $_.human.result -eq "FAIL" }).Count
    $skipCount = @($records | Where-Object { $_.human.result -eq "SKIP" }).Count
    $tailFollowCount = @($records | Where-Object { $_.human.led56_follow }).Count
    $flickerCount = @($records | Where-Object { $_.human.irregular_flicker }).Count
    $brightnessWrongCount = @($records | Where-Object { $_.human.brightness_wrong }).Count

    $summary = [ordered]@{
        schema_version = 1
        generated_at = (Get-Date).ToString("o")
        result = $Result
        mode = $Mode
        port = $PortName
        step_count = $Steps.Count
        observed_count = $records.Count
        pass_count = $passCount
        fail_count = $failCount
        skip_count = $skipCount
        led56_follow_count = $tailFollowCount
        irregular_flicker_count = $flickerCount
        brightness_wrong_count = $brightnessWrongCount
        plan = $planPath
        session_jsonl = $sessionPath
        serial_log = $serialPath
    }
    $summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryJsonPath -Encoding UTF8

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("# Status LED human effect review summary") | Out-Null
    $lines.Add("") | Out-Null
    $lines.Add(("- Result: {0}" -f $Result)) | Out-Null
    $lines.Add(("- Mode: {0}" -f $Mode)) | Out-Null
    $lines.Add(("- Port: {0}" -f $PortName)) | Out-Null
    $lines.Add(("- Pass/Fail/Skip: {0} / {1} / {2}" -f $passCount, $failCount, $skipCount)) | Out-Null
    $lines.Add(("- LED5/6 follow reports: {0}" -f $tailFollowCount)) | Out-Null
    $lines.Add(("- Irregular flicker reports: {0}" -f $flickerCount)) | Out-Null
    $lines.Add(("- Brightness problem reports: {0}" -f $brightnessWrongCount)) | Out-Null
    $lines.Add(("- Plan: {0}" -f $planPath)) | Out-Null
    $lines.Add(("- Session JSONL: {0}" -f $sessionPath)) | Out-Null
    $lines.Add(("- Serial log: {0}" -f $serialPath)) | Out-Null
    $lines.Add("") | Out-Null
    $lines.Add("## Human Observations") | Out-Null
    $lines.Add("") | Out-Null
    $lines.Add("| # | id | application timing | semantic lights | result | brightness | LED5/6 follow | flicker | unexpected | observed | notes |") | Out-Null
    $lines.Add("|---:|---|---|---|---|---|---|---|---|---|---|") | Out-Null
    for ($i = 0; $i -lt $records.Count; $i++) {
        $record = $records[$i]
        $h = $record.human
        $row = "| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} | {8} | {9} | {10} |" -f @(
            ($i + 1),
            $record.step_id,
            (($record.when -replace "\|", "/") -replace "`r?`n", " "),
            (($record.semantic -replace "\|", "/") -replace "`r?`n", " "),
            $h.result,
            (($h.brightness -replace "\|", "/") -replace "`r?`n", " "),
            $h.led56_follow,
            $h.irregular_flicker,
            (($h.unexpected_leds -replace "\|", "/") -replace "`r?`n", " "),
            (($h.observed -replace "\|", "/") -replace "`r?`n", " "),
            (($h.notes -replace "\|", "/") -replace "`r?`n", " "))
        $lines.Add($row) | Out-Null
    }
    $lines | Set-Content -LiteralPath $summaryMdPath -Encoding UTF8
}

$steps = @(Get-ReviewSteps)
Write-PlanMarkdown -Steps $steps

if ($PlanOnly.IsPresent) {
    Write-Outputs -Steps $steps -Result "PLAN_ONLY"
    Write-Host "PLAN_ONLY: $planPath"
    exit 0
}

$portName = Resolve-SerialPortName -RequestedPort $Port
if (-not (Show-IntroPrompt -StepCount $steps.Count)) {
    Write-Outputs -Steps $steps -Result "ABORTED_BY_OPERATOR" -PortName $portName
    Write-Host "ABORTED_BY_OPERATOR: $summaryMdPath"
    exit 2
}

$serial = $null
try {
    $serial = Open-SerialNoReset -PortName $portName
    [void](Read-SerialFor -Serial $serial -Milliseconds $InitialReadMs)
    Close-SerialQuiet -Serial $serial
    $serial = $null
    for ($index = 0; $index -lt $steps.Count; $index++) {
        $step = $steps[$index]
        $preclearResponses = [System.Collections.Generic.List[object]]::new()
        if ($step.id -ne "foundation-off") {
            $serial = Open-SerialNoReset -PortName $portName
            [void](Read-SerialFor -Serial $serial -Milliseconds 180)
            foreach ($preclearCommand in @("~LED:PREVIEW clear", "~LED:OFF")) {
                $preclearResponse = Send-SerialCommand -Serial $serial -Command $preclearCommand -ReadMilliseconds $PreclearReadMs
                $preclearResponses.Add([PSCustomObject]@{
                    command = $preclearCommand
                    response = $preclearResponse
                }) | Out-Null
                Start-Sleep -Milliseconds 80
            }
            Close-SerialQuiet -Serial $serial
            $serial = $null
        }
        if (-not (Show-StepStartPrompt -Step $step -Index ($index + 1) -Total $steps.Count)) {
            Write-Outputs -Steps $steps -Result "ABORTED_BY_OPERATOR" -PortName $portName
            exit 2
        }
        $serial = Open-SerialNoReset -PortName $portName
        [void](Read-SerialFor -Serial $serial -Milliseconds 180)
        $responses = @(Invoke-LedReviewCommands -Serial $serial -Commands @($step.commands))
        Close-SerialQuiet -Serial $serial
        $serial = $null
        $promptStatusLines = Get-StatusLines -Responses $responses
        $human = Show-StepPrompt -Step $step -Index ($index + 1) -Total $steps.Count -StatusLines $promptStatusLines
        $postResponses = @()
        if (@($step.post_commands).Count -gt 0 -and $human.result -ne "ABORT") {
            $serial = Open-SerialNoReset -PortName $portName
            [void](Read-SerialFor -Serial $serial -Milliseconds 180)
            $postResponses = @(Invoke-LedReviewCommands -Serial $serial -Commands @($step.post_commands))
            Close-SerialQuiet -Serial $serial
            $serial = $null
        }
        $statusLines = Get-StatusLines -Responses (@($responses) + @($postResponses))
        $record = [PSCustomObject]@{
            at = (Get-Date).ToString("o")
            mode = $Mode
            port = $portName
            sequence_index = $index + 1
            step_id = $step.id
            title = $step.title
            when = $step.when
            semantic = (Get-LedSemanticText -Step $step)
            expected = $step.expected
            human_focus = $step.human_focus
            commands = @($step.commands)
            post_commands = @($step.post_commands)
            preclear_responses = @($preclearResponses)
            responses = @($responses)
            post_responses = @($postResponses)
            prompt_status_rgb = $promptStatusLines.status_rgb
            prompt_status_summary = $promptStatusLines.summary
            prompt_status_selection = $promptStatusLines.selection
            status_rgb = $statusLines.status_rgb
            ec11_rgb = $statusLines.ec11_rgb
            key_rgb = $statusLines.key_rgb
            edge_rgb = $statusLines.edge_rgb
            status_summary = $statusLines.summary
            status_selection = $statusLines.selection
            status_selection_score = $statusLines.selection_score
            human = $human
        }
        $records.Add($record) | Out-Null
        ($record | ConvertTo-Json -Depth 12 -Compress) | Add-Content -LiteralPath $sessionPath -Encoding UTF8
        if ($human.result -eq "ABORT") {
            Write-Outputs -Steps $steps -Result "ABORTED_BY_OPERATOR" -PortName $portName
            exit 2
        }
    }
} finally {
    Close-SerialQuiet -Serial $serial
    Send-FinalLedOff -PortName $portName
}

$result = "HUMAN_REVIEW_PASS"
if (@($records | Where-Object { $_.human.result -eq "FAIL" -or $_.human.led56_follow -or $_.human.irregular_flicker -or $_.human.brightness_wrong }).Count -gt 0) {
    $result = "HUMAN_REVIEW_FAIL"
} elseif (@($records | Where-Object { $_.human.result -ne "PASS" }).Count -gt 0) {
    $result = "HUMAN_REVIEW_INCOMPLETE"
}
Write-Outputs -Steps $steps -Result $result -PortName $portName
Write-Host "result=$result"
Write-Host "summary=$summaryMdPath"
Write-Host "session=$sessionPath"
Write-Host "serial=$serialPath"

if ($result -ne "HUMAN_REVIEW_PASS") {
    exit 1
}
