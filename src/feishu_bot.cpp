#include "feishu_bot.h"
#include "m5claw_config.h"
#include "config.h"
#include "message_bus.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/* ── Feishu API endpoints ──────────────────────────── */
#define FS_AUTH_PATH    "/open-apis/auth/v3/tenant_access_token/internal"
#define FS_SEND_PATH    "/open-apis/im/v1/messages"
#define FS_WS_CFG_URL   "https://open.feishu.cn/callback/ws/endpoint"

/* ── State ─────────────────────────────────────────── */
static char s_app_id[64]       = {0};
static char s_app_secret[128]  = {0};
static char s_tenant_token[512]= {0};
static int64_t s_token_expire  = 0;

static WebSocketsClient* s_ws  = nullptr;
static TaskHandle_t s_task     = nullptr;
static bool s_running          = false;
static bool s_ws_connected     = false;

static char s_ws_host[128]     = {0};
static uint16_t s_ws_port      = 443;
static char s_ws_path[256]     = {0};
static int s_service_id        = 0;
static int s_ping_interval_ms  = 120000;

/* ── Incoming message display queue ─────────────────── */
#define FEISHU_DISPLAY_QUEUE_SIZE 6
static char* s_displayQueue[FEISHU_DISPLAY_QUEUE_SIZE] = {};
static volatile int s_displayHead = 0;
static volatile int s_displayTail = 0;

/* ── Deduplication ─────────────────────────────────── */
static uint64_t s_seen_keys[M5CLAW_FEISHU_DEDUP_SIZE] = {0};
static size_t s_seen_idx = 0;

static uint64_t fnv1a64(const char* s) {
    uint64_t h = 1469598103934665603ULL;
    if (!s) return h;
    while (*s) {
        h ^= (unsigned char)(*s++);
        h *= 1099511628211ULL;
    }
    return h;
}

static bool dedupCheck(const char* msgId) {
    uint64_t key = fnv1a64(msgId);
    for (size_t i = 0; i < M5CLAW_FEISHU_DEDUP_SIZE; i++) {
        if (s_seen_keys[i] == key) return true;
    }
    s_seen_keys[s_seen_idx] = key;
    s_seen_idx = (s_seen_idx + 1) % M5CLAW_FEISHU_DEDUP_SIZE;
    return false;
}

/* ── Protobuf helpers (binary wire format) ─────────── */
typedef struct { char key[32]; char value[128]; } WsHeader;
typedef struct {
    uint64_t seq_id, log_id;
    int32_t  service, method;
    WsHeader headers[16];
    size_t   header_count;
    const uint8_t* payload;
    size_t   payload_len;
} WsFrame;

static bool pbReadVarint(const uint8_t* buf, size_t len, size_t* pos, uint64_t* out) {
    uint64_t v = 0; int shift = 0;
    while (*pos < len && shift <= 63) {
        uint8_t b = buf[(*pos)++];
        v |= ((uint64_t)(b & 0x7F)) << shift;
        if (!(b & 0x80)) { *out = v; return true; }
        shift += 7;
    }
    return false;
}

static bool pbSkip(const uint8_t* buf, size_t len, size_t* pos, uint8_t wt) {
    uint64_t n = 0;
    switch (wt) {
        case 0: return pbReadVarint(buf, len, pos, &n);
        case 1: if (*pos + 8 > len) return false; *pos += 8; return true;
        case 2: if (!pbReadVarint(buf, len, pos, &n)) return false;
                if (*pos + (size_t)n > len) return false; *pos += (size_t)n; return true;
        case 5: if (*pos + 4 > len) return false; *pos += 4; return true;
        default: return false;
    }
}

