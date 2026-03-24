#include "heartbeat.h"
#include "m5claw_config.h"
#include "message_bus.h"
#include "memory_store.h"
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

static TimerHandle_t s_timer = nullptr;
static unsigned long s_lastTickMs = 0;

static bool hasActionableContent(const char* text) {
    if (!text || !text[0]) return false;
    static const char* markers[] = {
        "TODO", "REMIND", "CHECK", "TASK", "ALERT",
        "todo", "remind", "check", "task", "alert",
        "- [ ]", "* [ ]"
    };
    for (int i = 0; i < (int)(sizeof(markers) / sizeof(markers[0])); i++) {
        if (strstr(text, markers[i])) return true;
    }
    return false;
}

static void heartbeatCheck(TimerHandle_t timer) {
    (void)timer;
    s_lastTickMs = millis();
    String content = MemoryStore::readFile(M5CLAW_HEARTBEAT_FILE);
    if (content.length() == 0 || !hasActionableContent(content.c_str())) return;

    char* prompt = (char*)malloc(content.length() + 128);
    if (!prompt) return;

    snprintf(prompt, content.length() + 128,
        "[Heartbeat] Review and act on pending tasks from HEARTBEAT.md:\n%s", content.c_str());

    BusMessage msg = {};
    strlcpy(msg.channel, M5CLAW_CHAN_SYSTEM, sizeof(msg.channel));
    strlcpy(msg.chat_id, "heartbeat", sizeof(msg.chat_id));
    msg.content = prompt;

    if (!MessageBus::pushInbound(&msg)) {
        free(prompt);
    } else {
        Serial.println("[HEARTBEAT] Triggered agent with pending tasks");
    }
}

void Heartbeat::init() {
    Serial.println("[HEARTBEAT] Ready");
}

void Heartbeat::start() {
    if (s_timer) return;
    s_lastTickMs = millis();
    s_timer = xTimerCreate("hb", pdMS_TO_TICKS(M5CLAW_HEARTBEAT_INTERVAL_MS),
                           pdTRUE, nullptr, heartbeatCheck);
    if (s_timer) {
        xTimerStart(s_timer, 0);
        Serial.printf("[HEARTBEAT] Timer started (%d min interval)\n",
                      M5CLAW_HEARTBEAT_INTERVAL_MS / 60000);
    }
}

unsigned long Heartbeat::getRemainingMs() {
    if (!s_timer) return 0;
    unsigned long elapsed = millis() - s_lastTickMs;
    if (elapsed >= M5CLAW_HEARTBEAT_INTERVAL_MS) return 0;
    return M5CLAW_HEARTBEAT_INTERVAL_MS - elapsed;
}

