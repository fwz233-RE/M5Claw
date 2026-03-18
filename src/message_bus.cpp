#include "message_bus.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

static QueueHandle_t s_inbound  = nullptr;
static QueueHandle_t s_outbound = nullptr;

void MessageBus::init() {
    s_inbound  = xQueueCreate(M5CLAW_BUS_QUEUE_LEN, sizeof(BusMessage));
    s_outbound = xQueueCreate(M5CLAW_BUS_QUEUE_LEN, sizeof(BusMessage));
    Serial.printf("[BUS] Initialized (depth %d)\n", M5CLAW_BUS_QUEUE_LEN);
}

bool MessageBus::pushInbound(const BusMessage* msg) {
    if (!s_inbound) return false;
    if (xQueueSend(s_inbound, msg, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("[BUS] Inbound queue full");
        return false;
    }
    return true;
}

bool MessageBus::popInbound(BusMessage* msg, uint32_t timeoutMs) {
    if (!s_inbound) return false;
    TickType_t ticks = (timeoutMs == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
    return xQueueReceive(s_inbound, msg, ticks) == pdTRUE;
}

bool MessageBus::pushOutbound(const BusMessage* msg) {
    if (!s_outbound) return false;
    if (xQueueSend(s_outbound, msg, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("[BUS] Outbound queue full");
        return false;
    }
    return true;
}

bool MessageBus::popOutbound(BusMessage* msg, uint32_t timeoutMs) {
    if (!s_outbound) return false;
    TickType_t ticks = (timeoutMs == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
    return xQueueReceive(s_outbound, msg, ticks) == pdTRUE;
}
