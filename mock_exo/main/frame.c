#include "frame.h"

#include <string.h>

static uint16_t crc16_ccitt_false(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

// Packets here are 15 bytes, so no run of 254 non-zero bytes can occur and the
// 0xFF block case is left out. out must hold len + 1 bytes.
static size_t cobs_encode_short(const uint8_t *in, size_t len, uint8_t *out)
{
    size_t code_i = 0;
    size_t o = 1;
    uint8_t code = 1;

    for (size_t i = 0; i < len; i++) {
        if (in[i] == 0) {
            out[code_i] = code;
            code_i = o++;
            code = 1;
        } else {
            out[o++] = in[i];
            code++;
        }
    }
    out[code_i] = code;
    return o;
}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    put_u16(p, (uint16_t)v);
    put_u16(p + 2, (uint16_t)(v >> 16));
}

size_t frame_build_closure(uint16_t seq, uint16_t ack_seq, uint32_t time_us, float closure,
                           uint16_t crc_xor, uint8_t out[FRAME_LEN])
{
    uint8_t packet[PACKET_LEN];
    uint32_t payload_bits;
    memcpy(&payload_bits, &closure, sizeof payload_bits);

    packet[0] = FRAME_TYPE_CLOSURE;
    put_u16(&packet[1], seq);
    put_u16(&packet[3], ack_seq);
    put_u32(&packet[5], time_us);
    put_u32(&packet[9], payload_bits);
    put_u16(&packet[13], crc16_ccitt_false(packet, 13) ^ crc_xor);

    size_t n = cobs_encode_short(packet, PACKET_LEN, out);
    out[n++] = 0x00;
    return n;
}
