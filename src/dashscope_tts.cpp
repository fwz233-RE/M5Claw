#include "dashscope_tts.h"
#include "m5claw_config.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

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

static int readHttpBody(WiFiClientSecure& client, char* buf, int bufSize, unsigned long deadline) {
    int len = 0;
    while (len < bufSize - 1 && millis() < deadline) {
        if (client.available()) {
            buf[len++] = client.read();
        } else if (!client.connected()) {
            break;
        } else {
            delay(1);
        }
    }
    buf[len] = '\0';
    return len;
}

static bool parseHttpsUrl(const String& url, String& host, String& path) {
    if (!url.startsWith("https://")) return false;
    int slash = url.indexOf('/', 8);
    if (slash < 0) {
        host = url.substring(8);
        path = "/";
    } else {
        host = url.substring(8, slash);
        path = url.substring(slash);
    }
    return host.length() > 0;
}

size_t DashScopeTTS::synthesize(const char* text, int16_t* buffer, size_t maxSamples) {
    if (!s_api_key[0] || !text || !text[0]) return 0;

    WiFiClientSecure client;
    client.setInsecure();

    if (!client.connect(M5CLAW_TTS_HOST, 443)) {
        Serial.println("[TTS] Connection failed");
        return 0;
    }

    JsonDocument doc;
    doc["model"] = M5CLAW_TTS_MODEL;
    doc["input"]["text"] = text;
    doc["input"]["voice"] = M5CLAW_TTS_VOICE;
    doc["input"]["language_type"] = "Chinese";
    doc["stream"] = false;
    String body;
    serializeJson(doc, body);

    client.printf("POST %s HTTP/1.1\r\n", M5CLAW_TTS_PATH);
    client.printf("Host: %s\r\n", M5CLAW_TTS_HOST);
    client.printf("Authorization: Bearer %s\r\n", s_api_key);
    client.println("Content-Type: application/json");
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

    char jsonBuf[4096];
    int jsonLen = readHttpBody(client, jsonBuf, sizeof(jsonBuf), deadline);
    client.stop();

    if (jsonLen <= 0) {
        Serial.println("[TTS] Empty response");
        return 0;
    }

    JsonDocument respDoc;
    if (deserializeJson(respDoc, jsonBuf, jsonLen) != DeserializationError::Ok) {
        Serial.println("[TTS] Failed to parse JSON response");
        return 0;
    }

    const char* audioUrl = respDoc["output"]["audio"]["url"] | "";
    if (!audioUrl[0]) {
        Serial.println("[TTS] No audio URL in response");
        return 0;
    }

    String host, path;
    if (!parseHttpsUrl(String(audioUrl), host, path)) {
        Serial.println("[TTS] Invalid audio URL");
        return 0;
    }

    WiFiClientSecure audioClient;
    audioClient.setInsecure();
    if (!audioClient.connect(host.c_str(), 443)) {
        Serial.println("[TTS] Audio download connect failed");
        return 0;
    }

    audioClient.printf("GET %s HTTP/1.1\r\n", path.c_str());
    audioClient.printf("Host: %s\r\n", host.c_str());
    audioClient.println("Connection: close");
    audioClient.println();

    deadline = millis() + 30000;
    if (!skipHeaders(audioClient, deadline)) {
        Serial.println("[TTS] Audio header timeout");
        audioClient.stop();
        return 0;
    }

    size_t maxBytes = maxSamples * sizeof(int16_t);
    uint8_t* rawBuf = (uint8_t*)buffer;
    size_t totalBytes = 0;
    while (totalBytes < maxBytes && millis() < deadline) {
        if (audioClient.available()) {
            rawBuf[totalBytes++] = audioClient.read();
        } else if (!audioClient.connected()) {
            break;
        } else {
            delay(1);
        }
    }
    audioClient.stop();

    if (totalBytes <= 44) {
        Serial.println("[TTS] Audio data too short");
        return 0;
    }

    memmove(rawBuf, rawBuf + 44, totalBytes - 44);
    size_t pcmBytes = totalBytes - 44;
    size_t totalSamples = pcmBytes / sizeof(int16_t);
    Serial.printf("[TTS] Downloaded %d samples from URL\n", (int)totalSamples);
    return totalSamples;
}
