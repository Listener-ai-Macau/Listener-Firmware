/* Host-native regression against the production endpoint policy and VA FSM. */
#include <assert.h>
#include <stdio.h>
#include "voice_recording_endpoint_policy.h"

static denzic_voice_activation_v1_decision_t tick(
    denzic_voice_activation_v1_machine_t *machine,
    denzic_voice_activation_v1_config_t config,
    bool host_owned,
    bool auto_stop)
{
    config = voice_recording_endpoint_config(config, host_owned);
    const denzic_voice_activation_v1_input_t input = {
        .elapsed_ms = 20u,
        .enabled = true,
        .auto_start_enabled = true,
        .auto_stop_enabled = auto_stop,
        .recording_active = true,
        /* Production feeds this for a host-owned visible session, even while
         * it is waiting for provider words or an editor receipt. */
        .speech_detected = true,
        .start_blocked = true,
    };
    return denzic_voice_activation_v1_step(machine, &config, &input);
}

int main(void)
{
    const denzic_voice_activation_v1_config_t base =
        denzic_voice_activation_v1_default_config();
    assert(base.max_session_ms == 60000u);
    denzic_voice_activation_v1_machine_t machine = {0};
    for (unsigned i = 0; i < 6000; ++i) {
        assert(tick(&machine, base, true, true).action ==
               DENZIC_VOICE_ACTIVATION_V1_ACTION_NONE);
    }
    assert(machine.recording_ms == 120000u);

    /* Acquiring the host endpoint does not restart a duration timer. */
    denzic_voice_activation_v1_reset(&machine);
    for (unsigned i = 0; i < 2750; ++i) {
        assert(tick(&machine, base, false, true).action ==
               DENZIC_VOICE_ACTIVATION_V1_ACTION_NONE);
    }
    for (unsigned i = 0; i < 750; ++i) {
        assert(tick(&machine, base, true, true).action ==
               DENZIC_VOICE_ACTIVATION_V1_ACTION_NONE);
    }
    assert(machine.recording_ms == 70000u);
    assert(tick(&machine, base, false, true).stop_reason ==
           DENZIC_VOICE_ACTIVATION_V1_STOP_REASON_MAX_DURATION);

    /* Unowned/manual-empty sessions keep the original fallback limit. */
    denzic_voice_activation_v1_reset(&machine);
    for (unsigned i = 1; i < 3000; ++i) {
        assert(tick(&machine, base, false, true).action ==
               DENZIC_VOICE_ACTIVATION_V1_ACTION_NONE);
    }
    assert(tick(&machine, base, false, true).stop_reason ==
           DENZIC_VOICE_ACTIVATION_V1_STOP_REASON_MAX_DURATION);

    /* Hidden candidates keep their four-second wake rotation. */
    denzic_voice_activation_v1_config_t hidden = base;
    hidden.max_session_ms = hidden.min_session_ms = 4000u;
    denzic_voice_activation_v1_reset(&machine);
    for (unsigned i = 1; i < 200; ++i) {
        assert(tick(&machine, hidden, false, true).action ==
               DENZIC_VOICE_ACTIVATION_V1_ACTION_NONE);
    }
    assert(tick(&machine, hidden, false, true).stop_reason ==
           DENZIC_VOICE_ACTIVATION_V1_STOP_REASON_MAX_DURATION);

    /* Enrollment's existing auto-stop-off policy is unchanged. */
    denzic_voice_activation_v1_reset(&machine);
    for (unsigned i = 0; i < 6000; ++i) {
        assert(tick(&machine, base, false, false).action ==
               DENZIC_VOICE_ACTIVATION_V1_ACTION_NONE);
    }
    assert(base.max_session_ms == 60000u);
    puts("PASS: host-owned 120s, ownership transfer, unowned 60s, hidden 4s, enrollment");
    return 0;
}
