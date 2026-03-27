#pragma once
#include <Arduino.h>
#include "m5claw_config.h"

enum BusMediaKind : uint8_t {
    BUS_MEDIA_NONE = 0,
    BUS_MEDIA_IMAGE = 1,
    BUS_MEDIA_AUDIO = 2,
};

struct BusMessage {
    char channel[16];
    char chat_id[96];
    char msg_id[64];
    char* content;   // heap-allocated, receiver must free()
    char media_path[96];
    char media_mime[32];
    uint8_t media_kind;
};

namespace MessageBus {
    void init();
    bool pushInbound(const BusMessage* msg);
    bool popInbound(BusMessage* msg, uint32_t timeoutMs = UINT32_MAX);
    bool pushOutbound(const BusMessage* msg);
    bool popOutbound(BusMessage* msg, uint32_t timeoutMs = 0);
}
