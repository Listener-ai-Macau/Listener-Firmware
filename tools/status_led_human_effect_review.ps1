[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Port = "COMx",
    [int]$Baud = 115200,
    [string]$OutputDir = "",
    [ValidateSet("Foundation", "Scenes", "Complex", "Volume", "RootCause", "StaticRoot", "Repro", "Full")]
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
        [string]$PassRule = ""
    )

    [PSCustomObject]@{
        id = $Id
        title = $Title
        commands = @($Commands)
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
            -Expected "专用 effect-only 状态轨：只有 LED3 金色录音和 LED4 紫色/蓝紫处理参与；PWR/BLE、旋钮、边框不参与；LED5/OK 和 LED6/WARN 必须保持熄灭。" `
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
            -Commands @("~LED:PREVIEW ready", "~LED:STATUS") `
            -Expected "PWR/BLE 清楚但不抢眼；其它语义灯不亮。" `
            -HumanFocus "确认状态轨安静，不像跑马灯。"
        New-LedReviewStep `
            -Id "scene-ok" `
            -Title "产品场景：OK 确认" `
            -Commands @("~LED:PREVIEW ok", "~LED:STATUS") `
            -Expected "LED5 绿色短确认，旋钮/边框可有低亮绿色辅助；不应长时间常亮。" `
            -HumanFocus "确认 OK 位置和持续感，不能消失太快或太暗。"
        New-LedReviewStep `
            -Id "scene-rec-not-available" `
            -Title "产品场景：录音不可用警告" `
            -Commands @("~LED:PREVIEW rec_not_available", "~LED:STATUS") `
            -Expected "WARN 与 REC 成对提示，颜色应像警告，不应误读为正常录音。" `
            -HumanFocus "确认错误灯可以覆盖 5/6，且和普通闪烁区别明显。"
        New-LedReviewStep `
            -Id "scene-shutdown-confirm" `
            -Title "产品场景：长按关机确认" `
            -Commands @("~LED:PREVIEW shutdown_confirm", "~LED:STATUS") `
            -Expected "暖琥珀 PWR + 旋钮进度感 + 边框角落提示；状态灯语义仍清楚。" `
            -HumanFocus "确认这是关机确认感，不像错误或录音。"
    )
    return @($steps)
}

