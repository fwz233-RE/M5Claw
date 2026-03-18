#pragma once
#include <Arduino.h>
#include "m5claw_config.h"

struct BusMessage {
    char channel[16];
    char chat_id[96];
    char* content;   // heap-allocated, receiver must free()
};

namespace MessageBus {
    void init();
    bool pushInbound(const BusMessage* msg);
    bool popInbound(BusMessage* msg, uint32_t timeoutMs = UINT32_MAX);
    bool pushOutbound(const BusMessage* msg);
    bool popOutbound(BusMessage* msg, uint32_t timeoutMs = 0);
}
