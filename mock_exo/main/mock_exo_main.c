// Mock exo (task 5): stands in for the exo team's haptic task so the exo-side comms
// task (task 3) can be tested without the exoskeleton.
//
// Core 1: 1 kHz tick task. Generates closure from a scenario, builds the frame (packet,
//         CRC, COBS) and hands it to comms after a fixed, configurable latency. A
//         configurable share of frames get a wrong CRC. Toggles a GPIO each tick so the
//         rate can be checked on a logic analyzer.
// Core 0: comms task (not in this file).
//
// Sources: "Haptic Link Fundamentals" deck (Oct 10, 2026), slide numbers cited inline.
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "comms.h"
#include "frame.h"
#include "scenario.h"

// 1 kHz: deck slide 9, "every stage updates at least once every 1 ms".
#define TICK_PERIOD_MS 1

// Haptic task on core 1 at priority 10 (deck slide 21).
#define TICK_TASK_CORE 1
#define TICK_TASK_PRIO 10

// Whole ticks of delay between measuring closure and sending it. The 1 ms tick sets the
// resolution. Slots hold the last LATENCY_TICKS + 1 samples.
#define LATENCY_TICKS CONFIG_MOCK_EXO_LATENCY_MS

typedef struct {
    float closure;
    uint32_t time_us;  // when the value was measured, not when it is sent
} sample_t;

static sample_t s_delay_line[LATENCY_TICKS + 1];

// frame_no counts from 1. Returns the value to XOR into the CRC: 0 keeps it correct.
static uint16_t crc_xor_for(uint32_t frame_no)
{
#if CONFIG_MOCK_EXO_CRC_ERROR_EVERY > 0
    if (frame_no % CONFIG_MOCK_EXO_CRC_ERROR_EVERY == 0) {
        return (uint16_t)(esp_random() % 0xFFFF) + 1;
    }
#endif
#if CONFIG_MOCK_EXO_CRC_ERROR_PERMILLE > 0
    if (esp_random() % 1000 < CONFIG_MOCK_EXO_CRC_ERROR_PERMILLE) {
        return (uint16_t)(esp_random() % 0xFFFF) + 1;
    }
#endif
    (void)frame_no;
    return 0;
}

static void tick_task(void *arg)
{
    const scenario_t *sc = (const scenario_t *)arg;
    const gpio_num_t tick_gpio = (gpio_num_t)CONFIG_MOCK_EXO_TICK_GPIO;

    uint32_t level = 0;
    uint32_t sent = 0;  // frames sent; the u16 seq field is this value truncated
    TickType_t last_wake = xTaskGetTickCount();

    // The scenario repeats until power-off.
    for (uint32_t tick = 0;; tick++) {
        xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TICK_PERIOD_MS));

        level ^= 1;
        gpio_set_level(tick_gpio, level);

        uint32_t t_ms = (tick * TICK_PERIOD_MS) % sc->duration_ms;
        s_delay_line[tick % (LATENCY_TICKS + 1)] = (sample_t){
            .closure = scenario_slow_squeeze_closure(t_ms),
            .time_us = (uint32_t)esp_timer_get_time(),
        };

        // Oldest slot is the sample from LATENCY_TICKS ticks ago. Nothing to send until
        // the delay line has filled.
        if (tick >= LATENCY_TICKS) {
            const sample_t *out = &s_delay_line[(tick + 1) % (LATENCY_TICKS + 1)];
            // ack_seq is 0: the mock does not read force packets back yet.
            uint8_t frame[FRAME_LEN];
            size_t len = frame_build_closure((uint16_t)sent, 0, out->time_us, out->closure,
                                             crc_xor_for(sent + 1), frame);
            comms_send_frame(frame, len);
            sent++;
        }
    }
}

void app_main(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_MOCK_EXO_TICK_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    ESP_ERROR_CHECK(comms_init());

    xTaskCreatePinnedToCore(tick_task, "mock_haptic", 4096, (void *)&SCENARIO_SLOW_SQUEEZE,
                            TICK_TASK_PRIO, NULL, TICK_TASK_CORE);
}
