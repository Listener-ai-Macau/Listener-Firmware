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
#define DIAG_SRC_STATUS_LED 0x0E

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
#define DIAG_KBD_CUSTOM_KEY    6   /* a1=logical_key(1=KEY1..4=KEY4,5=EC11), a2=phase(1=press,2=release,3=single,4=double,5=long), a3=hid_usage, a4=esp_err */
#define DIAG_KBD_INPUT_DEBUG   7   /* a1=kind(1=key_raw,2=key_stable,3=ec11_transition,4=ec11_invalid,5=ec11_partial,6=ec11_dispatch), a2=key_or_state_or_direction, a3=level_or_delta_or_action, a4=detail_or_usage */

/* BLE HID events (DIAG_SRC_BLE_HID) */
#define DIAG_BLE_CONNECT       1   /* a1=connected, a2=heap_kb, a3=disconnect_count, a4=0 */
#define DIAG_BLE_DISCONNECT    2   /* a1=reason, a2=disconnect_count, a3=conn_duration_ms, a4=pre_state(heap/1024) */
#define DIAG_BLE_HID_SEND_FAIL 3   /* a1=key_ascii, a2=esp_err, a3=ble_connected, a4=0 */
#define DIAG_BLE_BATTERY_WARN  4   /* a1=level, a2=voltage_mv, a3=raw_adc, a4=adc_mv */
#define DIAG_BLE_BATTERY_LEVEL 5   /* a1=level, a2=voltage_mv, a3=raw_adc, a4=adc_mv */

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
#define DIAG_GAP_RECOVERY      10  /* a1=action(1=clear_bonds,2=terminate_conn,3=restart_adv,4=complete,5=rotate_identity,6=refresh_pairing_window,7=request_reconnect,8=keep_adv_connectable,9=bond_lookup_after_disconnect,10=request_security,11=type_audio_ready,12=windows_connecting,13=pre_reset_secure_ignored,14=pre_reset_disconnect,15=async_local_bond_delete,16=defer_adv_for_bond_delete,17=reject_conn_for_bond_delete,18=apply_ble_name,19=reject_unbonded_type_ready,20=retry_terminate_failed,21=stale_connection_reset,22=transaction_begin), a2=result_or_execute, a3=bond_count_or_state_flags_or_type_controlled, a4=conn_handle_or_operation_id */
#define DIAG_GAP_ADV_STATE     11  /* a1=action(1=suppress_shutdown,2=suppress_key_wake,3=defer_hid_start,4=defer_stack_sync,5=skip_connected,6=already_active,7=low_power_set,8=key_wake_stop,9=shutdown_prepare,10=reconnect_request,11=connect_fail_suppress,12=disconnect_suppress,13=adv_complete_suppress,14=stop_for_restart,15=stop_for_shutdown,16=stop_for_reconnect,17=start_directed,18=start_undirected,19=defer_bond_delete), a2=result_or_detail, a3=state_flags(1=adv_active,2=low_power,4=directed_pending,8=key_wake_only,16=shutdown_quiesce,32=nimble_ready,64=hid_started,128=connected,256=bond_delete_pending), a4=conn_handle */
#define DIAG_GAP_PHY           12  /* a1=status, a2=tx_phy, a3=rx_phy, a4=conn_handle */

/* Audio capture events (DIAG_SRC_AUDIO) */
#define DIAG_AUDIO_INIT_FAIL   1   /* a1=component(1=I2S/PDM,2=I2C,3=codec,4=mutex,5=task), a2=esp_err, a3=0, a4=0 */
#define DIAG_AUDIO_SESSION     2   /* a1=type(1=start,2=stop,3=cancel), a2=session_id, a3=duration_ms, a4=frame_count */
#define DIAG_AUDIO_SESSION_REJ 3   /* a1=reason(1=transport_not_ready,2=payload_unavailable,3=capture_unavailable,4=capture_not_started), a2=esp_err, a3=0, a4=0 */
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
#define DIAG_BAUD_REPLAY       9   /* a1=session_id, a2=event(1=armed,2=resend,3=skip_current,5=fail), a3=count_or_seq, a4=window_or_err */

/* Voice recording events (DIAG_SRC_VOICE_REC) */
#define DIAG_VREC_SESSION      1   /* a1=type(1=start,2=stop,3=cancel), a2=source_code, a3=session_count, a4=0 */
#define DIAG_VREC_REJECTED     2   /* a1=source_code, a2=reject_reason, a3=current_state, a4=0 */
#define DIAG_VREC_FLOW         3   /* a1=stage(1=toggle_start,2=toggle_stop,3=start_ok,4=stop_requested,5=pending_start,6=pending_ready,7=pending_timeout,8=session_finished,9=session_aborted,10=toggle_ignored,11=cancel,12=recovery,13=start_rejected,14=stop_rejected), a2=source_code, a3=session_count, a4=current_state */
#define DIAG_VREC_TIMING       4   /* a1=press_to_control_ms, a2=source_code, a3=active_session_id_or_0, a4=current_state; emitted only after recording-control dispatch */