static bool pbParseHeaderMsg(const uint8_t* buf, size_t len, WsHeader* h) {
    memset(h, 0, sizeof(*h));
    size_t pos = 0;
    while (pos < len) {
        uint64_t tag = 0, slen = 0;
        if (!pbReadVarint(buf, len, &pos, &tag)) return false;
        uint32_t field = (uint32_t)(tag >> 3);
        uint8_t wt = (uint8_t)(tag & 0x07);
        if (wt != 2) { if (!pbSkip(buf, len, &pos, wt)) return false; continue; }
        if (!pbReadVarint(buf, len, &pos, &slen)) return false;
        if (pos + (size_t)slen > len) return false;
        if (field == 1) {
            size_t n = (slen < sizeof(h->key) - 1) ? (size_t)slen : sizeof(h->key) - 1;
            memcpy(h->key, buf + pos, n); h->key[n] = '\0';
        } else if (field == 2) {
            size_t n = (slen < sizeof(h->value) - 1) ? (size_t)slen : sizeof(h->value) - 1;
            memcpy(h->value, buf + pos, n); h->value[n] = '\0';
        }
        pos += (size_t)slen;
    }
    return true;
}

static bool pbParseFrame(const uint8_t* buf, size_t len, WsFrame* f) {
    memset(f, 0, sizeof(*f));
    size_t pos = 0;
    while (pos < len) {
        uint64_t tag = 0, v = 0, blen = 0;
        if (!pbReadVarint(buf, len, &pos, &tag)) return false;
        uint32_t field = (uint32_t)(tag >> 3);
        uint8_t wt = (uint8_t)(tag & 0x07);
        if (field == 1 && wt == 0) { if (!pbReadVarint(buf, len, &pos, &f->seq_id)) return false; }
        else if (field == 2 && wt == 0) { if (!pbReadVarint(buf, len, &pos, &f->log_id)) return false; }
        else if (field == 3 && wt == 0) { if (!pbReadVarint(buf, len, &pos, &v)) return false; f->service = (int32_t)v; }
        else if (field == 4 && wt == 0) { if (!pbReadVarint(buf, len, &pos, &v)) return false; f->method = (int32_t)v; }
        else if (field == 5 && wt == 2) {
            if (!pbReadVarint(buf, len, &pos, &blen)) return false;
            if (pos + (size_t)blen > len) return false;
            if (f->header_count < 16) pbParseHeaderMsg(buf + pos, (size_t)blen, &f->headers[f->header_count++]);
            pos += (size_t)blen;
        }
        else if (field == 8 && wt == 2) {
            if (!pbReadVarint(buf, len, &pos, &blen)) return false;
            if (pos + (size_t)blen > len) return false;
            f->payload = buf + pos; f->payload_len = (size_t)blen;
            pos += (size_t)blen;
        }
        else { if (!pbSkip(buf, len, &pos, wt)) return false; }
    }
    return true;
}

static const char* frameHeaderValue(const WsFrame* f, const char* key) {
    for (size_t i = 0; i < f->header_count; i++) {
        if (strcmp(f->headers[i].key, key) == 0) return f->headers[i].value;
    }
    return nullptr;
}

/* ── Protobuf write helpers ────────────────────────── */
static bool pbWriteVarint(uint8_t* buf, size_t cap, size_t* pos, uint64_t value) {
    do {
        if (*pos >= cap) return false;
        uint8_t b = (uint8_t)(value & 0x7F); value >>= 7;
        if (value) b |= 0x80;
        buf[(*pos)++] = b;
    } while (value);
    return true;
}

static bool pbWriteTag(uint8_t* buf, size_t cap, size_t* pos, uint32_t field, uint8_t wt) {
    return pbWriteVarint(buf, cap, pos, ((uint64_t)field << 3) | wt);
}

static bool pbWriteBytes(uint8_t* buf, size_t cap, size_t* pos, uint32_t field, const uint8_t* data, size_t len) {
    if (!pbWriteTag(buf, cap, pos, field, 2)) return false;
    if (!pbWriteVarint(buf, cap, pos, len)) return false;
    if (*pos + len > cap) return false;
    memcpy(buf + *pos, data, len); *pos += len;
    return true;
}

static bool pbWriteString(uint8_t* buf, size_t cap, size_t* pos, uint32_t field, const char* s) {
    return pbWriteBytes(buf, cap, pos, field, (const uint8_t*)s, strlen(s));
}

static bool wsEncodeHeader(uint8_t* dst, size_t cap, size_t* outLen, const char* key, const char* value) {
    size_t pos = 0;
    if (!pbWriteString(dst, cap, &pos, 1, key)) return false;
    if (!pbWriteString(dst, cap, &pos, 2, value)) return false;
    *outLen = pos;
    return true;
}

