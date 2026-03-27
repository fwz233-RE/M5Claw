#pragma once

#include <WiFiClientSecure.h>

namespace TlsConfig {
    void configureClient(WiFiClientSecure& client, uint32_t timeoutMs);
    const uint8_t* caBundle();
}
