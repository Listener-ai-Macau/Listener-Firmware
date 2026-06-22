# Status LED human effect review summary

- Result: ABORTED_BY_OPERATOR
- Mode: IdleTransition
- Port: COM10
- Pass/Fail/Skip: 0 / 1 / 0
- LED5/6 follow reports: 0
- Irregular flicker reports: 0
- Brightness problem reports: 0
- Plan: C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware-wt-adhoc-oai2-usb-uart-reset-disable\docs\validation\oai2-status-led-idle-transition-rootfix-20260622-082649\human-effect-plan.md
- Session JSONL: C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware-wt-adhoc-oai2-usb-uart-reset-disable\docs\validation\oai2-status-led-idle-transition-rootfix-20260622-082649\human-effect-session.jsonl
- Serial log: C:\Users\Billy\Desktop\Denzic\Listener\Listener-Firmware-wt-adhoc-oai2-usb-uart-reset-disable\docs\validation\oai2-status-led-idle-transition-rootfix-20260622-082649\human-effect-serial.log

## Human Observations

| # | id | application timing | semantic lights | result | brightness | LED5/6 follow | flicker | unexpected | observed | notes |
|---:|---|---|---|---|---|---|---|---|---|---|
| 1 | idle-transition-status-active-to-connected | 隔离检查从工作状态进入 connected idle；前置状态只允许状态灯 LED3/REC 和 LED4/AI 参与，不让旋钮、按键、边框作为干扰源。 | 按本步骤应用时机判断语义归属；未在预期中点名的灯都不应乱入。 | FAIL | 无法判断 | False | False |  |  | 现在led3到led6会规则性的全闪, 找到根因修好先再测 |
