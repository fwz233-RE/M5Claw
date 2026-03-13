#include "dashscope_tts.h"
#include "m5claw_config.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>

static char s_api_key[320] = {0};

void DashScopeTTS::init(const char* apiKey) {
    strlcpy(s_api_key, apiKey, sizeof(s_api_key));
    Serial.println("[TTS] Initialized");
}

static bool skipHeaders(WiFiClientSecure& client, unsigned long deadline) {
    int state = 0;
    while (client.connected() && millis() < deadline) {
        if (!client.available()) { delay(1); continue; }
        char c = client.read();
        switch (state) {
            case 0: state = (c == '\r') ? 1 : (c == '\n') ? 2 : 0; break;
            case 1: state = (c == '\n') ? 2 : (c == '\r') ? 1 : 0; break;
            case 2: if (c == '\n') return true; state = (c == '\r') ? 3 : 0; break;
            case 3: if (c == '\n') return true; state = 0; break;
        }
    }
    return false;
}

// Decode base64 PCM data and append to buffer
static size_t decodeBase64PCM(const char* b64, size_t b64Len, int16_t* buffer, size_t offset, size_t maxSamples) {
    size_t outLen = 0;
    size_t maxBytes = (maxSamples - offset) * sizeof(int16_t);
    uint8_t* outBuf = (uint8_t*)(buffer + offset);

    int ret = mbedtls_base64_decode(outBuf, maxBytes, &outLen, (const uint8_t*)b64, b64Len);
    if (ret != 0) {
        Serial.printf("[TTS] Base64 decode error: %d\n", ret);
        return 0;
    }
    return outLen / sizeof(int16_t);
}

size_t DashScopeTTS::synthesize(const char* text, int16_t* buffer, size_t maxSamples) {
    if (!s_api_key[0] || !text || !text[0]) return 0;

    WiFiClientSecure client;
    client.setInsecure();

    if (!client.connect(M5CLAW_TTS_HOST, 443)) {
        Serial.println("[TTS] Connection failed");
        return 0;
    }

    // Build JSON body
    JsonDocument doc;
    doc["model"] = M5CLAW_TTS_MODEL;
    doc["input"]["text"] = text;
    doc["input"]["voice"] = M5CLAW_TTS_VOICE;
    doc["input"]["language_type"] = "Chinese";
    String body;
    serializeJson(doc, body);

    // Send request with SSE header for streaming
    client.printf("POST %s HTTP/1.1\r\n", M5CLAW_TTS_PATH);
    client.printf("Host: %s\r\n", M5CLAW_TTS_HOST);
    client.printf("Authorization: Bearer %s\r\n", s_api_key);
    client.println("Content-Type: application/json");
    client.println("X-DashScope-SSE: enable");
    client.printf("Content-Length: %d\r\n", body.length());
    client.println("Connection: close");
    client.println();
    client.print(body);

    unsigned long deadline = millis() + 30000;

    if (!skipHeaders(client, deadline)) {
        Serial.println("[TTS] Header timeout");
        client.stop();
        return 0;
    }

    // Read SSE stream and extract base64 audio data
    size_t totalSamples = 0;

    while (client.connected() && millis() < deadline) {
        if (!client.available()) { delay(1); continue; }

        String line = client.readStringUntil('\n');
        line.trim();

        if (line.startsWith("data:")) {
            String data = line.substring(5);
            data.trim();
            if (data.length() == 0) continue;

            JsonDocument eventDoc;
            if (deserializeJson(eventDoc, data) != DeserializationError::Ok) continue;

            JsonObject output = eventDoc["output"];
            if (output.isNull()) continue;

            JsonObject audio = output["audio"];
            if (audio.isNull()) continue;

            const char* audioData = audio["data"] | "";
            if (audioData[0] && totalSamples < maxSamples) {
                size_t decoded = decodeBase64PCM(audioData, strlen(audioData),
                                                  buffer, totalSamples, maxSamples);
                totalSamples += decoded;
            }

            const char* finishReason = output["finish_reason"] | "";
            if (strcmp(finishReason, "stop") == 0) {
                Serial.println("[TTS] Synthesis complete");
                break;
            }
        }
    }

    client.stop();
    Serial.printf("[TTS] Synthesized %d samples\n", (int)totalSamples);
    return totalSamples;
}
