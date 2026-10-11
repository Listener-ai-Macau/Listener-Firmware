#include "voice_stack_diag.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define VOICE_STACK_DIAG_MAGIC UINT32_C(0x56534431)
#define VOICE_STACK_DIAG_SCHEMA_VERSION UINT32_C(1)
#define VOICE_STACK_DIAG_RECORD_CAPACITY 16u
#define VOICE_STACK_DIAG_TASK_ID UINT32_C(0x56524354)
#define VOICE_STACK_DIAG_INVALID UINT32_MAX
#define VOICE_STACK_DIAG_ELF_SHA256_BYTES 32u
#define VOICE_STACK_DIAG_HEADER_COMMIT UINT32_C(0x56484431)
#define VOICE_STACK_DIAG_STATUS_COMMIT UINT32_C(0x56535431)

static const char *TAG = "voice_diag";

typedef struct {
    uint32_t sequence;
    uint32_t stage;
    uint32_t task_identity;
    uint32_t stack_pointer;
    uint32_t session_id;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t stack_used_bytes;
    uint32_t stack_sample_valid;
    uint32_t entry_depth;
    uint32_t checksum;
    uint32_t commit_sequence;
} voice_stack_diag_record_t;

typedef struct {
    uint32_t magic;
    uint32_t schema_version;
    uint32_t struct_bytes;
    uint32_t run_id;
    uint32_t stack_low;
    uint32_t stack_high;
    uint32_t stack_bytes;
    uint8_t firmware_elf_sha256[VOICE_STACK_DIAG_ELF_SHA256_BYTES];
    uint32_t checksum;
    uint32_t header_commit;
    uint32_t armed;
    uint32_t finished;
    uint32_t overflow;
    uint32_t record_count;
    uint32_t status_checksum;
    uint32_t status_commit;
    voice_stack_diag_record_t records[VOICE_STACK_DIAG_RECORD_CAPACITY];
} voice_stack_diag_state_t;

_Static_assert(
    VOICE_STACK_DIAG_RECORD_CAPACITY > 0u,
    "voice diagnostic record capacity must be nonzero");
_Static_assert(
    sizeof(voice_stack_diag_state_t) <= 4096u,
    "voice diagnostic RTC record must remain bounded");

static RTC_NOINIT_ATTR voice_stack_diag_state_t s_state;
static TaskHandle_t s_bound_task;
static uint32_t s_stack_low;
static uint32_t s_stack_high;
static uint32_t s_stack_bytes;
static uint32_t s_record_index;
static int8_t s_stage_slots[32];
static volatile bool s_armed;
static volatile bool s_active;
static volatile bool s_session_active;
static uint32_t s_depth;

