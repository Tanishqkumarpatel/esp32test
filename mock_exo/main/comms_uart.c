// Built-in UART1 comms backend for the mock exo. Stands in for the real comms task (task 3)
// when only the wire needs testing.
//
// Core 1 (tick task): comms_send_frame() copies the frame into a queue and returns.
// Core 0 (comms_tx task): drains the queue and writes to UART1, so the blocking UART write
//                         never runs on the 1 kHz task.
//
// UART setup follows uart_async_rxtxtasks in esp32test (UART1, 8N1, no flow control).
// Receive is not parsed yet: comms_get_force() always reports "no force".
#include <inttypes.h>
#include <stdatomic.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "comms.h"
#include "frame.h"

#define UART_PORT UART_NUM_1

#define COMMS_TASK_CORE 0
// TBD: comms task priority is not in the deck. Below the haptic task (10), above background.
#define COMMS_TASK_PRIO 5

// 64 frames is 64 ms of backlog at 1 kHz. If the baud rate is too low for 17 B/ms the queue
// fills and frames are dropped and counted instead of blocking the tick.
#define TX_QUEUE_LEN 64
#define RX_BUF_SIZE  256  // the driver requires more than UART_HW_FIFO_LEN (128)

typedef struct {
    uint8_t len;
    uint8_t data[FRAME_LEN];
} tx_item_t;

static const char *TAG = "comms_uart";

static QueueHandle_t s_tx_queue;
static _Atomic uint32_t s_dropped;  // written on core 1, read on core 0
static uint16_t s_closure_seq;      // only used by comms_send_closure(), called from one task

static void comms_tx_task(void *arg)
{
    (void)arg;
    tx_item_t item;
    uint32_t reported = 0;
    TickType_t last_report = xTaskGetTickCount();

    for (;;) {
        if (xQueueReceive(s_tx_queue, &item, pdMS_TO_TICKS(1000)) == pdTRUE) {
            uart_write_bytes(UART_PORT, item.data, item.len);
        }

        // At most one warning a second, so a full queue cannot flood the console.
        uint32_t dropped = atomic_load(&s_dropped);
        if (dropped != reported && xTaskGetTickCount() - last_report >= pdMS_TO_TICKS(1000)) {
            ESP_LOGW(TAG, "%" PRIu32 " frames dropped so far (tx queue full; baud too low?)", dropped);
            reported = dropped;
            last_report = xTaskGetTickCount();
        }
    }
}

esp_err_t comms_init(void)
{
    const uart_config_t cfg = {
        .baud_rate = CONFIG_MOCK_EXO_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    s_tx_queue = xQueueCreate(TX_QUEUE_LEN, sizeof(tx_item_t));
    if (!s_tx_queue) {
        return ESP_ERR_NO_MEM;
    }

    // No driver TX buffer: uart_write_bytes() blocks until the bytes are in the FIFO, which
    // is fine on core 0.
    esp_err_t err = uart_driver_install(UART_PORT, RX_BUF_SIZE, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_param_config(UART_PORT, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_set_pin(UART_PORT, CONFIG_MOCK_EXO_UART_TX_GPIO, CONFIG_MOCK_EXO_UART_RX_GPIO,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        return err;
    }

    if (xTaskCreatePinnedToCore(comms_tx_task, "comms_tx", 3072, NULL,
                                COMMS_TASK_PRIO, NULL, COMMS_TASK_CORE) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t comms_send_frame(const uint8_t *frame, size_t len)
{
    if (!frame || len == 0 || len > FRAME_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    tx_item_t item = { .len = (uint8_t)len };
    memcpy(item.data, frame, len);

    // Timeout 0: never block the 1 kHz task.
    if (xQueueSend(s_tx_queue, &item, 0) != pdTRUE) {
        atomic_fetch_add(&s_dropped, 1);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t comms_send_closure(float closure, uint32_t time_us)
{
    uint8_t frame[FRAME_LEN];
    size_t len = frame_build_closure(s_closure_seq++, 0, time_us, closure, 0, frame);
    return comms_send_frame(frame, len);
}

bool comms_get_force(comms_force_t *out)
{
    out->force_n = 0.0f;
    out->age_us = UINT32_MAX;
    out->valid = false;
    return false;
}

esp_err_t comms_send_fault(comms_fault_t code)
{
    (void)code;
    return ESP_ERR_NOT_SUPPORTED;  // TBD: fault packet type and codes are not defined yet
}
