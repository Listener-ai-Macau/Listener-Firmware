#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* These probes are intentionally kept in the watchdog platform port. They
 * sample the voice control task's external stack without changing the audio
 * or transport contract under test. */
enum {
    VOICE_STACK_DIAG_STAGE_CONTROL_ENTER = 1u,
    VOICE_STACK_DIAG_STAGE_SESSION_BEGIN_BEFORE = 2u,
    VOICE_STACK_DIAG_STAGE_WARMUP_READY = 3u,
    VOICE_STACK_DIAG_STAGE_HANDOFF_BEFORE = 4u,
    VOICE_STACK_DIAG_STAGE_HANDOFF_ENTER = 5u,
    VOICE_STACK_DIAG_STAGE_HANDOFF_SEND = 6u,
    VOICE_STACK_DIAG_STAGE_HANDOFF_AFTER = 7u,
    VOICE_STACK_DIAG_STAGE_SESSION_BEGIN_AFTER = 8u,
};

void voice_stack_diag_boot_report(void);
void voice_stack_diag_register_stack(void *base, uint32_t bytes);
void voice_stack_diag_bind_current_task(void);

bool voice_stack_diag_arm(uint32_t run_id);
void voice_stack_diag_dump(void);

bool voice_stack_diag_enter(uint32_t source_tag);
void voice_stack_diag_mark(uint32_t stage, uint32_t session_id,
                           uint32_t arg0, uint32_t arg1);
void voice_stack_diag_leave(int32_t result);
void voice_stack_diag_set_session_active(bool active);

#ifdef __cplusplus
}
#endif