/* Voice key events (DIAG_SRC_VOICE_KEY) */
#define DIAG_VKEY_PRESS        1   /* a1=type(1=single_click_toggle,2=double_click_recovery,3=long_press_ignored), a2=detail_ms, a3=0, a4=0 */
#define DIAG_VKEY_QUEUE_DROP   2   /* a1=type(1=single_click_toggle,2=double_click_recovery), a2=reason(1=no_queue,2=queue_full), a3=0, a4=0 */
#define DIAG_VKEY_EXPANDER     3   /* a1=status(0=fail,1=degraded_fallback), a2=esp_err, a3=0, a4=0 */
#define DIAG_VKEY_INPUT_DEBUG  4   /* a1=kind(1=raw,2=stable), a2=source(1=direct_gpio,2=legacy_io0_4,3=legacy_io0_5), a3=raw_high, a4=pressed_or_stable_high */

/* Self-test events (DIAG_SRC_SELF_TEST) */
#define DIAG_ST_POST_RESULT    1   /* a1=nvs_ok, a2=spiram_ok, a3=heap_free_kb, a4=critical_ok */
#define DIAG_ST_COMP_RESULT    2   /* a1=component_id, a2=result(0=fail,1=pass), a3=error_code, a4=0 */

/* Health events (DIAG_SRC_HEALTH) */
#define DIAG_HEALTH_HEARTBEAT  1   /* a1=heap_free_kb, a2=heap_min_kb, a3=ble_connected, a4=uptime_min */
#define DIAG_HEALTH_ALERT      2   /* a1=alert_type(1=heap_free,2=ble_rate,3=heap_largest), a2=alert_value, a3=threshold, a4=heap_largest_kb */

/* Power manager events (DIAG_SRC_POWER) */
#define DIAG_POWER_STATE          1 /* a1=previous_state, a2=next_state, a3=idle_ms, a4=blockers */
#define DIAG_POWER_SLEEP_ENTRY    2 /* a1=idle_ms, a2=battery_mv, a3=battery_level, a4=shutdown_reason */
#define DIAG_POWER_WAKE           3 /* a1=reset_reason, a2=pwr_hold_gpio, a3=last_shutdown_reason, a4=last_shutdown_idle_ms */
#define DIAG_POWER_SLEEP_BLOCKED  4 /* a1=blockers, a2=idle_ms, a3=shutdown_reason, a4=esp_err_or_detail */
#define DIAG_POWER_BATTERY_WARN   5 /* a1=battery_level, a2=battery_mv, a3=0, a4=0 */
#define DIAG_POWER_BLOCKER_CHANGE 6 /* a1=old_blockers, a2=new_blockers, a3=changed_mask, a4=enabled */
#define DIAG_POWER_STATUS         7 /* a1=state, a2=blockers, a3=idle_ms, a4=pwr_hold_gpio */
#define DIAG_POWER_WAKE_POLICY    8 /* legacy reserved: wake policy is no longer used after hardware shutdown migration */
#define DIAG_POWER_EXTERNAL_POWER 9 /* a1=flags, a2=raw_levels, a3=idle_ms, a4=shutdown_blockers */
#define DIAG_POWER_USB_DETECT     10 /* a1=level, a2=usb_power_present, a3=idle_ms, a4=raw_levels */
#define DIAG_POWER_CHARGE_STATE   11 /* a1=charging, a2=charge_full, a3=idle_ms, a4=raw_levels */
#define DIAG_POWER_HOLD_STATE     12 /* a1=configured, a2=gpio, a3=level(0=low,1=high,2=unknown), a4=action(0=source_snapshot,1=runtime_guard,2=shutdown_entry,3=shutdown_drive_high,4=shutdown_failed_restore,5=init,6=shutdown_failure_backoff) */

/* Board events (DIAG_SRC_BOARD) */
#define DIAG_BOARD_PROFILE        1 /* a1=flash_mb, a2=psram_mb, a3=key1_gpio, a4=ec11_key_gpio */
#define DIAG_BOARD_PROVISIONAL    2 /* a1=usb_det_gpio, a2=pwr_hold_gpio, a3=3v3_current_gpio, a4=led_current_gpio */
#define DIAG_BOARD_POWER_RAIL     3 /* a1=rail(1=3v3,2=led_5v), a2=raw_adc, a3=adc_mv, a4=calibrated */
#define DIAG_BOARD_LED_RESOURCE   4 /* a1=group(1=status,2=ec11,3=key,4=edge), a2=data_gpio, a3=first_led, a4=led_count */

