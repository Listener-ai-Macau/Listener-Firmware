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
#define DIAG_SRC_OTA       0x0B
#define DIAG_SRC_POWER     0x0C
#define DIAG_SRC_BOARD     0x0D

/* System events (DIAG_SRC_SYSTEM) */
#define DIAG_SYS_BOOT          1   /* a1=boot_reason, a2=0, a3=0, a4=0 */
#define DIAG_SYS_INIT_RESULT   2   /* a1=component_id, a2=esp_err, a3=0, a4=0 */
#define DIAG_SYS_BOOT_SAFETY   3   /* a1=reset_reason, a2=crash_count, a3=safe_mode, a4=threshold */

#define DIAG_BOOT_POWER_ON     1
#define DIAG_BOOT_RESET        2
#define DIAG_BOOT_WATCHDOG     3
#define DIAG_BOOT_EXCEPTION    4
#define DIAG_BOOT_DEEP_SLEEP   5

#define DIAG_COMP_NVS          1
#define DIAG_COMP_SPIRAM       2
#define DIAG_COMP_BLE_CTRL     3
#define DIAG_COMP_AUDIO_CODEC  4
#define DIAG_COMP_DIAG_LOG     5
#define DIAG_COMP_KEYBOARD     6
#define DIAG_COMP_HEALTH       7
#define DIAG_COMP_OTA          8
#define DIAG_COMP_POWER_MANAGER 9

/* Keyboard events (DIAG_SRC_KEYBOARD) */
#define DIAG_KBD_KEY_PRESS     1   /* a1=key_usage_or_ascii, a2=hid_result(0=ok), a3=ble_connected, a4=press_count */
#define DIAG_KBD_KEY_FAIL      2   /* a1=key_usage_or_ascii, a2=esp_err, a3=ble_connected, a4=0 */
#define DIAG_KBD_GPIO_FAIL     3   /* a1=gpio_group(1=CUSTOM_KEY,2=EC11_AB,3=EC11_C), a2=esp_err, a3=0, a4=0 */
#define DIAG_KBD_EC11_DETENT   4   /* a1=direction(1=CW,2=CCW), a2=detent_count, a3=0, a4=0 */
#define DIAG_KBD_QUEUE_DROP    5   /* a1=key_usage_or_ascii, a2=queue_depth, a3=0, a4=0 */
#define DIAG_KBD_CUSTOM_KEY    6   /* a1=logical_key(1=KEY1..4=KEY4), a2=phase(1=press,2=release), a3=fallback_usage, a4=esp_err */

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
#define DIAG_GAP_CONN_PARAM_REQ 8  /* a1=mode(1=active,2=low_power), a2=result, a3=conn_handle, a4=latency */
#define DIAG_GAP_SUBSCRIBE     9   /* a1=conn_handle, a2=attr_handle, a3=(reason<<16|prevn<<8|curn), a4=(previ<<8|curi) */
#define DIAG_GAP_RECOVERY      10  /* a1=action(1=clear_bonds,2=terminate_conn,3=restart_adv,4=complete,5=rotate_identity,6=refresh_pairing_window,7=request_reconnect), a2=result, a3=bond_count_or_state_flags, a4=conn_handle */

/* Audio capture events (DIAG_SRC_AUDIO) */
#define DIAG_AUDIO_INIT_FAIL   1   /* a1=component(1=I2S,2=I2C,3=codec,4=mutex,5=task), a2=esp_err, a3=0, a4=0 */
#define DIAG_AUDIO_SESSION     2   /* a1=type(1=start,2=stop,3=cancel), a2=session_id, a3=duration_ms, a4=frame_count */
#define DIAG_AUDIO_SESSION_REJ 3   /* a1=reason(1=transport_not_ready,2=payload_unavailable), a2=0, a3=0, a4=0 */
#define DIAG_AUDIO_UNDERRUN    4   /* a1=underrun_count, a2=buffer_level, a3=0, a4=0 */
#define DIAG_AUDIO_DROP        5   /* a1=drop_count, a2=reason, a3=buffer_level, a4=session_ms */
#define DIAG_AUDIO_I2S_FAIL    6   /* a1=drop_count, a2=esp_err, a3=0, a4=0 */
#define DIAG_AUDIO_IDLE_POWER  7   /* a1=enabled, a2=esp_err, a3=0, a4=0 */
#define DIAG_AUDIO_BACKPRESSURE 8  /* a1=session_id, a2=state(1=pause,2=resume), a3=queue_depth, a4=pool_in_use */

