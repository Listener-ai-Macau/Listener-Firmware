# Status LED human effect review summary

- Result: HUMAN_REVIEW_PASS
- Mode: StaticRoot
- Port: COM10
- Pass/Fail/Skip: 3 / 0 / 0
- LED5/6 follow reports: 0
- Irregular flicker reports: 0
- Brightness problem reports: 0
- Plan: C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware-wt-adhoc-oai1-led-flicker-guard-shutdown-confirm\docs\validation\voice-keyboard-firmware-full-function-test-1.9\human-led-diagnostic-staticroot-current-20260619-rerun20\human-effect-plan.md
- Session JSONL: C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware-wt-adhoc-oai1-led-flicker-guard-shutdown-confirm\docs\validation\voice-keyboard-firmware-full-function-test-1.9\human-led-diagnostic-staticroot-current-20260619-rerun20\human-effect-session.jsonl
- Serial log: C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware-wt-adhoc-oai1-led-flicker-guard-shutdown-confirm\docs\validation\voice-keyboard-firmware-full-function-test-1.9\human-led-diagnostic-staticroot-current-20260619-rerun20\human-effect-serial.log

## Human Observations

| # | id | application timing | semantic lights | result | brightness | LED5/6 follow | flicker | unexpected | observed | notes |
|---:|---|---|---|---|---|---|---|---|---|---|
| 1 | static-status-clean-50 |  | 基础/诊断步骤：按本步骤预期判断，不代表产品常态语义。 | PASS | 无法判断 | False | False |  |  | 变成静态灯了? 没闪了, 边框灯旋钮灯没有了嘛? |
| 2 | static-status-query-50 |  | 基础/诊断步骤：按本步骤预期判断，不代表产品常态语义。 | PASS | 无法判断 | False | False |  |  | 没有闪烁, 静态灯是不会闪的 |
| 3 | static-root-restore-brightness-50 |  | 基础/诊断步骤：按本步骤预期判断，不代表产品常态语义。 | PASS | 无法判断 | False | False |  |  |  |