static uint32_t voice_stack_diag_hash_bytes(
    const uint8_t *bytes,
    size_t length)
{
    uint32_t hash = UINT32_C(2166136261);
    for (size_t index = 0u; index < length; ++index) {
        hash ^= bytes[index];
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static uint32_t voice_stack_diag_header_checksum(
    const voice_stack_diag_state_t *state)
{
    const uint8_t *start = (const uint8_t *)&state->schema_version;
    const uint8_t *end = (const uint8_t *)&state->checksum;
    return voice_stack_diag_hash_bytes(start, (size_t)(end - start));
}

static uint32_t voice_stack_diag_record_checksum(
    const voice_stack_diag_record_t *record)
{
    const uint8_t *start = (const uint8_t *)&record->sequence;
    const uint8_t *end = (const uint8_t *)&record->checksum;
    return voice_stack_diag_hash_bytes(start, (size_t)(end - start));
}

static bool voice_stack_diag_header_valid(void)
{
    return s_state.magic == VOICE_STACK_DIAG_MAGIC &&
           s_state.schema_version == VOICE_STACK_DIAG_SCHEMA_VERSION &&
           s_state.struct_bytes == sizeof(s_state) &&
           s_state.header_commit == VOICE_STACK_DIAG_HEADER_COMMIT &&
           s_state.checksum == voice_stack_diag_header_checksum(&s_state);
}

static uint32_t voice_stack_diag_status_checksum(
    const voice_stack_diag_state_t *state)
{
    const uint8_t *start = (const uint8_t *)&state->armed;
    const uint8_t *end = (const uint8_t *)&state->status_checksum;
    return voice_stack_diag_hash_bytes(start, (size_t)(end - start));
}

static bool voice_stack_diag_status_valid(void)
{
    return s_state.status_commit == VOICE_STACK_DIAG_STATUS_COMMIT &&
           s_state.status_checksum == voice_stack_diag_status_checksum(&s_state);
}

static bool voice_stack_diag_record_valid(
    const voice_stack_diag_record_t *record)
{
    return record != NULL && record->sequence != 0u &&
           record->sequence <= VOICE_STACK_DIAG_RECORD_CAPACITY &&
           record->commit_sequence == record->sequence &&
           record->checksum == voice_stack_diag_record_checksum(record);
}

static bool voice_stack_diag_stack_sample_valid(uint32_t stack_pointer)
{
    return s_stack_bytes != 0u && s_stack_high > s_stack_low &&
           s_stack_high - s_stack_low == s_stack_bytes &&
           stack_pointer >= s_stack_low && stack_pointer <= s_stack_high;
}

static void voice_stack_diag_refresh_header_checksum(void)
{
    s_state.checksum = voice_stack_diag_header_checksum(&s_state);
}

static void voice_stack_diag_commit_status(
    uint32_t armed,
    uint32_t finished,
    uint32_t overflow,
    uint32_t record_count)
{
    s_state.status_commit = 0u;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    s_state.armed = armed;
    s_state.finished = finished;
    s_state.overflow = overflow;
    s_state.record_count = record_count;
    s_state.status_checksum = voice_stack_diag_status_checksum(&s_state);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    s_state.status_commit = VOICE_STACK_DIAG_STATUS_COMMIT;
}

static void voice_stack_diag_sha256_hex(
    const uint8_t *digest,
    char *text,
    size_t text_bytes)
{
    static const char hex[] = "0123456789abcdef";
    if (text == NULL || text_bytes == 0u) {
        return;
    }
    size_t digest_bytes = (text_bytes - 1u) / 2u;
    if (digest_bytes > VOICE_STACK_DIAG_ELF_SHA256_BYTES) {
        digest_bytes = VOICE_STACK_DIAG_ELF_SHA256_BYTES;
    }
    for (size_t index = 0u; index < digest_bytes; ++index) {
        text[index * 2u] = hex[digest[index] >> 4u];
        text[index * 2u + 1u] = hex[digest[index] & 0x0fu];
    }
    text[digest_bytes * 2u] = '\0';
}

void voice_stack_diag_boot_report(void)
{
    /* A new boot must begin disarmed. A retained armed-but-unfinished record
     * is exactly the evidence needed after a panic; it must not be consumed or
     * cleared here. */
    s_armed = false;
    s_active = false;
    s_session_active = false;
    s_depth = 0u;
    s_record_index = 0u;
    memset(s_stage_slots, 0xff, sizeof(s_stage_slots));

    if (!voice_stack_diag_header_valid()) {
        ESP_LOGI(TAG, "VOICE_DIAG retained: none_or_invalid");
        return;
    }

    char retained_elf_sha256[VOICE_STACK_DIAG_ELF_SHA256_BYTES * 2u + 1u];
    voice_stack_diag_sha256_hex(
        s_state.firmware_elf_sha256,
        retained_elf_sha256,
        sizeof(retained_elf_sha256));
    const esp_app_desc_t *current_description = esp_app_get_description();
    bool foreign_build = current_description == NULL ||
        memcmp(
            s_state.firmware_elf_sha256,
            current_description->app_elf_sha256,
            VOICE_STACK_DIAG_ELF_SHA256_BYTES) != 0;

    ESP_LOGI(
        TAG,
        "VOICE_DIAG retained: run_id=%" PRIu32 " armed=%" PRIu32
        " finished=%" PRIu32 " overflow=%" PRIu32 " records=%" PRIu32
        " status=%s identity=%s retained_elf_sha256=%s"
        " stack=[0x%08" PRIx32 ",0x%08" PRIx32 "] bytes=%" PRIu32,
        s_state.run_id,
        s_state.armed,
        s_state.finished,
        s_state.overflow,
        s_state.record_count,
        voice_stack_diag_status_valid() ? "valid" : "incomplete",
        foreign_build ? "foreign" : "match",
        retained_elf_sha256,
        s_state.stack_low,
        s_state.stack_high,
        s_state.stack_bytes);

    uint32_t valid = 0u;
    uint32_t invalid = 0u;
    uint32_t minimum_stack_pointer = VOICE_STACK_DIAG_INVALID;
    uint32_t maximum_depth = 0u;
    for (uint32_t index = 0u;
         index < VOICE_STACK_DIAG_RECORD_CAPACITY;
         ++index) {
        const voice_stack_diag_record_t *record = &s_state.records[index];
        if (!voice_stack_diag_record_valid(record)) {
            if (record->commit_sequence != 0u) {
                invalid++;
            }
            continue;
        }
        valid++;
        if (record->stack_pointer < minimum_stack_pointer) {
            minimum_stack_pointer = record->stack_pointer;
        }
        if (record->stack_sample_valid != 0u &&
            record->stack_used_bytes > maximum_depth) {
            maximum_depth = record->stack_used_bytes;
        }
        ESP_LOGI(
            TAG,
            "VOICE_DIAG record: seq=%" PRIu32 " stage=%" PRIu32
            " task=0x%08" PRIx32 " sp=0x%08" PRIx32
            " stack_used=%" PRIu32 " stack_ok=%" PRIu32
            " entry_depth=%" PRIu32 " session=%" PRIu32
            " arg0=%" PRIu32 " arg1=%" PRIu32,
            record->sequence,
            record->stage,
            record->task_identity,
            record->stack_pointer,
            record->stack_used_bytes,
            record->stack_sample_valid,
            record->entry_depth,
            record->session_id,
            record->arg0,
            record->arg1);
    }
    ESP_LOGI(
        TAG,
        "VOICE_DIAG summary: valid_records=%" PRIu32
        " invalid_records=%" PRIu32 " minimum_sp=0x%08" PRIx32
        " maximum_stack_used=%" PRIu32,
        valid,
        invalid,
        minimum_stack_pointer,
        maximum_depth);
}

void voice_stack_diag_register_stack(void *base, uint32_t bytes)
{
    uintptr_t low = (uintptr_t)base;
    if (base == NULL || bytes == 0u || low > UINT32_MAX ||
        (uintptr_t)bytes > (uintptr_t)UINT32_MAX - low) {
        s_stack_low = 0u;
        s_stack_high = 0u;
        s_stack_bytes = 0u;
        return;
    }
    uintptr_t high = low + (uintptr_t)bytes;
    s_stack_low = (uint32_t)low;
    s_stack_high = (uint32_t)high;
    s_stack_bytes = bytes;
}

void voice_stack_diag_bind_current_task(void)
{
    s_bound_task = xTaskGetCurrentTaskHandle();
}

bool voice_stack_diag_arm(uint32_t run_id)
{
    if (run_id == 0u || __atomic_load_n(&s_active, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&s_session_active, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&s_armed, __ATOMIC_ACQUIRE) ||
        s_bound_task == NULL || s_stack_bytes == 0u) {
        return false;
    }

    const esp_app_desc_t *description = esp_app_get_description();
    if (description == NULL) {
        return false;
    }

    memset(&s_state, 0, sizeof(s_state));
    s_state.magic = VOICE_STACK_DIAG_MAGIC;
    s_state.schema_version = VOICE_STACK_DIAG_SCHEMA_VERSION;
    s_state.struct_bytes = sizeof(s_state);
    s_state.run_id = run_id;
    s_state.stack_low = s_stack_low;
    s_state.stack_high = s_stack_high;
    s_state.stack_bytes = s_stack_bytes;
    memcpy(
        s_state.firmware_elf_sha256,
        description->app_elf_sha256,
        VOICE_STACK_DIAG_ELF_SHA256_BYTES);
    s_state.header_commit = 0u;
    voice_stack_diag_refresh_header_checksum();
    __atomic_thread_fence(__ATOMIC_RELEASE);
    s_state.header_commit = VOICE_STACK_DIAG_HEADER_COMMIT;
    voice_stack_diag_commit_status(1u, 0u, 0u, 0u);
    s_record_index = 0u;
    memset(s_stage_slots, 0xff, sizeof(s_stage_slots));
    s_depth = 0u;
    __atomic_store_n(&s_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_session_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_armed, true, __ATOMIC_RELEASE);
    return true;
}

void voice_stack_diag_dump(void)
{
    if (__atomic_load_n(&s_active, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&s_session_active, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&s_armed, __ATOMIC_ACQUIRE)) {
        ESP_LOGW(TAG, "VOICE_DIAG dump rejected: trigger_or_recording_active");
        return;
    }
    if (!voice_stack_diag_header_valid()) {
        ESP_LOGW(TAG, "VOICE_DIAG dump: none_or_invalid");
        return;
    }

    ESP_LOGI(
        TAG,
        "VOICE_DIAG dump: run_id=%" PRIu32 " armed=%" PRIu32
        " finished=%" PRIu32 " overflow=%" PRIu32 " records=%" PRIu32,
        s_state.run_id,
        s_state.armed,
        s_state.finished,
        s_state.overflow,
        s_state.record_count);
    for (uint32_t index = 0u;
         index < VOICE_STACK_DIAG_RECORD_CAPACITY;
         ++index) {
        const voice_stack_diag_record_t *record = &s_state.records[index];
        if (!voice_stack_diag_record_valid(record)) {
            continue;
        }
        ESP_LOGI(
            TAG,
            "VOICE_DIAG dump_record: seq=%" PRIu32 " stage=%" PRIu32
            " task=0x%08" PRIx32 " sp=0x%08" PRIx32
            " stack_used=%" PRIu32 " stack_ok=%" PRIu32
            " entry_depth=%" PRIu32 " session=%" PRIu32
            " arg0=%" PRIu32 " arg1=%" PRIu32,
            record->sequence,
            record->stage,
            record->task_identity,
            record->stack_pointer,
            record->stack_used_bytes,
            record->stack_sample_valid,
            record->entry_depth,
            record->session_id,
            record->arg0,
            record->arg1);
    }
}

bool voice_stack_diag_enter(uint32_t source_tag)
{
    /* 1 is the existing explicit user-start classification (EC11/key1).
     * VoiceActivation candidates use other classifications and must never
     * consume a manually armed diagnostic run. */
    if (source_tag != 1u) {
        return false;
    }
    if (!__atomic_load_n(&s_armed, __ATOMIC_ACQUIRE) ||
        xTaskGetCurrentTaskHandle() != s_bound_task) {
        return false;
    }
    if (s_depth < UINT32_MAX) {
        s_depth++;
    }
    __atomic_store_n(&s_active, true, __ATOMIC_RELEASE);
    return true;
}

static bool voice_stack_diag_sample_is_better(
    const voice_stack_diag_record_t *previous,
    uint32_t stack_pointer,
    bool stack_sample_valid,
    uint32_t entry_depth)
{
    if (previous == NULL) {
        return true;
    }
    /* A valid-to-invalid (or invalid-to-valid) transition is evidence in its
     * own right. In particular, retain both lower- and higher-end SP escapes
     * instead of filtering the sample that proves the range is wrong. */
    if (stack_sample_valid != (previous->stack_sample_valid != 0u)) {
        return true;
    }
    if (stack_sample_valid && previous->stack_sample_valid != 0u &&
        stack_pointer < previous->stack_pointer) {
        return true;
    }
    return entry_depth > previous->entry_depth;
}

void voice_stack_diag_mark(
    uint32_t stage,
    uint32_t session_id,
    uint32_t arg0,
    uint32_t arg1)
{
    if (!__atomic_load_n(&s_active, __ATOMIC_ACQUIRE) ||
        !__atomic_load_n(&s_armed, __ATOMIC_RELAXED) ||
        xTaskGetCurrentTaskHandle() != s_bound_task) {
        return;
    }

    if (stage == 0u || stage > 31u) {
        return;
    }

    uint32_t stack_pointer = (uint32_t)(uintptr_t)esp_cpu_get_sp();
    bool stack_sample_valid =
        voice_stack_diag_stack_sample_valid(stack_pointer);
    uint32_t stack_used_bytes = stack_sample_valid
        ? s_stack_high - stack_pointer
        : 0u;
    uint32_t entry_depth = s_depth;
    int8_t previous_slot = s_stage_slots[stage - 1u];
    if (previous_slot >= 0 &&
        !voice_stack_diag_sample_is_better(
            &s_state.records[(uint32_t)previous_slot],
            stack_pointer,
            stack_sample_valid,
            entry_depth)) {
        return;
    }

    if (s_record_index >= VOICE_STACK_DIAG_RECORD_CAPACITY) {
        s_state.overflow = 1u;
        voice_stack_diag_commit_status(
            1u,
            0u,
            1u,
            s_record_index);
        return;
    }

    uint32_t sequence = s_record_index + 1u;
    voice_stack_diag_record_t *record = &s_state.records[s_record_index];
    record->commit_sequence = 0u;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    record->sequence = sequence;
    record->stage = stage;
    record->task_identity = VOICE_STACK_DIAG_TASK_ID;
    record->stack_pointer = stack_pointer;
    record->session_id = session_id;
    record->arg0 = arg0;
    record->arg1 = arg1;
    record->stack_used_bytes = stack_used_bytes;
    record->stack_sample_valid = stack_sample_valid ? 1u : 0u;
    record->entry_depth = entry_depth;
    record->checksum = voice_stack_diag_record_checksum(record);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    record->commit_sequence = sequence;
    s_stage_slots[stage - 1u] = (int8_t)s_record_index;
    s_record_index++;
    voice_stack_diag_commit_status(
        1u,
        0u,
        s_state.overflow,
        s_record_index);
}

void voice_stack_diag_leave(int32_t result)
{
    (void)result;
    if (!__atomic_load_n(&s_active, __ATOMIC_ACQUIRE) ||
        xTaskGetCurrentTaskHandle() != s_bound_task) {
        return;
    }
    if (s_depth > 1u) {
        s_depth--;
        return;
    }
    s_depth = 0u;
    voice_stack_diag_commit_status(
        0u,
        1u,
        s_state.overflow,
        s_record_index);
    /* Publish inactive only after the final status commit. DUMP can then
     * safely observe either the complete final state or an active run. */
    __atomic_store_n(&s_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_armed, false, __ATOMIC_RELEASE);
}

void voice_stack_diag_set_session_active(bool active)
{
    __atomic_store_n(&s_session_active, active, __ATOMIC_RELEASE);
}
