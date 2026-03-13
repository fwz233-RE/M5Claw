#include "session_mgr.h"
#include "m5claw_config.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

static String sessionPath(const char* chat_id) {
    String path = M5CLAW_SESSION_DIR;
    path += "/";
    path += chat_id;
    path += ".jsonl";
    return path;
}

void SessionMgr::init() {
    if (!SPIFFS.exists(M5CLAW_SESSION_DIR)) {
        SPIFFS.mkdir(M5CLAW_SESSION_DIR);
    }
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

void SessionMgr::clearSession(const char* chat_id) {
    SPIFFS.remove(sessionPath(chat_id));
}
