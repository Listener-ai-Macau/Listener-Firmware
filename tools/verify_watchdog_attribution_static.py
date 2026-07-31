from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (
    ROOT
    / "ports"
    / "esp32"
    / "watchdog_platform"
    / "watchdog_platform_esp32.c"
).read_text(encoding="utf-8")

REQUIRED = (
    "static RTC_NOINIT_ATTR watchdog_platform_rtc_timeout_t s_rtc_timeout;",
    "void IRAM_ATTR esp_task_wdt_isr_user_handler(void)",
    "esp_task_wdt_print_triggered_tasks(",
    "xTaskGetCurrentTaskHandleForCore(0)",
    "xTaskGetCurrentTaskHandleForCore(1)",
    "failed_core_mask=0x%08",
    'missed=\\"%s\\"',
    'running_cpu0=\\"%s\\" running_cpu1=\\"%s\\"',
)


def main() -> int:
    missing = [fragment for fragment in REQUIRED if fragment not in SOURCE]
    if missing:
        print("FAIL: retained Task WDT attribution contract")
        for fragment in missing:
            print(f"missing: {fragment}")
        return 1
    if "CONFIG_ESP_TASK_WDT_TIMEOUT_S=5" not in (
        ROOT / "sdkconfig.defaults"
    ).read_text(encoding="utf-8"):
        print("FAIL: retained Task WDT attribution must keep the 5 second gate")
        return 1
    print(
        "PASS: Task WDT offender names, failed cores, and running tasks "
        "survive reset in RTC no-init memory"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
