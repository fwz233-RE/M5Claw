#include "dashscope_stt.h"
#include "m5claw_config.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>

static char s_api_key[320] = {0};
static String s_result;
static volatile bool s_ws_connected = false;
static volatile bool s_task_started = false;
static volatile bool s_ws_done = false;
static WebSocketsClient* s_ws = nullptr;

static void ws_event(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            s_ws_connected = true;
            Serial.println("[STT] WS connected");
            break;
        case WStype_DISCONNECTED:
            s_ws_connected = false;
            s_ws_done = true;
            Serial.println("[STT] WS disconnected");
            break;
        case WStype_TEXT: {
            JsonDocument doc;
            if (deserializeJson(doc, payload, length) == DeserializationError::Ok) {
                const char* action = doc["header"]["action"] | "";
                if (strcmp(action, "result-generated") == 0) {
                    JsonObject output = doc["payload"]["output"];
                    JsonObject sentence = output["sentence"];
                    const char* text = sentence["text"] | "";
                    if (text[0]) {
                        s_result = text;
                        Serial.printf("[STT] Text: %s\n", text);
                    }
                } else if (strcmp(action, "task-started") == 0) {
                    Serial.println("[STT] Task started");
                    s_task_started = true;
                } else if (strcmp(action, "task-finished") == 0) {
                    Serial.println("[STT] Task finished");
                    s_ws_done = true;
                } else if (strcmp(action, "task-failed") == 0) {
                    Serial.printf("[STT] Task failed: %s\n", (const char*)(doc["header"]["message"] | "unknown"));
                    s_ws_done = true;
                }
            }
            break;
        }
        default:
            break;
    }
}

void DashScopeSTT::init(const char* apiKey) {
    strlcpy(s_api_key, apiKey, sizeof(s_api_key));
    Serial.println("[STT] Initialized");
}

String DashScopeSTT::recognize(const int16_t* samples, size_t sampleCount) {
    if (!s_api_key[0] || sampleCount == 0) return "";

    s_result = "";
    s_ws_connected = false;
    s_task_started = false;
    s_ws_done = false;

    if (s_ws) { delete s_ws; s_ws = nullptr; }
    s_ws = new WebSocketsClient();

    char authHeader[384];
    snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s", s_api_key);

    s_ws->beginSSL(M5CLAW_STT_WS_HOST, M5CLAW_STT_WS_PORT, M5CLAW_STT_WS_PATH, (const uint8_t*)nullptr);
    s_ws->setExtraHeaders(authHeader);
    s_ws->onEvent(ws_event);

    // Wait for connection
    unsigned long start = millis();
    while (!s_ws_connected && millis() - start < 10000) {
        s_ws->loop();
        delay(10);
    }
    if (!s_ws_connected) {
        Serial.println("[STT] Connection timeout");
        delete s_ws; s_ws = nullptr;
        return "";
    }

    // Send run-task
    char taskId[48];
    snprintf(taskId, sizeof(taskId), "stt-%lu", millis());
    JsonDocument startDoc;
    startDoc["header"]["action"] = "run-task";
    startDoc["header"]["task_id"] = taskId;
    startDoc["header"]["streaming"] = "duplex";
    startDoc["payload"]["model"] = M5CLAW_STT_MODEL;
    startDoc["payload"]["task_group"] = "audio";
    startDoc["payload"]["task"] = "asr";
    startDoc["payload"]["function"] = "recognition";
    startDoc["payload"]["parameters"]["format"] = "pcm";
    startDoc["payload"]["parameters"]["sample_rate"] = M5CLAW_STT_SAMPLE_RATE;
    String startJson;
    serializeJson(startDoc, startJson);
    s_ws->sendTXT(startJson);
    Serial.printf("[STT] Sent run-task, %d samples to send\n", (int)sampleCount);

    // Wait for task-started
    start = millis();
    while (!s_task_started && !s_ws_done && millis() - start < 5000) {
        s_ws->loop();
        delay(10);
    }
    if (!s_task_started) {
        Serial.println("[STT] Task start timeout");
        delete s_ws; s_ws = nullptr;
        return "";
    }

    // Send audio in chunks
    const size_t chunkSamples = M5CLAW_STT_SAMPLE_RATE * M5CLAW_STT_CHUNK_MS / 1000;
    const size_t chunkBytes = chunkSamples * sizeof(int16_t);
    const uint8_t* audioBytes = (const uint8_t*)samples;
    size_t totalBytes = sampleCount * sizeof(int16_t);
    size_t sent = 0;

    while (sent < totalBytes && !s_ws_done) {
        size_t toSend = totalBytes - sent;
        if (toSend > chunkBytes) toSend = chunkBytes;
        s_ws->sendBIN(audioBytes + sent, toSend);
        sent += toSend;
        s_ws->loop();
        delay(5);
    }

    // Send finish-task
    JsonDocument finishDoc;
    finishDoc["header"]["action"] = "finish-task";
    finishDoc["header"]["task_id"] = taskId;
    finishDoc["payload"]["input"] = JsonObject();
    String finishJson;
    serializeJson(finishDoc, finishJson);
    s_ws->sendTXT(finishJson);

    // Wait for final result
    start = millis();
    while (!s_ws_done && millis() - start < 10000) {
        s_ws->loop();
        delay(10);
    }

    delete s_ws;
    s_ws = nullptr;

    Serial.printf("[STT] Final result: %s\n", s_result.c_str());
    return s_result;
}