function Get-ComplexSteps {
    $steps = @(
        New-LedReviewStep `
            -Id "complex-recording-processing-product" `
            -Title "复杂灯效：录音+处理 专用动态效果" `
            -Commands @("~LED:PREVIEW recording_processing_led_only", "~LED:REC_LEVEL 100 60000", "~LED:STATUS", "~LED:STATUS", "~LED:STATUS") `
            -Expected "专用 effect-only 预览：状态灯只看 REC/AI，PWR/BLE 不参与；KEY1/KEY2 允许低亮工作态辅助；旋钮/边框由录音暖金优先，带同色慢速高光/扫动，不做同区金紫混色；LED5/OK 和 LED6/WARN 不应跟闪。" `
            -HumanFocus "按灯效本身判断：按键辅助是否克制，旋钮底座环是否有稳定音量感且不是全静态，边框是否像环境支撑而不是乱闪；叠加态应该有同色高光运动感；~LED:STATUS 必须显示 preview_effect_only=1。"
        New-LedReviewStep `
            -Id "complex-recording-processing-status-only" `
            -Title "复杂灯效：录音+处理 仅状态灯动态" `
            -Commands @("~LED:PREVIEW recording_processing_status_led_only", "~LED:STATUS", "~LED:STATUS", "~LED:STATUS") `
            -Expected "专用 effect-only 状态轨：只保留 LED3/REC 和 LED4/AI 的低频量化动态；PWR/BLE、旋钮和边框应熄灭；LED5/OK 和 LED6/WARN 不应跟闪。" `
            -HumanFocus "这里验证动态状态轨本身：如果这里稳定，说明状态灯高级动态可用；如果仍闪，问题在状态灯物理链路、日志或刷新策略。"
        New-LedReviewStep `
            -Id "complex-capture-only" `
            -Title "复杂灯效：仅录音 暖金旋转底光" `
            -Commands @("~LED:PREVIEW capture_led_only", "~LED:REC_LEVEL 75 60000", "~LED:STATUS", "~LED:STATUS", "~LED:STATUS") `
            -Expected "专用 effect-only 预览：LED3 金色录音随音量和慢呼吸有克制亮度变化；KEY1 可有低亮暖金辅助；旋钮底座一圈应是暖金低亮底光加同色高光旋转，边框应是更低亮度的暖金框体支撑；不应靠缺几颗灯表达亮度；LED5/6 应保持熄灭。" `
            -HumanFocus "看单独录音是否像旋钮底座氛围灯，按键辅助是否不抢眼，是否稳定不闪，旋钮不要像随机闪或缺灯；~LED:STATUS 必须显示 preview_effect_only=1。"
        New-LedReviewStep `
            -Id "complex-processing-only" `
            -Title "复杂灯效：仅处理 紫色环绕" `
            -Commands @("~LED:PREVIEW processing_led_only", "~LED:STATUS", "~LED:STATUS", "~LED:STATUS") `
            -Expected "专用 effect-only 预览：LED4 紫色处理可低频量化呼吸；KEY2 可有低亮紫色辅助；旋钮底座一圈应有低亮紫色底光和一个慢速环绕高光，边框应有更低亮度的紫色框体波动；LED5/6 应保持熄灭，且查询状态不应导致重启。" `
            -HumanFocus "看单独处理是否有围绕旋钮的旋转感、按键辅助是否克制、边框是否好看且克制、是否稳定不闪；~LED:STATUS 必须显示 preview_effect_only=1。"
        New-LedReviewStep `
            -Id "complex-shutdown-confirm" `
            -Title "复杂灯效：长按关机确认" `
            -Commands @("~LED:PREVIEW shutdown_confirm", "~LED:STATUS", "~LED:STATUS", "~LED:STATUS") `
            -Expected "暖琥珀 PWR + 旋钮进度/边框角落提示；应明显像关机确认，不像错误或录音。" `
            -HumanFocus "确认长按关机确认灯效是否能被人眼理解。"
    )
    return @($steps)
}

function Get-VolumeSteps {
    $steps = @(
        New-LedReviewStep `
            -Id "volume-capture-sweep" `
            -Title "音量响应：录音动态扫动" `
            -Commands @(
                "~LED:PREVIEW capture_led_only",
                "~LED:REC_LEVEL 0 2200",
                "WAIT 1300",
                "~LED:REC_LEVEL 30 2200",
                "WAIT 1300",
                "~LED:REC_LEVEL 65 2200",
                "WAIT 1300",
                "~LED:REC_LEVEL 100 60000",
                "WAIT 2200",
                "~LED:STATUS") `
            -Expected "专用 effect-only 预览：LED3/REC、KEY1 低亮暖金辅助、旋钮暖金音量弧、边框暖金侧轨应按 0/30/65/100 四档明显变亮、变长；高音量时有小范围同色高光顺滑移动；PWR/BLE 不参与；LED5/6 必须灭。" `
            -HumanFocus "这是动态音量验收：点确定后马上盯着灯，每档约 2 秒；重点看旋钮/边框是否有可接受的音量跟随，按键辅助是否不抢眼，颜色是否是暖金而不是绿闪，高音量不能像完全静态或一开始跳闪；允许不是全环全框都亮，因为当前硬件上 broad 动态会触发 5/6；~LED:STATUS 必须显示 preview_effect_only=1。"
        New-LedReviewStep `
            -Id "volume-overlap-high" `
            -Title "音量响应：录音+处理 高音量叠加" `
            -Commands @("~LED:PREVIEW recording_processing_led_only", "~LED:REC_LEVEL 100 60000", "WAIT 1800", "~LED:STATUS", "WAIT 700", "~LED:STATUS", "WAIT 700", "~LED:STATUS") `
            -Expected "专用 effect-only 预览：REC 暖金高音量反馈和 AI 紫色语义可同时存在；PWR/BLE 不参与；KEY1/KEY2 允许低亮工作态辅助；状态 LED4 仍显示 AI 紫色，但旋钮/边框应由录音暖金优先，带同相位的小范围高光/扫动，不做同区金紫混色；LED5/6 必须灭，不应跟 LED3/4 闪。" `
            -HumanFocus "这是最接近之前闪烁痛点的高级效果验收：重点看暖金是否稳定保持、开始阶段是否跳闪、按键辅助是否克制、板框是否是低亮支撑而不是乱闪；还要看是否有 5/6 跟闪或随机绿闪；~LED:STATUS 必须显示 preview_effect_only=1。"
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
    $lines.Add("- Volume mode drives the same recording-level renderer with `~LED:REC_LEVEL`; this pass uses effect-only preview commands so PWR/BLE, live reconnects, and charge-state changes do not participate in the judged effect.") | Out-Null
    $lines.Add("- EC11, key, and edge/frame LEDs are independent accent surfaces. For this pass, EC11 uses a knob-base warm-gold volume arc for REC and a slow violet orbit for AI-only; REC+AI overlap gives EC11/edge priority to recording warm gold with a slow same-color highlight instead of same-zone color mixing.") | Out-Null
    $lines.Add("- Complex mode uses effect-only preview commands for REC/AI, EC11, and edge/frame validation; ordinary product previews remain in Scenes, not in the LED-effect tuning pass.") | Out-Null
    $lines.Add("- RootCause mode temporarily lowers global brightness to test whether the visible 5/6 flicker is brightness/electrical-threshold sensitive, then restores brightness to 50%.") | Out-Null
    $lines.Add("- StaticRoot mode compares fixed REC+AI output with and without a status query, separating dynamic-refresh flicker from static physical bleed or query/log interference.") | Out-Null
    $lines.Add("- Repro mode intentionally drives the status rail and key LEDs with a known-bad broad dynamic pattern while keeping software OK/WARN at zero, so human observation can separate logical status from physical cross-zone disturbance.") | Out-Null
    $lines.Add("- Product direction for this pass: quiet but alive semantic status rail; mature controller/speaker-style ring feedback on the EC11 base; restrained edge/frame support; green OK only for success; amber/red WARN only for errors; warm amber for shutdown confirmation.") | Out-Null
    $lines.Add("") | Out-Null
    $lines.Add("## Review Steps") | Out-Null
    $lines.Add("") | Out-Null
    $lines.Add("| # | id | expected | human focus | commands |") | Out-Null
    $lines.Add("|---:|---|---|---|---|") | Out-Null
    for ($i = 0; $i -lt $Steps.Count; $i++) {
        $step = $Steps[$i]
        $commands = ($step.commands | ForEach-Object { "``$_``" }) -join "<br>"
        $row = "| {0} | {1} | {2} | {3} | {4} |" -f @(
            ($i + 1),
            $step.id,
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
    $serial.WriteTimeout = 1000
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

function Get-StatusLines {
    param([Parameter(Mandatory = $true)][object[]]$Responses)
    $text = (($Responses | ForEach-Object { [string]$_.response }) -join "`n")
    $statusRgb = ""
    $ec11Rgb = ""
    $keyRgb = ""
    $edgeRgb = ""
    $summary = ""
    foreach ($line in ($text -split "`r?`n")) {
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
    [PSCustomObject]@{
        status_rgb = $statusRgb
        ec11_rgb = $ec11Rgb
        key_rgb = $keyRgb
        edge_rgb = $edgeRgb
        summary = $summary
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

function Show-IntroPrompt {
    param([Parameter(Mandatory = $true)][int]$StepCount)
    if ($NoPrompt.IsPresent) {
        return $true
    }
    Ensure-FormsLoaded
    $message = @"
这次先做人眼基础验收，不继续闷头改复杂效果。

我希望看到的基础效果：
1. 单灯命令只亮一个真实 LED，其他灯完全不跟亮。
2. 亮度 10% / 35% / 70% 人眼递增，不能抖、不能平台化。
3. 录音+处理中允许 LED3/REC 和 LED4/AI 做低频量化亮度变化，LED5/OK 与 LED6/WARN 必须灭，不能不规则跟前灯闪。
4. 音量响应模式会模拟低/中/高 rec_level；你要判断人眼是否真的看到亮度层级，而不是只看日志。
5. 旋钮灯、按键灯、边框灯是独立区域，不能污染状态灯语义。

接下来会逐步发串口命令，每一步弹窗让你记录看到的颜色、亮度、闪烁、串灯。
共 $StepCount 步。准备好看板子后点“确定”；不方便就点“取消”。
"@
    $result = [System.Windows.Forms.MessageBox]::Show(
        $message,
        "Listener 灯效基础人眼确认",
        [System.Windows.Forms.MessageBoxButtons]::OKCancel,
        [System.Windows.Forms.MessageBoxIcon]::Information)
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
    $form.Height = 690
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
    $expected.Height = 105
    $expected.Multiline = $true
    $expected.ReadOnly = $true
    $expected.ScrollBars = "Vertical"
    $expected.Text = "预期：$($Step.expected)`r`n观察重点：$($Step.human_focus)`r`n通过规则：$($Step.pass_rule)"
    $form.Controls.Add($expected)
    $y += 115

    $cmdBox = [System.Windows.Forms.TextBox]::new()
    $cmdBox.Left = 12
    $cmdBox.Top = $y
    $cmdBox.Width = 805
    $cmdBox.Height = 72
    $cmdBox.Multiline = $true
    $cmdBox.ReadOnly = $true
    $cmdBox.ScrollBars = "Vertical"
    $cmdBox.Text = "命令：`r`n" + (($Step.commands | ForEach-Object { "  $_" }) -join "`r`n")
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
    $statusBox.Text = "串口状态：`r`n$($StatusLines.status_rgb)`r`n$($StatusLines.ec11_rgb)`r`n$($StatusLines.key_rgb)`r`n$($StatusLines.edge_rgb)`r`n$($StatusLines.summary)"
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
    $message = @"
第 $Index / $Total 步即将开始：$($Step.title)

点“确定”后我会立刻发送这一组串口命令，请马上看板子。

预期：
$($Step.expected)

观察重点：
$($Step.human_focus)
"@
    $result = [System.Windows.Forms.MessageBox]::Show(
        $message,
        "即将播放灯效 $Index/$Total",
        [System.Windows.Forms.MessageBoxButtons]::OKCancel,
        [System.Windows.Forms.MessageBoxIcon]::Information)
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
    $lines.Add("| # | id | result | brightness | LED5/6 follow | flicker | unexpected | observed | notes |") | Out-Null
    $lines.Add("|---:|---|---|---|---|---|---|---|---|") | Out-Null
    for ($i = 0; $i -lt $records.Count; $i++) {
        $record = $records[$i]
        $h = $record.human
        $row = "| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} | {8} |" -f @(
            ($i + 1),
            $record.step_id,
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
        $responses = [System.Collections.Generic.List[object]]::new()
        foreach ($command in @($step.commands)) {
            if ($command -match "^(WAIT|SLEEP)\s+(\d+)$") {
                $waitMs = [int]$Matches[2]
                $serialLines.Add(("> {0}" -f $command)) | Out-Null
                $response = Read-SerialFor -Serial $serial -Milliseconds $waitMs
            } else {
                $readMs = $CommandReadMs
                if ($command -like "~LED:STATUS*" -and $readMs -lt 1800) {
                    $readMs = 1800
                }
                $response = Send-SerialCommand -Serial $serial -Command $command -ReadMilliseconds $readMs
                if ($command -like "~LED:STATUS*" -and [string]::IsNullOrWhiteSpace($response)) {
                    $extraResponse = Read-SerialFor -Serial $serial -Milliseconds 1200
                    if (-not [string]::IsNullOrWhiteSpace($extraResponse)) {
                        $response = $extraResponse
                    }
                }
            }
            $responses.Add([PSCustomObject]@{
                command = $command
                response = $response
            }) | Out-Null
            Start-Sleep -Milliseconds 100
        }
        Close-SerialQuiet -Serial $serial
        $serial = $null
        $statusLines = Get-StatusLines -Responses $responses
        $human = Show-StepPrompt -Step $step -Index ($index + 1) -Total $steps.Count -StatusLines $statusLines
        $record = [PSCustomObject]@{
            at = (Get-Date).ToString("o")
            mode = $Mode
            port = $portName
            sequence_index = $index + 1
            step_id = $step.id
            title = $step.title
            expected = $step.expected
            human_focus = $step.human_focus
            commands = @($step.commands)
            preclear_responses = @($preclearResponses)
            responses = @($responses)
            status_rgb = $statusLines.status_rgb
            ec11_rgb = $statusLines.ec11_rgb
            key_rgb = $statusLines.key_rgb
            edge_rgb = $statusLines.edge_rgb
            status_summary = $statusLines.summary
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