/* BLE Audio Stream events (DIAG_SRC_BLE_AUDIO) */
#define DIAG_BAUD_STATE_CHANGE 1   /* a1=old_state, a2=new_state, a3=reason, a4=session_id */
#define DIAG_BAUD_POOL_EXHAUST 2   /* a1=session_id, a2=in_use, a3=pool_size, a4=high_water */
#define DIAG_BAUD_NOTIFY_FAIL  3   /* a1=session_id, a2=seq, a3=esp_err, a4=retries */
#define DIAG_BAUD_SESSION_ABORT 4  /* a1=session_id, a2=reason, a3=expected_packet_count, a4=epoch */
#define DIAG_BAUD_LINK_TIMEOUT 5   /* a1=session_id, a2=waited_ms, a3=packet_type, a4=seq_or_count */
#define DIAG_BAUD_NOTIFY_STATE 6   /* a1=session_id, a2=epoch, a3=conn_handle, a4=notify_state */
#define DIAG_BAUD_WATERMARK    7   /* a1=session_id, a2=queue_depth, a3=pool_in_use, a4=pressure_percent */
#define DIAG_BAUD_BACKPRESSURE 8   /* a1=session_id, a2=state(1=pause,2=resume), a3=queue_depth, a4=pool_in_use */

/* Voice recording events (DIAG_SRC_VOICE_REC) */
#define DIAG_VREC_SESSION      1   /* a1=type(1=start,2=stop,3=cancel), a2=source_code, a3=session_count, a4=0 */
#define DIAG_VREC_REJECTED     2   /* a1=source_code, a2=reject_reason, a3=current_state, a4=0 */
#define DIAG_VREC_FLOW         3   /* a1=stage(1=toggle_start,2=toggle_stop,3=start_ok,4=stop_requested,5=pending_start,6=pending_ready,7=pending_timeout,8=session_finished,9=session_aborted,10=toggle_ignored,11=cancel,12=recovery,13=start_rejected,14=stop_rejected), a2=source_code, a3=session_count, a4=current_state */

/* Voice key events (DIAG_SRC_VOICE_KEY) */
#define DIAG_VKEY_PRESS        1   /* a1=type(1=single_click_toggle,2=double_click_recovery,3=long_press_ignored), a2=detail_ms, a3=0, a4=0 */
#define DIAG_VKEY_QUEUE_DROP   2   /* a1=type(1=single_click_toggle,2=double_click_recovery), a2=reason(1=no_queue,2=queue_full), a3=0, a4=0 */
#define DIAG_VKEY_EXPANDER     3   /* a1=status(0=fail,1=degraded_fallback), a2=esp_err, a3=0, a4=0 */

/* Self-test events (DIAG_SRC_SELF_TEST) */
#define DIAG_ST_POST_RESULT    1   /* a1=nvs_ok, a2=spiram_ok, a3=heap_free_kb, a4=critical_ok */
#define DIAG_ST_COMP_RESULT    2   /* a1=component_id, a2=result(0=fail,1=pass), a3=error_code, a4=0 */

/* Health events (DIAG_SRC_HEALTH) */
#define DIAG_HEALTH_HEARTBEAT  1   /* a1=heap_free_kb, a2=heap_min_kb, a3=ble_connected, a4=uptime_min */
#define DIAG_HEALTH_ALERT      2   /* a1=alert_type(1=heap_free,2=ble_rate,3=heap_largest), a2=alert_value, a3=threshold, a4=heap_largest_kb */

