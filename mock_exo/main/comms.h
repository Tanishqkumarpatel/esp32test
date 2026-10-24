// Comms API boundary between the exo haptic task (core 1) and the comms task (core 0).
//
// PLACEHOLDER: the real header belongs to task 3 (exo-side comms, Piyusha).
// Function names are from the deck, slide 37: comms_send_closure(),
// comms_get_force() (newtons + age), comms_send_fault().
// The exact signatures, types and fault codes below are ASSUMPTIONS until comms.h lands.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    float force_n;    // Grip force in newtons. Sign convention TBD (open item).
    uint32_t age_us;  // How old the force is when read (deck slide 37: "force + age").
    bool valid;       // false if no force packet received yet / link down.
} comms_force_t;

// TBD: fault code values are not defined in the contract yet.
typedef uint8_t comms_fault_t;

esp_err_t comms_init(void);

// closure: 0 = open, 1 = closed (deck slide 5).
// time_us: when the value was measured (deck slide 10, packet field time_us).
// Must return immediately: called from the 1 kHz haptic task on core 1. Overwrite the
// closure mailbox and wake the comms task; packet build, CRC and UART happen on core 0.
esp_err_t comms_send_closure(float closure, uint32_t time_us);

// PLACEHOLDER (mock exo addition, not in the deck): hands a frame already built on core 1
// (packet, CRC and COBS done, 0x00 delimiter included) to the comms task to transmit as is.
// Lets the mock inject frames with a wrong CRC. Same rule: return immediately. The bytes are
// copied before returning, so the caller may reuse the buffer.
esp_err_t comms_send_frame(const uint8_t *frame, size_t len);

// Non-blocking: reads the newest force from the mailbox (deck slide 25, queue of 1).
// Returns false if no valid force is available.
bool comms_get_force(comms_force_t *out);

esp_err_t comms_send_fault(comms_fault_t code);