/* Status LED events (DIAG_SRC_STATUS_LED) */
#define DIAG_LED_STATE       1 /* a1=state_type, a2=value, a3=detail, a4=detail */
#define DIAG_LED_ERROR       2 /* a1=domain, a2=severity, a3=0, a4=0 */
#define DIAG_LED_OUTPUT_FAIL 3 /* a1=gpio, a2=esp_err, a3=stage, a4=0 */
#define DIAG_LED_PROFILE     4 /* a1=profile, a2=0, a3=0, a4=0 */
#define DIAG_LED_POWER_INPUT 5 /* a1=power_flags(1=external_power,2=charging,4=full,8=raw_charging,16=raw_full,32=full_latched,64=battery_valid,128=low_power_disabled,256=output_disabled,bit24=display_valid,bit25=display_rise_suppressed,bit26=external_from_usb_det,bit27=external_from_charger_status,bits16_23=display_level), a2=battery_mv, a3=battery_level(255=unknown), a4=reason(1=boot,2=power_change,3=ble_state,4=recording_start,5=recording_stop,6=processing_start,7=processing_stop,8=success,9=error,10=low_power_off,11=low_power_resume,12=prepare_sleep,13=manual_off,14=device_settings,15=render,16=preview,17=brightness,18=profile,19=test,20=key,21=booting,255=other) */
#define DIAG_LED_VISUAL_STATE 6 /* a1=active_flags(1=PWR,2=BLE,4=REC,8=AI,16=OK,32=WARN,64=KEY,128=EDGE,256=EC11), a2=pwr_rgb_0xRRGGBB, a3=visual_flags(1=external_power,2=charging,4=full,8=battery_valid,16=output_disabled,32=low_power_disabled,64=status_window,128=boot_feedback,pwr_class_bits8_11(0=off,1=green,2=amber,3=red,4=white,5=blue,6=violet,7=gold,15=other),ble_state_bits12_15,error_domain_bits16_19), a4=reason(1=boot,2=power_change,3=ble_state,4=recording_start,5=recording_stop,6=processing_start,7=processing_stop,8=success,9=error,10=low_power_off,11=low_power_resume,12=prepare_sleep,13=manual_off,14=device_settings,15=render,16=preview,17=brightness,18=profile,19=test,20=key,21=booting,255=other) */
#define DIAG_LED_OUTPUT_STATE 7 /* a1=output_disabled, a2=low_power_disabled, a3=active_flags(1=PWR,2=BLE,4=REC,8=AI,16=OK,32=WARN,64=KEY,128=EDGE,256=EC11), a4=reason(1=boot,2=power_change,3=ble_state,4=recording_start,5=recording_stop,6=processing_start,7=processing_stop,8=success,9=error,10=low_power_off,11=low_power_resume,12=prepare_sleep,13=manual_off,14=device_settings,15=render,16=preview,17=brightness,18=profile,19=test,20=key,21=booting,255=other) */
#define DIAG_LED_FRAME_RGB    8 /* a1=status_color_classes_nibbles(PWR,BLE,REC,AI,OK,WARN;0=off,1=green,2=amber,3=red,4=white,5=blue,6=violet,7=gold,15=other), a2=status_intensity_bytes(PWR,BLE,REC,AI), a3=status_intensity_bytes(OK,WARN,EC11,EDGE), a4=reason(1=boot,2=power_change,3=ble_state,4=recording_start,5=recording_stop,6=processing_start,7=processing_stop,8=success,9=error,10=low_power_off,11=low_power_resume,12=prepare_sleep,13=manual_off,14=device_settings,15=render,16=preview,17=brightness,18=profile,19=test,20=key,21=booting,255=other) */

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
#define DIAG_OTA_ABORT_IDLE_TIMEOUT          6
#define DIAG_OTA_ROLLBACK_POST_FAILED       0x01
#define DIAG_OTA_ROLLBACK_BLE_NOT_READY     0x02
#define DIAG_OTA_ROLLBACK_KEYBOARD_NOT_READY 0x04

#define DIAG_OTA_PARTITION_RUNNING          1
#define DIAG_OTA_PARTITION_BOOT             2
#define DIAG_OTA_PARTITION_UPDATE           3
#define DIAG_OTA_PARTITION_NEXT             4

#endif /* DIAG_LOG_EVENTS_H */
