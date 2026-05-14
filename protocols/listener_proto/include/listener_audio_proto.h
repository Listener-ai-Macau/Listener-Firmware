#ifndef LISTENER_AUDIO_PROTO_H
#define LISTENER_AUDIO_PROTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LISTENER_AUDIO_PROTO_MAGIC "VKA1"
#define LISTENER_AUDIO_PROTO_MAGIC_U32 0x31414B56u
#define LISTENER_AUDIO_PROTO_HEADER_BYTES 20u

typedef enum {
    LISTENER_AUDIO_PACKET_TYPE_SESSION_START = 1,
    LISTENER_AUDIO_PACKET_TYPE_AUDIO_DATA = 2,
    LISTENER_AUDIO_PACKET_TYPE_AUDIO_CHUNK = LISTENER_AUDIO_PACKET_TYPE_AUDIO_DATA,
    LISTENER_AUDIO_PACKET_TYPE_SESSION_STOP = 3,
    LISTENER_AUDIO_PACKET_TYPE_SESSION_CANCEL = 4,
    LISTENER_AUDIO_PACKET_TYPE_SESSION_ERROR = 5,
} listener_audio_packet_type_t;

/*
 * Phase-1 continuous notify streaming keeps the legacy wire layout stable while
 * reinterpreting the old chunk fields as packet-sequence metadata.
 */
typedef struct __attribute__((packed)) {
    uint8_t magic[4];
    uint8_t packet_type;
    uint8_t flags;
    uint16_t header_len_le;
    uint32_t session_id_le;
    uint16_t chunk_index_le;      /* audio_data: packet_sequence; stop/cancel: expected_packet_count */
    uint8_t fragment_index;       /* continuous stream phase: always 0 */
    uint8_t fragment_count;       /* continuous stream phase: always 1 */
    uint16_t payload_len_le;
    uint16_t chunk_pcm_bytes_le;  /* audio_data: this packet's PCM bytes */
    uint32_t reserved_le;
} listener_audio_packet_header_t;

static inline void listener_audio_proto_header_init(
    listener_audio_packet_header_t *header,
    listener_audio_packet_type_t packet_type,
    uint32_t session_id,
    uint16_t chunk_index,
    uint8_t fragment_index,
    uint8_t fragment_count,
    uint16_t payload_len,
    uint16_t chunk_pcm_bytes)
{
    if (header == NULL) {
        return;
    }

    header->magic[0] = 'V';
    header->magic[1] = 'K';
    header->magic[2] = 'A';
    header->magic[3] = '1';
    header->packet_type = (uint8_t)packet_type;
    header->flags = 0;
    header->header_len_le = LISTENER_AUDIO_PROTO_HEADER_BYTES;
    header->session_id_le = session_id;
    header->chunk_index_le = chunk_index;
    header->fragment_index = fragment_index;
    header->fragment_count = fragment_count;
    header->payload_len_le = payload_len;
    header->chunk_pcm_bytes_le = chunk_pcm_bytes;
    header->reserved_le = 0;
}

#ifdef __cplusplus
}
#endif

#endif
