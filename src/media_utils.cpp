#include "media_utils.h"
#include "m5claw_config.h"
#include "tls_utils.h"
#include <SPIFFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

static constexpr unsigned long kMediaHttpTimeoutMs = 20000;

static bool parseHttpsUrl(const char* url, char* host, size_t hostSize,
                          char* path, size_t pathSize) {
    if (!url || strncmp(url, "https://", 8) != 0) return false;
    const char* p = url + 8;
    const char* slash = strchr(p, '/');
    if (!slash) {
        strlcpy(host, p, hostSize);
        strlcpy(path, "/", pathSize);
        return host[0] != '\0';
    }
    size_t hostLen = slash - p;
    if (hostLen == 0 || hostLen >= hostSize) return false;
    memcpy(host, p, hostLen);
    host[hostLen] = '\0';
    strlcpy(path, slash, pathSize);
    return true;
}

static bool skipHeaders(WiFiClientSecure& client, unsigned long deadline, bool* chunked,
                        char* mimeTypeOut, size_t mimeTypeOutSize) {
    *chunked = false;
    if (mimeTypeOut && mimeTypeOutSize) mimeTypeOut[0] = '\0';

    char line[256];
    int pos = 0;
    int state = 0;
    while (client.connected() && millis() < deadline) {
        if (!client.available()) { delay(1); continue; }
        char c = client.read();
        if (c != '\r' && c != '\n' && pos < (int)sizeof(line) - 1) line[pos++] = c;
        switch (state) {
            case 0: state = (c == '\r') ? 1 : (c == '\n') ? 2 : 0; break;
            case 1: state = (c == '\n') ? 2 : (c == '\r') ? 1 : 0; break;
            case 2: if (c == '\n') return true; state = (c == '\r') ? 3 : 0; break;
            case 3: if (c == '\n') return true; state = 0; break;
        }
        if (c == '\n') {
            line[pos] = '\0';
            if (strncasecmp(line, "Transfer-Encoding:", 18) == 0) {
                const char* v = line + 18;
                while (*v == ' ') v++;
                if (strncasecmp(v, "chunked", 7) == 0) *chunked = true;
            } else if (mimeTypeOut && mimeTypeOutSize &&
                       strncasecmp(line, "Content-Type:", 13) == 0) {
                const char* v = line + 13;
                while (*v == ' ') v++;
                size_t n = strcspn(v, ";");
                if (n >= mimeTypeOutSize) n = mimeTypeOutSize - 1;
                memcpy(mimeTypeOut, v, n);
                mimeTypeOut[n] = '\0';
            }
            pos = 0;
        }
    }
    return false;
}

static bool readChunkedBodyToFile(WiFiClientSecure& client, File& f, size_t maxBytes,
                                  unsigned long deadline) {
    size_t total = 0;
    while (true) {
        char sizeBuf[16];
        int pos = 0;
        while (pos < (int)sizeof(sizeBuf) - 1 && millis() < deadline) {
            while (!client.available()) {
                if (millis() >= deadline) return false;
                if (!client.connected()) return false;
                delay(1);
            }
            char c = client.read();
            if (c == '\n') break;
            if (c != '\r') sizeBuf[pos++] = c;
        }
        sizeBuf[pos] = '\0';
        size_t chunkSize = strtoul(sizeBuf, nullptr, 16);
        if (chunkSize == 0) return true;
        while (chunkSize > 0) {
            uint8_t buf[512];
            size_t want = chunkSize > sizeof(buf) ? sizeof(buf) : chunkSize;
            size_t got = 0;
            while (got < want) {
                while (!client.available()) {
                    if (millis() >= deadline) return false;
                    if (!client.connected()) return false;
                    delay(1);
                }
                got += client.read(buf + got, want - got);
            }
            total += got;
            if (total > maxBytes) return false;
            if (f.write(buf, got) != got) return false;
            chunkSize -= got;
        }
        for (int i = 0; i < 2; i++) {
            while (!client.available()) {
                if (millis() >= deadline) return false;
                if (!client.connected()) return false;
                delay(1);
            }
            client.read();
        }
    }
}

static bool readRawBodyToFile(WiFiClientSecure& client, File& f, size_t maxBytes,
                              unsigned long deadline) {
    size_t total = 0;
    uint8_t buf[512];
    while (true) {
        if (millis() >= deadline) return false;
        if (client.available()) {
            int got = client.read(buf, sizeof(buf));
            if (got <= 0) continue;
            total += got;
            if (total > maxBytes) return false;
            if (f.write(buf, got) != (size_t)got) return false;
        } else if (!client.connected()) {
            return true;
        } else {
            delay(1);
        }
    }
}

const char* MediaUtils::guessMimeType(const char* path, const char* fallbackMime) {
    if (!path) return fallbackMime;
    const char* ext = strrchr(path, '.');
    if (!ext) return fallbackMime;
    ext++;
    if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) return "image/jpeg";
    if (strcasecmp(ext, "png") == 0) return "image/png";
    if (strcasecmp(ext, "webp") == 0) return "image/webp";
    if (strcasecmp(ext, "gif") == 0) return "image/gif";
    if (strcasecmp(ext, "wav") == 0) return "audio/wav";
    if (strcasecmp(ext, "mp3") == 0) return "audio/mpeg";
    if (strcasecmp(ext, "m4a") == 0) return "audio/m4a";
    if (strcasecmp(ext, "ogg") == 0) return "audio/ogg";
    if (strcasecmp(ext, "flac") == 0) return "audio/flac";
    return fallbackMime;
}

bool MediaUtils::downloadHttpsUrlToFile(const char* url, const char* destPath,
                                        char* mimeTypeOut, size_t mimeTypeOutSize,
                                        size_t maxBytes) {
    if (WiFi.status() != WL_CONNECTED || !url || !destPath) return false;

    char host[128];
    char path[384];
    if (!parseHttpsUrl(url, host, sizeof(host), path, sizeof(path))) {
        Serial.printf("[MEDIA] Unsupported URL: %s\n", url ? url : "(null)");
        return false;
    }

    WiFiClientSecure client;
    TlsConfig::configureClient(client, 20000);
    if (!client.connect(host, 443)) {
        Serial.printf("[MEDIA] Connect failed: %s\n", host);
        return false;
    }

    client.printf("GET %s HTTP/1.1\r\n", path);
    client.printf("Host: %s\r\n", host);
    client.println("Connection: close");
    client.println();

    unsigned long deadline = millis() + kMediaHttpTimeoutMs;
    bool chunked = false;
    char headerMime[48];
    if (!skipHeaders(client, deadline, &chunked, headerMime, sizeof(headerMime))) {
        client.stop();
        return false;
    }

    File f = SPIFFS.open(destPath, "w");
    if (!f) {
        client.stop();
        return false;
    }

    bool ok = chunked ? readChunkedBodyToFile(client, f, maxBytes, deadline)
                      : readRawBodyToFile(client, f, maxBytes, deadline);
    f.close();
    client.stop();

    if (!ok) {
        SPIFFS.remove(destPath);
        return false;
    }

    const char* fallbackMime = guessMimeType(path);
    const char* finalMime = headerMime[0] ? headerMime : fallbackMime;
    if (mimeTypeOut && mimeTypeOutSize) strlcpy(mimeTypeOut, finalMime, mimeTypeOutSize);
    return true;
}
