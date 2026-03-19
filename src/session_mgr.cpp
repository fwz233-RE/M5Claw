#include "session_mgr.h"
#include "m5claw_config.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

static String sessionPath(const char* chat_id) {
    const char* id = chat_id;
    char hashed[16];
    if (strlen(chat_id) > 16) {
        uint32_t h = 5381;
        for (const char* p = chat_id; *p; p++) h = h * 33 + (unsigned char)*p;
        snprintf(hashed, sizeof(hashed), "h%08x", h);
        id = hashed;
    }
    String path = "/sessions/";
    path += id;
    path += ".jsonl";
    return path;
}

void SessionMgr::init() {
    // SPIFFS has flat filesystem, no mkdir needed
}

bool SessionMgr::appendMessage(const char* chat_id, const char* role, const char* content) {
    String path = sessionPath(chat_id);
    File f = SPIFFS.open(path, "a");
    if (!f) return false;

    JsonDocument doc;
    doc["role"] = role;
    doc["content"] = content;
    String line;
    serializeJson(doc, line);
    f.println(line);
    f.close();
    return true;
}

String SessionMgr::getHistoryJson(const char* chat_id, int maxMessages) {
    String path = sessionPath(chat_id);
    File f = SPIFFS.open(path, "r");
    if (!f) return "[]";

    String lines[M5CLAW_SESSION_MAX_MSGS];
    int count = 0;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            lines[count % M5CLAW_SESSION_MAX_MSGS] = line;
            count++;
        }
    }
    f.close();

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    int start = (count > maxMessages) ? count - maxMessages : 0;
    int total = (count > M5CLAW_SESSION_MAX_MSGS) ? M5CLAW_SESSION_MAX_MSGS : count;
    int ringStart = (count > M5CLAW_SESSION_MAX_MSGS) ? (count % M5CLAW_SESSION_MAX_MSGS) : 0;

    for (int i = 0; i < total && i < maxMessages; i++) {
        int idx = (ringStart + i) % M5CLAW_SESSION_MAX_MSGS;
        if (lines[idx].length() == 0) continue;
        JsonDocument lineDoc;
        if (deserializeJson(lineDoc, lines[idx]) == DeserializationError::Ok) {
            arr.add(lineDoc.as<JsonVariant>());
        }
    }

    String result;
    serializeJson(doc, result);
    return result;
}

