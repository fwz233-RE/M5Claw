#pragma once
#include <Arduino.h>

namespace SkillLoader {
    void init();
    size_t buildSummary(char* buf, size_t size);
}