static int wsSendFrame(const WsFrame* f, const uint8_t* payload, size_t payloadLen) {
    uint8_t* out = (uint8_t*)malloc(1024);
    if (!out) return -1;
    size_t pos = 0;
    const size_t cap = 1024;
    if (!pbWriteTag(out, cap, &pos, 1, 0) || !pbWriteVarint(out, cap, &pos, f->seq_id)) { free(out); return -1; }
    if (!pbWriteTag(out, cap, &pos, 2, 0) || !pbWriteVarint(out, cap, &pos, f->log_id)) { free(out); return -1; }
    if (!pbWriteTag(out, cap, &pos, 3, 0) || !pbWriteVarint(out, cap, &pos, (uint32_t)f->service)) { free(out); return -1; }
    if (!pbWriteTag(out, cap, &pos, 4, 0) || !pbWriteVarint(out, cap, &pos, (uint32_t)f->method)) { free(out); return -1; }
    for (size_t i = 0; i < f->header_count; i++) {
        uint8_t hb[256]; size_t hlen = 0;
        if (!wsEncodeHeader(hb, sizeof(hb), &hlen, f->headers[i].key, f->headers[i].value)) { free(out); return -1; }
        if (!pbWriteBytes(out, cap, &pos, 5, hb, hlen)) { free(out); return -1; }
    }
    if (payload && payloadLen > 0) {
        if (!pbWriteBytes(out, cap, &pos, 8, payload, payloadLen)) { free(out); return -1; }
    }
    int ret = -1;
    if (s_ws) { s_ws->sendBIN(out, pos); ret = (int)pos; }
    free(out);
    return ret;
}

/* ── HTTPS helper ──────────────────────────────────── */
static bool resolveHost(const char* host, IPAddress& ip) {
    if (WiFi.status() != WL_CONNECTED) return false;
    for (int i = 1; i <= 2; i++) {
        if (WiFi.hostByName(host, ip)) return true;
        delay(100);
    }
    return false;
}

static String httpsPost(const char* host, const char* path, const char* body, const char* authHeader) {
    if (WiFi.status() != WL_CONNECTED) return "";

    WiFiClientSecure* client = new WiFiClientSecure();
    if (!client) { Serial.println("[FEISHU] OOM for TLS client"); return ""; }
    client->setInsecure();
    client->setTimeout(10000);

    IPAddress ip;
    if (!resolveHost(host, ip)) { Serial.printf("[FEISHU] DNS failed: %s\n", host); delete client; return ""; }
    if (!client->connect(ip, 443)) { Serial.println("[FEISHU] Connect failed"); delete client; return ""; }

    client->printf("POST %s HTTP/1.1\r\n", path);
    client->printf("Host: %s\r\n", host);
    client->println("Content-Type: application/json; charset=utf-8");
    if (authHeader && authHeader[0]) client->printf("Authorization: %s\r\n", authHeader);
    client->printf("Content-Length: %d\r\n", strlen(body));
    client->println("Connection: close");
    client->println();
    client->print(body);

    unsigned long deadline = millis() + 15000;
    int state = 0;
    bool headersDone = false;
    while (client->connected() && millis() < deadline) {
        if (!client->available()) { delay(1); continue; }
        char c = client->read();
        switch (state) {
            case 0: state = (c == '\r') ? 1 : (c == '\n') ? 2 : 0; break;
            case 1: state = (c == '\n') ? 2 : (c == '\r') ? 1 : 0; break;
            case 2: if (c == '\n') { headersDone = true; } state = (c == '\r') ? 3 : 0; break;
            case 3: if (c == '\n') { headersDone = true; } state = 0; break;
        }
        if (headersDone) break;
    }

    String resp;
    resp.reserve(2048);
    while (millis() < deadline) {
        if (client->available()) {
            char c = client->read();
            if (resp.length() < 4096) resp += c;
        } else if (!client->connected()) break;
        else delay(1);
    }
    client->stop();
    delete client;
    return resp;
}

