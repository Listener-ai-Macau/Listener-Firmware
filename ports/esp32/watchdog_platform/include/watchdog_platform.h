#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t watchdog_platform_subscribe_current_task(const char *task_name);
void watchdog_platform_feed_current_task(void);
void watchdog_platform_delay_ms(uint32_t delay_ms);
uint32_t watchdog_platform_task_notify_take(BaseType_t clear_on_exit, uint32_t wait_ms);
uint32_t watchdog_platform_task_notify_take_low_power(BaseType_t clear_on_exit, uint32_t wait_ms);
esp_err_t watchdog_platform_enter_shutdown_critical(const char *reason);
esp_err_t watchdog_platform_exit_shutdown_critical(void);
void watchdog_platform_log_config(void);
bool watchdog_platform_consume_usb_command(const char *line);

/* 在 PSRAM 上分配任务栈并启动任务（heap_caps_malloc(MALLOC_CAP_SPIRAM) + xTaskCreateStatic）。
 * 用于把低频/事件驱动/瞬态任务的栈从片内 SRAM 外移到 8MB PSRAM，缓解 boot 期
 * internal DRAM 极度紧张（实测只剩 ~5-8KB，任何新 task 的栈分不到就红灯/NO_MEM）。
 *
 * 调用方需提供两个持久的文件级 static 变量：StaticTask_t 控制块 + StackType_t* 栈指针，
 * 每个任务独立、不可共享。返回 pdPASS/pdFAIL，失败时内部已释放栈内存。
 *
 * 约束：
 * - 仅用于低频/非实时任务。高频热路径（音频 capture/afe_fetch/stream，每帧访问栈）
 *   必须留片内，PSRAM 栈的 cache 延迟会导致掉帧。
 * - **绝对不能用于会执行 flash 操作的任务**（spi_flash_*、esp_partition_write/erase、
 *   esp_ota_*、nvs_commit/nvs_set_* 等，含间接调用）。flash 操作期间 cache 被关闭，
 *   此时若调用任务的栈在 PSRAM，会触发
 *   `esp_task_stack_is_sane_cache_disabled` assert → boot loop。
 *   因此 diag_log_writer / ble_ota_worker / power_manager 等 flash/NVS/OTA 任务必须留片内。
 * 前提 Kconfig：CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y（本项目已开）。 */
BaseType_t watchdog_platform_start_task_on_spiram(
    TaskFunction_t task_func,
    const char *name,
    uint32_t stack_words,
    UBaseType_t priority,
    TaskHandle_t *handle_out,
    StaticTask_t *task_control,
    StackType_t **task_stack);

#ifdef __cplusplus
}
#endif
