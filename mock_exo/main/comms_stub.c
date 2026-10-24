// Stub comms backend so the mock exo builds and runs before task 3 exists.
// It sends nothing and always reports "no force". Disable with
// CONFIG_MOCK_EXO_COMMS_STUB once the real comms component is linked.
#include "comms.h"

esp_err_t comms_init(void)
{
    return ESP_OK;
}

esp_err_t comms_send_closure(float closure, uint32_t time_us)
{
    (void)closure;
    (void)time_us;
    return ESP_OK;
}

esp_err_t comms_send_frame(const uint8_t *frame, size_t len)
{
    (void)frame;
    (void)len;
    return ESP_OK;
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
    return ESP_OK;
}