/* ── Tenant token management ───────────────────────── */
static bool refreshToken() {
    if (!s_app_id[0] || !s_app_secret[0]) return false;

    int64_t now = (int64_t)(millis() / 1000);
    if (s_tenant_token[0] && s_token_expire > now + 300) return true;

    JsonDocument doc;
    doc["app_id"] = s_app_id;
    doc["app_secret"] = s_app_secret;
    String body;
    serializeJson(doc, body);

    String resp = httpsPost(M5CLAW_FEISHU_API_BASE, FS_AUTH_PATH, body.c_str(), nullptr);
    if (resp.length() == 0) return false;

    // handle chunked: find first '{'
    int jsonStart = resp.indexOf('{');
    if (jsonStart < 0) return false;

    JsonDocument rdoc;
    if (deserializeJson(rdoc, resp.c_str() + jsonStart)) return false;
    int code = rdoc["code"] | -1;
    if (code != 0) { Serial.printf("[FEISHU] Token error code=%d\n", code); return false; }

    const char* token = rdoc["tenant_access_token"];
    int expire = rdoc["expire"] | 7200;
    if (token) {
        strlcpy(s_tenant_token, token, sizeof(s_tenant_token));
        s_token_expire = now + expire - 300;
        Serial.printf("[FEISHU] Token refreshed (expires %ds)\n", expire);
        return true;
    }
    return false;
}

/* ── Feishu API call ───────────────────────────────── */
static String feishuApiCall(const char* path, const char* body) {
    if (!refreshToken()) return "";
    char auth[600];
    snprintf(auth, sizeof(auth), "Bearer %s", s_tenant_token);
    return httpsPost(M5CLAW_FEISHU_API_BASE, path, body, auth);
}

/* ── Handle incoming message event ─────────────────── */
static void handleMessageEvent(JsonObject event) {
    JsonObject message = event["message"];
    if (message.isNull()) return;

    const char* messageId = message["message_id"] | "";
    const char* chatId    = message["chat_id"] | "";
    const char* chatType  = message["chat_type"] | "p2p";
    const char* msgType   = message["message_type"] | "text";
    const char* contentRaw = message["content"] | "";

    if (!chatId[0] || !contentRaw[0]) return;
    if (messageId[0] && dedupCheck(messageId)) return;
    if (strcmp(msgType, "text") != 0) return;

    JsonDocument contentDoc;
    if (deserializeJson(contentDoc, contentRaw)) return;
    const char* text = contentDoc["text"] | "";
    const char* cleaned = text;
    if (strncmp(cleaned, "@_user_1 ", 9) == 0) cleaned += 9;
    while (*cleaned == ' ' || *cleaned == '\n') cleaned++;
    if (!cleaned[0]) return;

    // routing: DM -> sender open_id, group -> chat_id
    const char* routeId = chatId;
    const char* senderId = "";
    JsonObject sender = event["sender"];
    if (!sender.isNull()) {
        JsonObject senderIdObj = sender["sender_id"];
        if (!senderIdObj.isNull()) senderId = senderIdObj["open_id"] | "";
    }
    if (strcmp(chatType, "p2p") == 0 && senderId[0]) routeId = senderId;

    Serial.printf("[FEISHU] Msg from %s: %.200s\n", routeId, cleaned);

    int next = (s_displayHead + 1) % FEISHU_DISPLAY_QUEUE_SIZE;
    if (next == s_displayTail) {
        free(s_displayQueue[s_displayTail]);
        s_displayQueue[s_displayTail] = nullptr;
        s_displayTail = (s_displayTail + 1) % FEISHU_DISPLAY_QUEUE_SIZE;
    }
    s_displayQueue[s_displayHead] = strdup(cleaned);
    s_displayHead = next;

    BusMessage msg = {};
    strlcpy(msg.channel, M5CLAW_CHAN_FEISHU, sizeof(msg.channel));
    strlcpy(msg.chat_id, routeId, sizeof(msg.chat_id));
    strlcpy(msg.msg_id, messageId, sizeof(msg.msg_id));
    msg.content = strdup(cleaned);
    if (msg.content) {
        if (!MessageBus::pushInbound(&msg)) free(msg.content);
    }
}

