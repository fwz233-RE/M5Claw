#include "memory_store.h"
#include "m5claw_config.h"
#include <SPIFFS.h>

void MemoryStore::init() {
    if (!SPIFFS.begin(true)) {
        Serial.println("[MEM] SPIFFS mount failed");
        return;
    }
    Serial.printf("[MEM] SPIFFS: %d/%d bytes used\n",
                  SPIFFS.usedBytes(), SPIFFS.totalBytes());
}

String MemoryStore::readFile(const char* path) {
    File f = SPIFFS.open(path, "r");
    if (!f) return "";
    String content = f.readString();
    f.close();
    return content;
}

bool MemoryStore::writeFile(const char* path, const char* content) {
    File f = SPIFFS.open(path, "w");
    if (!f) return false;
    f.print(content);
    f.close();
    return true;
}

bool MemoryStore::appendFile(const char* path, const char* content) {
    File f = SPIFFS.open(path, "a");
    if (!f) return false;
    f.print(content);
    f.close();
    return true;
}

String MemoryStore::readSoul() { return readFile(M5CLAW_SOUL_FILE); }
String MemoryStore::readUser() { return readFile(M5CLAW_USER_FILE); }
String MemoryStore::readMemory() { return readFile(M5CLAW_MEMORY_FILE); }

bool MemoryStore::writeMemory(const char* content) {
    return writeFile(M5CLAW_MEMORY_FILE, content);
}
