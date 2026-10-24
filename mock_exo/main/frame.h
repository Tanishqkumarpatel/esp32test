// Closure frame builder: 15-byte packet, COBS-encoded, then a 0x00 delimiter.
// C port of build_frame() in tools/fake_haptic.py; keep the two in step.
//
//   packet: type u8 | seq u16 | ack_seq u16 | time_us u32 | payload f32 | crc16 u16
//   little-endian; crc16 is CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over the 13 bytes before it.
//
// TBD (assumed until task 3 publishes the packet spec): byte order, type code, CRC variant.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define FRAME_TYPE_CLOSURE 0x01  // TBD: packet type codes are not defined yet

#define PACKET_LEN 15
// COBS adds one byte for a packet under 254 bytes, plus the 0x00 delimiter.
#define FRAME_LEN  (PACKET_LEN + 2)

// Builds one closure frame into out (FRAME_LEN bytes) and returns its length.
// crc_xor is XORed into the CRC: 0 leaves it correct, any other value makes it wrong.
// No heap, no blocking: safe on the 1 kHz task.
size_t frame_build_closure(uint16_t seq, uint16_t ack_seq, uint32_t time_us, float closure,
                           uint16_t crc_xor, uint8_t out[FRAME_LEN]);
