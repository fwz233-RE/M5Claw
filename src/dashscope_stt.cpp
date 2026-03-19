#include "dashscope_stt.h"
#include "m5claw_config.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>

static char s_api_key[320] = {0};
static String s_result;
static String s_partial;
static volatile bool s_ws_connected = false;
static volatile bool s_task_started = false;
static volatile bool s_ws_done = false;
static bool s_streaming = false;
static WebSocketsClient* s_ws = nullptr;
static char s_task_id[48] = {0};

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
                const char* event = doc["header"]["event"] | doc["header"]["action"] | "";
                if (strcmp(event, "result-generated") == 0) {
                    JsonObject output = doc["payload"]["output"];
                    JsonObject sentence = output["sentence"];
                    const char* text = sentence["text"] | "";
                    if (text[0]) {
                        s_result = text;
                        s_partial = text;
                        Serial.printf("[STT] Text: %s\n", text);
                    }
                } else if (strcmp(event, "task-started") == 0) {
                    Serial.println("[STT] Task started");
                    s_task_started = true;
                } else if (strcmp(event, "task-finished") == 0) {
                    Serial.println("[STT] Task finished");
                    s_ws_done = true;
                } else if (strcmp(event, "task-failed") == 0) {
                    const char* errMsg = doc["header"]["error_message"] | doc["header"]["message"] | "unknown";
                    Serial.printf("[STT] Task failed: %s\n", errMsg);
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

bool DashScopeSTT::beginStream(SttAbortCheckFn shouldAbort) {
    if (!s_api_key[0]) return false;
    if (s_streaming) endStream();

    s_result = "";
    s_partial = "";
    s_ws_connected = false;
    s_task_started = false;
    s_ws_done = false;

    if (s_ws) { delete s_ws; s_ws = nullptr; }
    s_ws = new WebSocketsClient();
    if (!s_ws) {
        Serial.println("[STT] Failed to allocate WebSocket");
        return false;
    }

    Serial.printf("[STT] Connecting, heap=%d\n", ESP.getFreeHeap());

    char authHeader[384];
    snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s", s_api_key);
    s_ws->setExtraHeaders(authHeader);
    s_ws->onEvent(ws_event);
    s_ws->beginSSL(M5CLAW_STT_WS_HOST, M5CLAW_STT_WS_PORT, M5CLAW_STT_WS_PATH, "", "");

    unsigned long start = millis();
    while (!s_ws_connected && millis() - start < 10000) {
        if (shouldAbort && shouldAbort()) {
            Serial.println("[STT] Aborted during connect");
            delete s_ws; s_ws = nullptr;
            return false;
        }
        s_ws->loop();
        delay(10);
    }
    if (!s_ws_connected) {
        Serial.println("[STT] Connection timeout");
        delete s_ws; s_ws = nullptr;
        return false;
    }

    snprintf(s_task_id, sizeof(s_task_id), "stt-%lu", millis());
    JsonDocument startDoc;
    startDoc["header"]["action"] = "run-task";
    startDoc["header"]["task_id"] = s_task_id;
    startDoc["header"]["streaming"] = "duplex";
    startDoc["payload"]["model"] = M5CLAW_STT_MODEL;
    startDoc["payload"]["task_group"] = "audio";
    startDoc["payload"]["task"] = "asr";
    startDoc["payload"]["function"] = "recognition";
    startDoc["payload"]["parameters"]["format"] = "pcm";
    startDoc["payload"]["parameters"]["sample_rate"] = M5CLAW_STT_SAMPLE_RATE;
    startDoc["payload"]["input"].to<JsonObject>();
    String startJson;
    serializeJson(startDoc, startJson);
    s_ws->sendTXT(startJson);
    Serial.println("[STT] Sent run-task (streaming)");

    start = millis();
    while (!s_task_started && !s_ws_done && millis() - start < 5000) {
        if (shouldAbort && shouldAbort()) {
            Serial.println("[STT] Aborted during task start");
            delete s_ws; s_ws = nullptr;
            return false;
        }
        s_ws->loop();
        delay(10);
    }
    if (!s_task_started) {
        Serial.println("[STT] Task start timeout");
        delete s_ws; s_ws = nullptr;
        return false;
    }

    s_streaming = true;
    return true;
}

void DashScopeSTT::feedAudio(const int16_t* samples, size_t count) {
    if (!s_streaming || !s_ws || !s_ws_connected || s_ws_done) return;
    s_ws->sendBIN((const uint8_t*)samples, count * sizeof(int16_t));
}

void DashScopeSTT::poll() {
    if (s_ws) s_ws->loop();
}

bool DashScopeSTT::isStreaming() {
    return s_streaming && s_ws && s_ws_connected && !s_ws_done;
}

String DashScopeSTT::getPartialText() {
    return s_partial;
}

String DashScopeSTT::endStream() {
    if (!s_streaming) return s_result;
    s_streaming = false;

    if (s_ws && s_ws_connected && !s_ws_done) {
        JsonDocument finishDoc;
        finishDoc["header"]["action"] = "finish-task";
        finishDoc["header"]["task_id"] = s_task_id;
        finishDoc["payload"]["input"].to<JsonObject>();
        String finishJson;
        serializeJson(finishDoc, finishJson);
        s_ws->sendTXT(finishJson);

        unsigned long start = millis();
        while (!s_ws_done && millis() - start < 10000) {
            s_ws->loop();
            delay(10);
        }
    }

    delete s_ws;
    s_ws = nullptr;

    Serial.printf("[STT] Final result: %s\n", s_result.c_str());
    return s_result;
}

String DashScopeSTT::recognize(const int16_t* samples, size_t sampleCount) {
    if (!sampleCount) return "";
    if (!beginStream()) return "";

    const size_t chunkSamples = M5CLAW_STT_SAMPLE_RATE * M5CLAW_STT_CHUNK_MS / 1000;
    size_t sent = 0;
    while (sent < sampleCount && !s_ws_done) {
        size_t toSend = sampleCount - sent;
        if (toSend > chunkSamples) toSend = chunkSamples;
        feedAudio(samples + sent, toSend);
        sent += toSend;
        poll();
        delay(5);
    }

    return endStream();
}
