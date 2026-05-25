#ifndef DIAG_LOG_EVENTS_H
#define DIAG_LOG_EVENTS_H

/* Severity */
#define DIAG_SEV_INFO  0
#define DIAG_SEV_WARN  1
#define DIAG_SEV_ERROR 2

/* Source IDs */
#define DIAG_SRC_SYSTEM    0x01
#define DIAG_SRC_KEYBOARD  0x02
#define DIAG_SRC_BLE_HID   0x03
#define DIAG_SRC_BLE_GAP   0x04
#define DIAG_SRC_AUDIO     0x05
#define DIAG_SRC_VOICE_REC 0x06
#define DIAG_SRC_VOICE_KEY 0x07
#define DIAG_SRC_SELF_TEST 0x08
#define DIAG_SRC_HEALTH    0x09
#define DIAG_SRC_BLE_AUDIO 0x0A

/* System events (DIAG_SRC_SYSTEM) */
#define DIAG_SYS_BOOT          1   /* a1=boot_reason, a2=0, a3=0, a4=0 */
#define DIAG_SYS_INIT_RESULT   2   /* a1=component_id, a2=esp_err, a3=0, a4=0 */

#define DIAG_BOOT_POWER_ON     1
#define DIAG_BOOT_RESET        2
#define DIAG_BOOT_WATCHDOG     3
#define DIAG_BOOT_EXCEPTION    4

#define DIAG_COMP_NVS          1
#define DIAG_COMP_SPIRAM       2
#define DIAG_COMP_BLE_CTRL     3
#define DIAG_COMP_AUDIO_CODEC  4
#define DIAG_COMP_DIAG_LOG     5
#define DIAG_COMP_KEYBOARD     6
#define DIAG_COMP_HEALTH       7

/* Keyboard events (DIAG_SRC_KEYBOARD) */
#define DIAG_KBD_KEY_PRESS     1   /* a1=key_ascii, a2=hid_result(0=ok), a3=ble_connected, a4=press_count */
#define DIAG_KBD_KEY_FAIL      2   /* a1=key_ascii, a2=esp_err, a3=ble_connected, a4=0 */
#define DIAG_KBD_GPIO_FAIL     3   /* a1=gpio_group(1=WASD,2=EC11_AB,3=EC11_C), a2=esp_err, a3=0, a4=0 */
#define DIAG_KBD_EC11_DETENT   4   /* a1=direction(1=CW,2=CCW), a2=detent_count, a3=0, a4=0 */
#define DIAG_KBD_QUEUE_DROP    5   /* a1=key_ascii, a2=queue_depth, a3=0, a4=0 */

/* BLE HID events (DIAG_SRC_BLE_HID) */
#define DIAG_BLE_CONNECT       1   /* a1=connected, a2=heap_kb, a3=disconnect_count, a4=0 */
#define DIAG_BLE_DISCONNECT    2   /* a1=reason, a2=disconnect_count, a3=conn_duration_ms, a4=pre_state(heap/1024) */
#define DIAG_BLE_HID_SEND_FAIL 3   /* a1=key_ascii, a2=esp_err, a3=ble_connected, a4=0 */
#define DIAG_BLE_BATTERY_WARN  4   /* a1=level, a2=voltage_mv, a3=0, a4=0 */

/* BLE GAP events (DIAG_SRC_BLE_GAP) */
#define DIAG_GAP_MTU           1   /* a1=requested/0, a2=actual, a3=conn_handle, a4=channel_id */
#define DIAG_GAP_ENCRYPT       2   /* a1=status(0=fail,1=success), a2=esp_status, a3=conn_handle, a4=0 */
#define DIAG_GAP_BOND          3   /* a1=result(0=fail,1=success), a2=reason, a3=conn_handle, a4=0 */
#define DIAG_GAP_ADV_START     4   /* a1=result(0=fail,1=success), a2=mode_or_error, a3=context, a4=0 */
#define DIAG_GAP_ADV_STOP      5   /* a1=result(0=fail,1=success), a2=0, a3=0, a4=0 */
#define DIAG_GAP_CTRL_INIT     6   /* a1=stage, a2=esp_err, a3=mode, a4=0 */
#define DIAG_GAP_CONN_PARAM    7   /* a1=conn_interval, a2=conn_latency, a3=conn_timeout, a4=conn_handle */