/* ── Process WS event JSON payload ─────────────────── */
static void processWsEventJson(const uint8_t* payload, size_t len) {
    JsonDocument doc;
    if (deserializeJson(doc, (const char*)payload, len)) return;
    JsonObject event = doc["event"];
    JsonObject header = doc["header"];
    if (!event.isNull() && !header.isNull()) {
        const char* eventType = header["event_type"] | "";
        if (strcmp(eventType, "im.message.receive_v1") == 0)
            handleMessageEvent(event);
    } else if (!event.isNull()) {
        handleMessageEvent(event);
    }
}

/* ── Handle WS binary frame ────────────────────────── */
static void handleWsFrame(const uint8_t* buf, size_t len) {
    WsFrame frame = {};
    if (!pbParseFrame(buf, len, &frame)) return;

    const char* type = frameHeaderValue(&frame, "type");
    if (frame.method == 0) {
        if (type && strcmp(type, "pong") == 0 && frame.payload && frame.payload_len > 0) {
            JsonDocument cfg;
            if (!deserializeJson(cfg, (const char*)frame.payload, frame.payload_len)) {
                int pi = cfg["PingInterval"] | 0;
                if (pi > 0) s_ping_interval_ms = pi * 1000;
            }
        }
        return;
    }
    if (!type || strcmp(type, "event") != 0) return;
    if (!frame.payload || frame.payload_len == 0) return;

    processWsEventJson(frame.payload, frame.payload_len);

    // ack
    char ack[32];
    int ackLen = snprintf(ack, sizeof(ack), "{\"code\":200}");
    WsFrame resp = frame;
    wsSendFrame(&resp, (const uint8_t*)ack, (size_t)ackLen);
}

/* ── WebSocket event callback ──────────────────────── */
static void wsEventCallback(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            s_ws_connected = true;
            Serial.println("[FEISHU] WS connected");
            break;
        case WStype_DISCONNECTED:
            s_ws_connected = false;
            Serial.println("[FEISHU] WS disconnected");
            break;
        case WStype_BIN:
            if (payload && length > 0) handleWsFrame(payload, length);
            break;
        default: break;
    }
}

/* ── Pull WS config from Feishu ────────────────────── */
static bool parseQueryParam(const char* url, const char* key, char* out, size_t outSize) {
    const char* q = strchr(url, '?');
    if (!q) return false;
    q++;
    size_t keyLen = strlen(key);
    while (*q) {
        const char* eq = strchr(q, '=');
        if (!eq) break;
        const char* amp = strchr(eq + 1, '&');
        size_t nameLen = (size_t)(eq - q);
        if (nameLen == keyLen && strncmp(q, key, keyLen) == 0) {
            size_t valLen = amp ? (size_t)(amp - (eq + 1)) : strlen(eq + 1);
            size_t n = (valLen < outSize - 1) ? valLen : outSize - 1;
            memcpy(out, eq + 1, n); out[n] = '\0';
            return true;
        }
        if (!amp) break;
        q = amp + 1;
    }
    return false;
}

static bool pullWsConfig() {
    JsonDocument doc;
    doc["AppID"] = s_app_id;
    doc["AppSecret"] = s_app_secret;
    String body;
    serializeJson(doc, body);

    String resp = httpsPost(M5CLAW_FEISHU_API_BASE, "/callback/ws/endpoint", body.c_str(), nullptr);
    if (resp.length() == 0) return false;

    int jsonStart = resp.indexOf('{');
    if (jsonStart < 0) return false;

    JsonDocument rdoc;
    if (deserializeJson(rdoc, resp.c_str() + jsonStart)) return false;
    int code = rdoc["code"] | -1;
    if (code != 0) { Serial.printf("[FEISHU] WS config error code=%d\n", code); return false; }

    const char* wsUrl = rdoc["data"]["URL"] | "";
    if (!wsUrl[0]) return false;

    // parse URL: wss://host:port/path?params
    String urlStr = wsUrl;
    if (urlStr.startsWith("wss://")) urlStr = urlStr.substring(6);

    int slashPos = urlStr.indexOf('/');
    String hostPart = (slashPos > 0) ? urlStr.substring(0, slashPos) : urlStr;
    String pathPart = (slashPos > 0) ? urlStr.substring(slashPos) : "/";

    int colonPos = hostPart.indexOf(':');
    if (colonPos > 0) {
        strlcpy(s_ws_host, hostPart.substring(0, colonPos).c_str(), sizeof(s_ws_host));
        s_ws_port = hostPart.substring(colonPos + 1).toInt();
    } else {
        strlcpy(s_ws_host, hostPart.c_str(), sizeof(s_ws_host));
        s_ws_port = 443;
    }
    strlcpy(s_ws_path, pathPart.c_str(), sizeof(s_ws_path));

    char sid[24] = {0};
    if (parseQueryParam(wsUrl, "service_id", sid, sizeof(sid)))
        s_service_id = atoi(sid);

    JsonObject ccfg = rdoc["data"]["ClientConfig"];
    if (!ccfg.isNull()) {
        int pi = ccfg["PingInterval"] | 0;
        if (pi > 0) s_ping_interval_ms = pi * 1000;
    }

    Serial.printf("[FEISHU] WS config: %s:%d%s service=%d\n", s_ws_host, s_ws_port, s_ws_path, s_service_id);
    return true;
}

