/* Mock-exo receiver: reads COBS-framed closure packets from UART1,
   decodes them, validates CRC-16/CCITT-FALSE, and prints the closure value.

   Board B (USB1) runs this firmware.
   Board A (USB0) runs the sender that generates the frames.

   Frame format (from mock_exo):
     15-byte packet: type(1) | seq(2) | ack_seq(2) | time_us(4) | closure(4) | crc16(2)
     COBS-encoded + 0x00 delimiter → 17 bytes on the wire.
*/
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"

#include "frame.h"

static const char *TAG = "MOCK_RECV";

/* ---------- UART config (must match the sender) ---------- */
#define TXD_PIN       (16)
#define RXD_PIN       (17)
#define BAUD_RATE     921600
#define BUF_SIZE      1024

/* ---------- CRC-16/CCITT-FALSE (same as frame.c) ---------- */
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

/* ---------- COBS decode ---------- */
/* Decodes in-place. Returns decoded length, or 0 on error. */
static size_t cobs_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max)
{
    size_t o = 0;
    size_t i = 0;

    while (i < in_len) {
        uint8_t code = in[i++];
        if (code == 0) {
            return 0; /* unexpected zero inside COBS data */
        }
        for (uint8_t j = 1; j < code; j++) {
            if (i >= in_len || o >= out_max) return 0;
            out[o++] = in[i++];
        }
        /* If code < 0xFF and there's more data, output a zero. */
        if (code < 0xFF && i < in_len) {
            if (o >= out_max) return 0;
            out[o++] = 0x00;
        }
    }
    return o;
}

/* ---------- helpers ---------- */
static uint16_t get_u16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t get_u32(const uint8_t *p) { return get_u16(p) | ((uint32_t)get_u16(p + 2) << 16); }
static float    get_f32(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }

/* ---------- Frame ring buffer ---------- */
/* We accumulate bytes until we see a 0x00 delimiter, then decode the frame. */
static uint8_t s_ring[64];
static size_t  s_ring_len = 0;

static uint32_t s_good_frames = 0;
static uint32_t s_bad_crc     = 0;
static uint32_t s_bad_decode  = 0;

static void process_frame(const uint8_t *cobs_data, size_t cobs_len)
{
    uint8_t packet[PACKET_LEN + 4]; /* a little extra room */
    size_t pkt_len = cobs_decode(cobs_data, cobs_len, packet, sizeof(packet));

    if (pkt_len != PACKET_LEN) {
        s_bad_decode++;
        return;
    }

    /* Validate CRC over the first 13 bytes; CRC is at bytes 13-14. */
    uint16_t crc_computed = crc16_ccitt_false(packet, 13);
    uint16_t crc_received = get_u16(&packet[13]);
    if (crc_computed != crc_received) {
        s_bad_crc++;
        if (s_bad_crc % 50 == 1) {
            ESP_LOGW(TAG, "CRC FAIL #%lu  (got 0x%04X, expected 0x%04X)",
                     (unsigned long)s_bad_crc, crc_received, crc_computed);
        }
        return;
    }

    /* Parse fields. */
    uint8_t  type    = packet[0];
    uint16_t seq     = get_u16(&packet[1]);
    uint16_t ack_seq = get_u16(&packet[3]);
    uint32_t time_us = get_u32(&packet[5]);
    float    closure = get_f32(&packet[9]);

    s_good_frames++;

    /* Print every 100th good frame to avoid flooding the console. */
    if (s_good_frames % 100 == 1) {
        ESP_LOGI(TAG, "OK #%lu  type=%u seq=%u ack=%u time_us=%lu closure=%.4f",
                 (unsigned long)s_good_frames, type, seq, ack_seq,
                 (unsigned long)time_us, closure);
    }
}

/* Feed raw UART bytes into the frame assembler. */
static void feed_bytes(const uint8_t *data, int len)
{
    for (int i = 0; i < len; i++) {
        if (data[i] == 0x00) {
            /* Delimiter: process whatever is in the ring. */
            if (s_ring_len > 0) {
                process_frame(s_ring, s_ring_len);
                s_ring_len = 0;
            }
        } else {
            if (s_ring_len < sizeof(s_ring)) {
                s_ring[s_ring_len++] = data[i];
            } else {
                /* Overflow — discard and wait for next delimiter. */
                s_ring_len = 0;
            }
        }
    }
}

/* ---------- Receiver task ---------- */
static void recv_task(void *arg)
{
    uint8_t *data = (uint8_t *)malloc(BUF_SIZE);
    TickType_t last_stats = xTaskGetTickCount();

    while (1) {
        int rxBytes = uart_read_bytes(UART_NUM_1, data, BUF_SIZE, 100 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            feed_bytes(data, rxBytes);
        }

        /* Print stats every 5 seconds. */
        if (xTaskGetTickCount() - last_stats >= pdMS_TO_TICKS(5000)) {
            ESP_LOGI(TAG, "[stats] good=%lu  bad_crc=%lu  bad_decode=%lu",
                     (unsigned long)s_good_frames,
                     (unsigned long)s_bad_crc,
                     (unsigned long)s_bad_decode);
            last_stats = xTaskGetTickCount();
        }
    }
    free(data);
}

void app_main(void)
{
    /* UART1 init */
    uart_config_t uart_config = {
        .baud_rate  = BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "Mock-exo receiver on UART1 (TX=%d, RX=%d) @ %d baud", TXD_PIN, RXD_PIN, BAUD_RATE);
    ESP_LOGI(TAG, "Waiting for closure frames ...");

    xTaskCreate(recv_task, "recv_task", 4096, NULL, 10, NULL);
}
