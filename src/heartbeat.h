#pragma once
#include <Arduino.h>

namespace Heartbeat {
    void init();
    void start();
    unsigned long getRemainingMs();
}
