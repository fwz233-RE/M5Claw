#include "dashscope_tts.h"
#include "m5claw_config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

static char s_api_key[320] = {0};

static bool secureConnect(WiFiClientSecure& client, const char* host, uint16_t port, const char* tag) {
    IPAddress ip;
    if (!WiFi.hostByName(host, ip)) {
        Serial.printf("%s DNS failed for %s\n", tag, host);
        return false;
    }
    Serial.printf("%s DNS %s -> %s, heap=%d\n", tag, host, ip.toString().c_str(), ESP.getFreeHeap());
    client.setInsecure();
    client.setTimeout(15000);
    return client.connect(ip, port);
}

static bool plainConnect(WiFiClient& client, const char* host, uint16_t port, const char* tag) {
    IPAddress ip;
    if (!WiFi.hostByName(host, ip)) {
        Serial.printf("%s DNS failed for %s\n", tag, host);
        return false;
    }
    client.setTimeout(15000);
    return client.connect(ip, port);
}

void DashScopeTTS::init(const char* apiKey) {
    strlcpy(s_api_key, apiKey, sizeof(s_api_key));
    Serial.println("[TTS] Initialized");
}

template <typename TClient>
static bool skipHeaders(TClient& client, unsigned long deadline) {
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

template <typename TClient>
static int readHttpBody(TClient& client, char* buf, int bufSize, unsigned long deadline) {
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

struct ParsedUrl {
    bool secure;
    String host;
    String path;
    uint16_t port;
};

static bool parseUrl(const String& url, ParsedUrl& parsed) {
    int schemeLen = 0;
    if (url.startsWith("https://")) {
        parsed.secure = true;
        parsed.port = 443;
        schemeLen = 8;
    } else if (url.startsWith("http://")) {
        parsed.secure = false;
        parsed.port = 80;
        schemeLen = 7;
    } else {
        return false;
    }

    int slash = url.indexOf('/', schemeLen);
    String hostPort = (slash < 0) ? url.substring(schemeLen) : url.substring(schemeLen, slash);
    parsed.path = (slash < 0) ? "/" : url.substring(slash);

    int colon = hostPort.indexOf(':');
    if (colon >= 0) {
        parsed.host = hostPort.substring(0, colon);
        long port = hostPort.substring(colon + 1).toInt();
        if (port <= 0 || port > 65535) return false;
        parsed.port = (uint16_t)port;
    } else {
        parsed.host = hostPort;
    }

    return parsed.host.length() > 0;
}

size_t DashScopeTTS::synthesize(const char* text, int16_t* buffer, size_t maxSamples) {
    if (!s_api_key[0] || !text || !text[0]) return 0;

    Serial.printf("[TTS] synthesize start, heap=%d, stack=%d\n",
                  ESP.getFreeHeap(), uxTaskGetStackHighWaterMark(NULL));

    ParsedUrl audioUrlInfo;

    // Phase 1: POST to get audio URL (scope limits WiFiClientSecure lifetime)
    {
        WiFiClientSecure client;
        if (!secureConnect(client, M5CLAW_TTS_HOST, 443, "[TTS]")) {
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

        const int JSON_BUF_SIZE = 4096;
        char* jsonBuf = (char*)malloc(JSON_BUF_SIZE);
        if (!jsonBuf) {
            Serial.println("[TTS] Failed to alloc json buffer");
            client.stop();
            return 0;
        }

        int jsonLen = readHttpBody(client, jsonBuf, JSON_BUF_SIZE, deadline);
        client.stop();

        if (jsonLen <= 0) {
            Serial.println("[TTS] Empty response");
            free(jsonBuf);
            return 0;
        }

        JsonDocument respDoc;
        if (deserializeJson(respDoc, jsonBuf, jsonLen) != DeserializationError::Ok) {
            Serial.println("[TTS] Failed to parse JSON response");
            free(jsonBuf);
            return 0;
        }
        free(jsonBuf);

        const char* audioUrl = respDoc["output"]["audio"]["url"] | "";
        if (!audioUrl[0]) {
            Serial.println("[TTS] No audio URL in response");
            return 0;
        }

        if (!parseUrl(String(audioUrl), audioUrlInfo)) {
            Serial.println("[TTS] Invalid audio URL");
            return 0;
        }
    } // client destroyed here — stack reclaimed before phase 2

    // Phase 2: Download audio from URL
    size_t maxBytes = maxSamples * sizeof(int16_t);
    uint8_t* rawBuf = (uint8_t*)buffer;
    size_t totalBytes = 0;
    unsigned long deadline = millis() + 30000;

    if (audioUrlInfo.secure) {
        WiFiClientSecure audioClient;
        if (!secureConnect(audioClient, audioUrlInfo.host.c_str(), audioUrlInfo.port, "[TTS-DL]")) {
            Serial.println("[TTS] Audio download connect failed");
            return 0;
        }

        audioClient.printf("GET %s HTTP/1.1\r\n", audioUrlInfo.path.c_str());
        audioClient.printf("Host: %s\r\n", audioUrlInfo.host.c_str());
        audioClient.println("Connection: close");
        audioClient.println();

        if (!skipHeaders(audioClient, deadline)) {
            Serial.println("[TTS] Audio header timeout");
            audioClient.stop();
            return 0;
        }

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
    } else {
        WiFiClient audioClient;
        if (!plainConnect(audioClient, audioUrlInfo.host.c_str(), audioUrlInfo.port, "[TTS-DL]")) {
            Serial.println("[TTS] Audio download connect failed");
            return 0;
        }

        audioClient.printf("GET %s HTTP/1.1\r\n", audioUrlInfo.path.c_str());
        audioClient.printf("Host: %s\r\n", audioUrlInfo.host.c_str());
        audioClient.println("Connection: close");
        audioClient.println();

        if (!skipHeaders(audioClient, deadline)) {
            Serial.println("[TTS] Audio header timeout");
            audioClient.stop();
            return 0;
        }

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
    }

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