/* Power manager events (DIAG_SRC_POWER) */
#define DIAG_POWER_STATE          1 /* a1=previous_state, a2=next_state, a3=idle_ms, a4=blockers */
#define DIAG_POWER_SLEEP_ENTRY    2 /* a1=idle_ms, a2=battery_mv, a3=battery_level, a4=reason */
#define DIAG_POWER_WAKE           3 /* a1=wake_source, a2=wake_gpio_mask_low, a3=last_sleep_reason, a4=last_idle_ms */
#define DIAG_POWER_SLEEP_BLOCKED  4 /* a1=blockers, a2=idle_ms, a3=reason, a4=esp_err */
#define DIAG_POWER_BATTERY_WARN   5 /* a1=battery_level, a2=battery_mv, a3=0, a4=0 */
#define DIAG_POWER_BLOCKER_CHANGE 6 /* a1=old_blockers, a2=new_blockers, a3=changed_mask, a4=enabled */
#define DIAG_POWER_STATUS         7 /* a1=state, a2=blockers, a3=idle_ms, a4=wake_gpio_mask_low */
#define DIAG_POWER_WAKE_POLICY    8 /* a1=policy, a2=wake_gpio_mask_low, a3=voice_key_capable, a4=voice_key_gpio */

/* Board events (DIAG_SRC_BOARD) */
#define DIAG_BOARD_PROFILE        1 /* a1=flash_mb, a2=psram_mb, a3=key1_gpio, a4=ec11_key_gpio */
#define DIAG_BOARD_PROVISIONAL    2 /* a1=usb_det_gpio, a2=pwr_hold_gpio, a3=3v3_current_gpio, a4=led_current_gpio */
#define DIAG_BOARD_POWER_RAIL     3 /* a1=rail(1=3v3,2=led_5v), a2=raw_adc, a3=adc_mv, a4=calibrated */
#define DIAG_BOARD_LED_RESOURCE   4 /* a1=group(1=status,2=key,3=edge), a2=data_gpio, a3=first_led, a4=led_count */

/* Firmware OTA events (DIAG_SRC_OTA) */
#define DIAG_OTA_STATE          1   /* a1=partition_subtype, a2=ota_state, a3=0, a4=0 */
#define DIAG_OTA_BEGIN          2   /* a1=partition_subtype, a2=image_size, a3=esp_err, a4=0 */
#define DIAG_OTA_WRITE          3   /* a1=partition_subtype, a2=offset_or_bytes, a3=esp_err, a4=0 */
#define DIAG_OTA_VERIFY         4   /* a1=partition_subtype, a2=bytes_written, a3=esp_err, a4=0 */
#define DIAG_OTA_SET_BOOT       5   /* a1=partition_subtype, a2=bytes_written, a3=esp_err, a4=0 */
#define DIAG_OTA_REBOOT         6   /* a1=partition_subtype, a2=0, a3=0, a4=0 */
#define DIAG_OTA_PENDING_VERIFY 7   /* a1=partition_subtype, a2=ota_state, a3=0, a4=0 */
#define DIAG_OTA_MARK_VALID     8   /* a1=partition_subtype, a2=0, a3=esp_err, a4=0 */
#define DIAG_OTA_ROLLBACK       9   /* a1=partition_subtype, a2=detail, a3=esp_err, a4=reason */
#define DIAG_OTA_ABORT          10  /* a1=partition_subtype, a2=bytes_written, a3=esp_err, a4=reason */
#define DIAG_OTA_REJECTED       11  /* a1=partition_subtype, a2=detail, a3=esp_err, a4=blocker */
#define DIAG_OTA_VERSION        12  /* a1=from_version_hash, a2=to_version_hash, a3=partition_subtype, a4=event */
#define DIAG_OTA_PARTITION      13  /* a1=role, a2=partition_subtype, a3=offset, a4=size_bytes */

#define DIAG_OTA_ABORT_USB                  1
#define DIAG_OTA_ROLLBACK_USB               1
#define DIAG_OTA_TEST_BOOT_INACTIVE         2
#define DIAG_OTA_ABORT_BLE_CONTROL          3
#define DIAG_OTA_ABORT_BLE_WRITE_FAIL       4
#define DIAG_OTA_ABORT_BLE_DISCONNECT       5
#define DIAG_OTA_ROLLBACK_POST_FAILED       0x01
#define DIAG_OTA_ROLLBACK_BLE_NOT_READY     0x02
#define DIAG_OTA_ROLLBACK_KEYBOARD_NOT_READY 0x04

#define DIAG_OTA_PARTITION_RUNNING          1
#define DIAG_OTA_PARTITION_BOOT             2
#define DIAG_OTA_PARTITION_UPDATE           3
#define DIAG_OTA_PARTITION_NEXT             4

#endif /* DIAG_LOG_EVENTS_H */
