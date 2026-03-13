#pragma once
#include <Arduino.h>

namespace MemoryStore {
    void init();
    String readFile(const char* path);
    bool writeFile(const char* path, const char* content);
    bool appendFile(const char* path, const char* content);
    String readSoul();
    String readUser();
    String readMemory();
    bool writeMemory(const char* content);
}
