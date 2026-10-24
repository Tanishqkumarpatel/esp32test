/* Mock-exo sender: builds closure frames from the slow-squeeze scenario and
   transmits them over UART1 (GPIO 16 TX, GPIO 17 RX) at 921600 baud.

   Board A (USB0) runs this firmware.
   Board B (USB1) runs the receiver that decodes and prints the frames.

   Frame format (from mock_exo):
     15-byte packet: type(1) | seq(2) | ack_seq(2) | time_us(4) | closure(4) | crc16(2)
     COBS-encoded + 0x00 delimiter → 17 bytes on the wire.
*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include "string.h"
#include "driver/gpio.h"

#include "frame.h"
#include "scenario.h"

static const char *TAG = "MOCK_SENDER";

/* ---------- UART config ---------- */
#define TXD_PIN       (16)
#define RXD_PIN       (17)
#define BAUD_RATE     921600
#define RX_BUF_SIZE   1024

/* ---------- Tick config ---------- */
/* Send one frame every TICK_MS milliseconds.
   1 ms = 1 kHz is the design target but FreeRTOS tick resolution on the
   default 100 Hz scheduler is 10 ms. We use 10 ms here so frames actually
   go out; change CONFIG_FREERTOS_HZ to 1000 for true 1 kHz. */
#define TICK_MS  10

static void init_uart(void)
{
    const uart_config_t uart_config = {
        .baud_rate  = BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

/* ---------- TX task: sends mock-exo closure frames ---------- */
static void tx_task(void *arg)
{
    uint16_t seq = 0;
    TickType_t last_wake = xTaskGetTickCount();

    ESP_LOGI(TAG, "TX task started – sending closure frames every %d ms", TICK_MS);

    for (uint32_t tick = 0; ; tick++) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TICK_MS));

        /* Time within the scenario (wraps at duration_ms). */
        uint32_t t_ms = (tick * TICK_MS) % SCENARIO_SLOW_SQUEEZE.duration_ms;
        float closure = scenario_slow_squeeze_closure(t_ms);
        uint32_t time_us = (uint32_t)esp_timer_get_time();

        /* Build the COBS-framed packet. */
        uint8_t frame[FRAME_LEN];
        size_t len = frame_build_closure(seq, 0, time_us, closure, 0, frame);

        /* Send over UART1. */
        uart_write_bytes(UART_NUM_1, frame, len);

        /* Print every 100th frame so we can see progress without flooding. */
        if (seq % 100 == 0) {
            ESP_LOGI(TAG, "seq=%u  closure=%.4f  t_ms=%lu  len=%u",
                     seq, closure, (unsigned long)t_ms, (unsigned)len);
        }
        seq++;
    }
}

/* ---------- RX task: reads any response from the receiver ---------- */
static void rx_task(void *arg)
{
    uint8_t *data = (uint8_t *)malloc(RX_BUF_SIZE + 1);

    while (1) {
        int rxBytes = uart_read_bytes(UART_NUM_1, data, RX_BUF_SIZE,
                                      1000 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            data[rxBytes] = '\0';
            ESP_LOGI(TAG, "RX got %d bytes: '%s'", rxBytes, data);
        }
    }
    free(data);
}

void app_main(void)
{
    init_uart();
    ESP_LOGI(TAG, "Mock-exo sender on UART1 (TX=%d, RX=%d) @ %d baud", TXD_PIN, RXD_PIN, BAUD_RATE);
    xTaskCreate(rx_task, "uart_rx_task", 3072, NULL, configMAX_PRIORITIES - 1, NULL);
    xTaskCreate(tx_task, "uart_tx_task", 4096, NULL, configMAX_PRIORITIES - 2, NULL);
}