/* ── Send ping frame ───────────────────────────────── */
static void sendPing() {
    WsFrame ping = {};
    ping.service = s_service_id;
    ping.method = 0;
    ping.header_count = 1;
    strlcpy(ping.headers[0].key, "type", sizeof(ping.headers[0].key));
    strlcpy(ping.headers[0].value, "ping", sizeof(ping.headers[0].value));
    wsSendFrame(&ping, nullptr, 0);
}

/* ── Stop request flag ─────────────────────────────── */
static volatile bool s_stopRequested = false;
static volatile bool s_stopped = false;

static void cleanupWs() {
    if (s_ws) {
        s_ws->disconnect();
        delete s_ws;
        s_ws = nullptr;
    }
    s_ws_connected = false;
}

/* ── Feishu WS task ────────────────────────────────── */
static void feishuTask(void* arg) {
    Serial.println("[FEISHU] Task started");
    int configFailCount = 0;

    while (true) {
        if (s_stopRequested) {
            cleanupWs();
            s_stopped = true;
            while (s_stopRequested) vTaskDelay(pdMS_TO_TICKS(200));
            s_stopped = false;
            configFailCount = 0;
            Serial.println("[FEISHU] Resumed");
        }

        if (WiFi.status() != WL_CONNECTED) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (!pullWsConfig()) {
            configFailCount++;
            int backoffSec;
            if      (configFailCount <= 2) backoffSec = 15;
            else if (configFailCount <= 5) backoffSec = 60;
            else                           backoffSec = 180;

            if (configFailCount == 5) {
                Serial.println("[FEISHU] Persistent failure, reconnecting WiFi");
                WiFi.disconnect(false);
                vTaskDelay(pdMS_TO_TICKS(2000));
                WiFi.reconnect();
                vTaskDelay(pdMS_TO_TICKS(10000));
            }

            Serial.printf("[FEISHU] Config failed (%d), retry in %ds\n", configFailCount, backoffSec);
            vTaskDelay(pdMS_TO_TICKS(backoffSec * 1000));
            continue;
        }
        configFailCount = 0;

        s_ws = new WebSocketsClient();
        if (!s_ws) { vTaskDelay(pdMS_TO_TICKS(5000)); continue; }
        s_ws->beginSSL(s_ws_host, s_ws_port, s_ws_path);
        s_ws->onEvent(wsEventCallback);
        s_ws->setReconnectInterval(5000);
        s_ws->enableHeartbeat(s_ping_interval_ms, 3000, 2);

        unsigned long lastPing = 0;
        bool wasConnected = false;

        unsigned long wsStartTime = millis();
        while (!s_stopRequested) {
            s_ws->loop();

            if (s_ws_connected) {
                wasConnected = true;
                unsigned long now = millis();
                if (now - lastPing >= (unsigned long)s_ping_interval_ms) {
                    sendPing();
                    lastPing = now;
                }
            } else if (wasConnected) {
                Serial.println("[FEISHU] WS disconnected, breaking out");
                break;
            } else if (millis() - wsStartTime > 45000) {
                Serial.println("[FEISHU] Initial connect timeout");
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        cleanupWs();
        if (!s_stopRequested) {
            Serial.println("[FEISHU] WS closed, reconnecting in 5s...");
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
}

/* ── Public API ────────────────────────────────────── */

void FeishuBot::init() {
    String appId = Config::getFeishuAppId();
    String appSecret = Config::getFeishuAppSecret();
    if (appId.length() > 0) strlcpy(s_app_id, appId.c_str(), sizeof(s_app_id));
    if (appSecret.length() > 0) strlcpy(s_app_secret, appSecret.c_str(), sizeof(s_app_secret));

    if (s_app_id[0] && s_app_secret[0])
        Serial.printf("[FEISHU] Credentials loaded (app_id=%.8s...)\n", s_app_id);
    else
        Serial.println("[FEISHU] No credentials configured");
}

void FeishuBot::start() {
    if (!s_app_id[0] || !s_app_secret[0]) {
        Serial.println("[FEISHU] Not configured, skipping");
        return;
    }
    if (s_task) return;

    xTaskCreatePinnedToCore(feishuTask, "feishu", M5CLAW_FEISHU_TASK_STACK,
                            nullptr, M5CLAW_FEISHU_TASK_PRIO, &s_task, M5CLAW_FEISHU_TASK_CORE);
    s_running = true;
    Serial.println("[FEISHU] Started");
}

bool FeishuBot::isRunning() { return s_running && s_ws_connected; }

void FeishuBot::stop() {
    if (!s_running || s_stopped) return;
    s_stopRequested = true;
    unsigned long t = millis();
    while (!s_stopped && millis() - t < 5000) delay(50);
    Serial.printf("[FEISHU] Paused (freed WS TLS), heap=%d largest=%d\n",
                  ESP.getFreeHeap(), (int)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

void FeishuBot::resume() {
    if (!s_running || !s_stopped) return;
    s_stopRequested = false;
    unsigned long t = millis();
    while (s_stopped && millis() - t < 1000) delay(50);
    Serial.println("[FEISHU] Resumed");
}

bool FeishuBot::sendMessage(const char* chatId, const char* text) {
    if (!s_app_id[0] || !s_app_secret[0]) return false;

    const char* idType = "chat_id";
    if (strncmp(chatId, "ou_", 3) == 0) idType = "open_id";

    char path[256];
    snprintf(path, sizeof(path), "%s?receive_id_type=%s", FS_SEND_PATH, idType);

    size_t textLen = strlen(text);
    size_t offset = 0;
    bool allOk = true;

    while (offset < textLen) {
        size_t chunk = textLen - offset;
        if (chunk > M5CLAW_FEISHU_MAX_MSG_LEN) chunk = M5CLAW_FEISHU_MAX_MSG_LEN;

        JsonDocument contentDoc;
        contentDoc["text"] = String(text + offset, chunk);
        String contentStr;
        serializeJson(contentDoc, contentStr);

        JsonDocument bodyDoc;
        bodyDoc["receive_id"] = chatId;
        bodyDoc["msg_type"] = "text";
        bodyDoc["content"] = contentStr;
        String bodyStr;
        serializeJson(bodyDoc, bodyStr);

        String resp = feishuApiCall(path, bodyStr.c_str());
        if (resp.length() > 0) {
            int jsonStart = resp.indexOf('{');
            if (jsonStart >= 0) {
                JsonDocument rdoc;
                if (!deserializeJson(rdoc, resp.c_str() + jsonStart)) {
                    int code = rdoc["code"] | -1;
                    if (code == 0) Serial.printf("[FEISHU] Sent to %s (%d bytes)\n", chatId, (int)chunk);
                    else { Serial.printf("[FEISHU] Send error code=%d\n", code); allOk = false; }
                }
            }
        } else { allOk = false; }

        offset += chunk;
    }
    return allOk;
}

bool FeishuBot::hasIncomingForDisplay() {
    return s_displayHead != s_displayTail;
}

char* FeishuBot::takeIncomingForDisplay() {
    if (s_displayHead == s_displayTail) return nullptr;
    char* text = s_displayQueue[s_displayTail];
    s_displayQueue[s_displayTail] = nullptr;
    s_displayTail = (s_displayTail + 1) % FEISHU_DISPLAY_QUEUE_SIZE;
    return text;
}

static String httpsDelete(const char* host, const char* path, const char* authHeader) {
    if (WiFi.status() != WL_CONNECTED) return "";
    WiFiClientSecure* client = new WiFiClientSecure();
    if (!client) return "";
    client->setInsecure();
    client->setTimeout(30000);
    IPAddress ip;
    if (!resolveHost(host, ip)) { delete client; return ""; }
    if (!client->connect(ip, 443)) { delete client; return ""; }
    client->printf("DELETE %s HTTP/1.1\r\n", path);
    client->printf("Host: %s\r\n", host);
    if (authHeader && authHeader[0]) client->printf("Authorization: %s\r\n", authHeader);
    client->println("Connection: close");
    client->println();
    int state = 0; bool headersDone = false;
    while (client->connected()) {
        if (!client->available()) { delay(1); continue; }
        char c = client->read();
        switch (state) {
            case 0: state = (c == '\r') ? 1 : (c == '\n') ? 2 : 0; break;
            case 1: state = (c == '\n') ? 2 : (c == '\r') ? 1 : 0; break;
            case 2: if (c == '\n') headersDone = true; state = (c == '\r') ? 3 : 0; break;
            case 3: if (c == '\n') headersDone = true; state = 0; break;
        }
        if (headersDone) break;
    }
    String resp; resp.reserve(512);
    while (client->connected() || client->available()) {
        if (client->available()) { char c = client->read(); if (resp.length() < 2048) resp += c; }
        else delay(1);
    }
    client->stop(); delete client;
    return resp;
}

static String feishuApiDelete(const char* path) {
    if (!refreshToken()) return "";
    char auth[600];
    snprintf(auth, sizeof(auth), "Bearer %s", s_tenant_token);
    return httpsDelete(M5CLAW_FEISHU_API_BASE, path, auth);
}

bool FeishuBot::addReaction(const char* messageId, const char* emojiType,
                            char* reactionIdOut, size_t reactionIdSize) {
    if (!messageId || !messageId[0]) return false;
    if (reactionIdOut) reactionIdOut[0] = '\0';

    const char* emoji = (emojiType && emojiType[0]) ? emojiType : "Typing";

    char path[256];
    snprintf(path, sizeof(path), "/open-apis/im/v1/messages/%s/reactions", messageId);

    JsonDocument bodyDoc;
    bodyDoc["reaction_type"]["emoji_type"] = emoji;
    String bodyStr;
    serializeJson(bodyDoc, bodyStr);

    String resp = feishuApiCall(path, bodyStr.c_str());
    if (resp.length() == 0) return false;

    int jsonStart = resp.indexOf('{');
    if (jsonStart < 0) return false;

    JsonDocument rdoc;
    if (deserializeJson(rdoc, resp.c_str() + jsonStart)) return false;
    int code = rdoc["code"] | -1;
    if (code != 0) {
        Serial.printf("[FEISHU] addReaction error code=%d\n", code);
        return false;
    }

    const char* rid = rdoc["data"]["reaction_id"] | "";
    if (rid[0] && reactionIdOut) {
        strlcpy(reactionIdOut, rid, reactionIdSize);
    }
    Serial.printf("[FEISHU] Reaction added: %s -> %s\n", emoji, rid);
    return true;
}

bool FeishuBot::removeReaction(const char* messageId, const char* reactionId) {
    if (!messageId || !messageId[0] || !reactionId || !reactionId[0]) return false;

    char path[384];
    snprintf(path, sizeof(path), "/open-apis/im/v1/messages/%s/reactions/%s", messageId, reactionId);

    String resp = feishuApiDelete(path);
    if (resp.length() == 0) return false;

    int jsonStart = resp.indexOf('{');
    if (jsonStart < 0) return false;

    JsonDocument rdoc;
    if (deserializeJson(rdoc, resp.c_str() + jsonStart)) return false;
    int code = rdoc["code"] | -1;
    if (code != 0) {
        Serial.printf("[FEISHU] removeReaction error code=%d\n", code);
        return false;
    }
    Serial.println("[FEISHU] Reaction removed");
    return true;
}