/* Audio capture events (DIAG_SRC_AUDIO) */
#define DIAG_AUDIO_INIT_FAIL   1   /* a1=component(1=I2S,2=I2C,3=codec,4=mutex,5=task), a2=esp_err, a3=0, a4=0 */
#define DIAG_AUDIO_SESSION     2   /* a1=type(1=start,2=stop,3=cancel), a2=session_id, a3=duration_ms, a4=frame_count */
#define DIAG_AUDIO_SESSION_REJ 3   /* a1=reason(1=transport_not_ready,2=payload_unavailable), a2=0, a3=0, a4=0 */
#define DIAG_AUDIO_UNDERRUN    4   /* a1=underrun_count, a2=buffer_level, a3=0, a4=0 */
#define DIAG_AUDIO_DROP        5   /* a1=drop_count, a2=reason, a3=buffer_level, a4=session_ms */
#define DIAG_AUDIO_I2S_FAIL    6   /* a1=drop_count, a2=esp_err, a3=0, a4=0 */

/* BLE Audio Stream events (DIAG_SRC_BLE_AUDIO) */
#define DIAG_BAUD_STATE_CHANGE 1   /* a1=old_state, a2=new_state, a3=reason, a4=session_id */
#define DIAG_BAUD_POOL_EXHAUST 2   /* a1=session_id, a2=in_use, a3=pool_size, a4=high_water */
#define DIAG_BAUD_NOTIFY_FAIL  3   /* a1=session_id, a2=seq, a3=esp_err, a4=retries */
#define DIAG_BAUD_SESSION_ABORT 4  /* a1=session_id, a2=reason, a3=expected_packet_count, a4=0 */
#define DIAG_BAUD_LINK_TIMEOUT 5   /* a1=session_id, a2=waited_ms, a3=packet_type, a4=seq_or_count */

/* Voice recording events (DIAG_SRC_VOICE_REC) */
#define DIAG_VREC_SESSION      1   /* a1=type(1=start,2=stop,3=cancel), a2=source_code, a3=session_count, a4=0 */
#define DIAG_VREC_REJECTED     2   /* a1=source_code, a2=reject_reason, a3=current_state, a4=0 */

/* Voice key events (DIAG_SRC_VOICE_KEY) */
#define DIAG_VKEY_PRESS        1   /* a1=type(1=short,2=recovery_hold), a2=0, a3=0, a4=0 */
#define DIAG_VKEY_QUEUE_DROP   2   /* a1=type(1=short,2=recovery_hold), a2=reason(1=no_queue,2=queue_full), a3=0, a4=0 */
#define DIAG_VKEY_EXPANDER     3   /* a1=status(0=fail,1=degraded_fallback), a2=esp_err, a3=0, a4=0 */

/* Self-test events (DIAG_SRC_SELF_TEST) */
#define DIAG_ST_POST_RESULT    1   /* a1=nvs_ok, a2=spiram_ok, a3=heap_free_kb, a4=critical_ok */
#define DIAG_ST_COMP_RESULT    2   /* a1=component_id, a2=result(0=fail,1=pass), a3=error_code, a4=0 */

/* Health events (DIAG_SRC_HEALTH) */
#define DIAG_HEALTH_HEARTBEAT  1   /* a1=heap_free_kb, a2=heap_min_kb, a3=ble_connected, a4=uptime_min */
#define DIAG_HEALTH_ALERT      2   /* a1=alert_type(1=heap,2=ble_rate), a2=alert_value, a3=threshold, a4=0 */

#endif /* DIAG_LOG_EVENTS_H */
