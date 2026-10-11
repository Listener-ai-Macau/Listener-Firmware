#ifndef VOICE_RECORDING_ENDPOINT_POLICY_H
#define VOICE_RECORDING_ENDPOINT_POLICY_H

#include <stdbool.h>
#include "denzic_voice_activation_v1.h"

/* The host owns the endpoint of accepted visible dictation. The wake-window
 * duration guard cannot terminate that logical session while its owner is
 * still speaking or waiting for editor delivery. Transport loss, explicit
 * STOP/ABORT and the packet-sequence safety limit retain their own owners. */
static inline denzic_voice_activation_v1_config_t
voice_recording_endpoint_config(
    denzic_voice_activation_v1_config_t config,
    bool host_endpoint_owned)
{
    if (host_endpoint_owned) {
        config.max_session_ms = 0u;
    }
    return config;
}

#endif
