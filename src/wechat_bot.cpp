#include "wechat_bot.h"
#include "m5claw_config.h"
#include "config.h"
#include "message_bus.h"
#include "media_utils.h"
#include "tls_utils.h"
#include <SPIFFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/base64.h>

/* ── State ─────────────────────────────────────────── */
static char s_token[512]       = {0};
static char s_api_host[128]    = {0};
static char s_uin_b64[24]      = {0};
static char s_update_buf[512]  = {0};
static char s_last_uid[128]   = {0};
static uint32_t s_msg_seq      = 0;

struct WechatContextEntry {
    char userId[128];
    char contextToken[256];
};

static WechatContextEntry s_ctxCache[6] = {};

static TaskHandle_t s_task     = nullptr;
static bool s_running          = false;
static bool s_polling          = false;

/* ── Display queue ─────────────────────────────────── */
#define WX_DISPLAY_QUEUE_SIZE 6
static char* s_displayQueue[WX_DISPLAY_QUEUE_SIZE] = {};
static volatile int s_displayHead = 0;
static volatile int s_displayTail = 0;

/* ── Deduplication ─────────────────────────────────── */
static uint64_t s_seen_keys[M5CLAW_WECHAT_DEDUP_SIZE] = {0};
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

static bool dedupCheck(const char* key) {
    uint64_t k = fnv1a64(key);
    for (size_t i = 0; i < M5CLAW_WECHAT_DEDUP_SIZE; i++) {
        if (s_seen_keys[i] == k) return true;
    }
    s_seen_keys[s_seen_idx] = k;
    s_seen_idx = (s_seen_idx + 1) % M5CLAW_WECHAT_DEDUP_SIZE;
    return false;
}

/* ── Generate random UIN (base64 of random uint32) ── */
static void generateUin() {
    uint32_t r = esp_random();
    size_t olen = 0;
    mbedtls_base64_encode((unsigned char*)s_uin_b64, sizeof(s_uin_b64),
                          &olen, (const unsigned char*)&r, sizeof(r));
    s_uin_b64[olen] = '\0';
}

static void rememberContextToken(const char* userId, const char* contextToken) {
    if (!userId || !userId[0] || !contextToken || !contextToken[0]) return;
    int freeIdx = -1;
    for (int i = 0; i < (int)(sizeof(s_ctxCache) / sizeof(s_ctxCache[0])); i++) {
        if (strcmp(s_ctxCache[i].userId, userId) == 0) {
            strlcpy(s_ctxCache[i].contextToken, contextToken, sizeof(s_ctxCache[i].contextToken));
            return;
        }
        if (freeIdx < 0 && s_ctxCache[i].userId[0] == '\0') freeIdx = i;
    }
    int idx = (freeIdx >= 0) ? freeIdx : (esp_random() % (sizeof(s_ctxCache) / sizeof(s_ctxCache[0])));
    strlcpy(s_ctxCache[idx].userId, userId, sizeof(s_ctxCache[idx].userId));
    strlcpy(s_ctxCache[idx].contextToken, contextToken, sizeof(s_ctxCache[idx].contextToken));
}

static const char* findContextToken(const char* userId) {
    if (!userId || !userId[0]) return "";
    for (const auto& entry : s_ctxCache) {
        if (strcmp(entry.userId, userId) == 0) return entry.contextToken;
    }
    return "";
}

/* ── DNS + HTTPS helper ────────────────────────────── */
static bool resolveHost(const char* host, IPAddress& ip) {
    if (WiFi.status() != WL_CONNECTED) return false;
    for (int i = 1; i <= 2; i++) {
        if (WiFi.hostByName(host, ip)) return true;
        delay(100);
    }
    return false;
}

static String httpsPostRaw(const char* host, const char* path, const char* body,
                           const char* authToken, int timeoutMs) {
    if (WiFi.status() != WL_CONNECTED) return "";

    WiFiClientSecure* client = new WiFiClientSecure();
    if (!client) { Serial.println("[WECHAT] OOM for TLS client"); return ""; }
    TlsConfig::configureClient(*client, timeoutMs);

    IPAddress ip;
    if (!resolveHost(host, ip)) { Serial.printf("[WECHAT] DNS failed: %s\n", host); delete client; return ""; }
    if (!client->connect(host, 443)) { Serial.println("[WECHAT] Connect failed"); delete client; return ""; }

    client->printf("POST %s HTTP/1.0\r\n", path);
    client->printf("Host: %s\r\n", host);
    client->println("Content-Type: application/json");
    if (authToken && authToken[0]) {
        client->println("AuthorizationType: ilink_bot_token");
        client->printf("Authorization: Bearer %s\r\n", authToken);
        client->printf("X-WECHAT-UIN: %s\r\n", s_uin_b64);
    }
    client->printf("Content-Length: %d\r\n", (int)strlen(body));
    client->println();
    client->print(body);

    unsigned long deadline = millis() + timeoutMs;
    int state = 0;
    bool headersDone = false;
    while (client->connected() && millis() < deadline) {
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

    String resp;
    resp.reserve(4096);
    while (millis() < deadline) {
        if (client->available()) {
            char c = client->read();
            if (resp.length() < 8192) resp += c;
        } else if (!client->connected()) break;
        else delay(1);
    }
    client->stop();
    delete client;
    return resp;
}

static String httpsPost(const char* host, const char* path, const char* body) {
    return httpsPostRaw(host, path, body, s_token, 45000);
}

static String httpsGet(const char* host, const char* path, int timeoutMs = 15000) {
    if (WiFi.status() != WL_CONNECTED) return "";

    WiFiClientSecure* client = new WiFiClientSecure();
    if (!client) return "";
    TlsConfig::configureClient(*client, timeoutMs);

    IPAddress ip;
    if (!resolveHost(host, ip)) { Serial.printf("[WECHAT] DNS failed: %s\n", host); delete client; return ""; }
    if (!client->connect(host, 443)) { Serial.println("[WECHAT] GET connect failed"); delete client; return ""; }

    client->printf("GET %s HTTP/1.0\r\n", path);
    client->printf("Host: %s\r\n", host);
    client->println("iLink-App-ClientVersion: 1");
    client->println();

    unsigned long deadline = millis() + timeoutMs;
    int st = 0; bool headersDone = false;
    while (client->connected() && millis() < deadline) {
        if (!client->available()) { delay(1); continue; }
        char c = client->read();
        switch (st) {
            case 0: st = (c == '\r') ? 1 : (c == '\n') ? 2 : 0; break;
            case 1: st = (c == '\n') ? 2 : (c == '\r') ? 1 : 0; break;
            case 2: if (c == '\n') headersDone = true; st = (c == '\r') ? 3 : 0; break;
            case 3: if (c == '\n') headersDone = true; st = 0; break;
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

/* ── Find JSON start in response (skip chunked headers) */
static int findJsonStart(const String& resp) {
    int i = resp.indexOf('{');
    return (i >= 0) ? i : -1;
}

static void enqueueDisplayText(const char* text) {
    if (!text || !text[0]) return;
    int next = (s_displayHead + 1) % WX_DISPLAY_QUEUE_SIZE;
    if (next == s_displayTail) {
        free(s_displayQueue[s_displayTail]);
        s_displayQueue[s_displayTail] = nullptr;
        s_displayTail = (s_displayTail + 1) % WX_DISPLAY_QUEUE_SIZE;
    }
    s_displayQueue[s_displayHead] = strdup(text);
    s_displayHead = next;
}

static const char* findStringField(JsonObject obj, const char* const* fields, size_t count) {
    for (size_t i = 0; i < count; i++) {
        const char* value = obj[fields[i]] | "";
        if (value[0]) return value;
    }
    return "";
}

static bool extractImageCandidate(JsonObject item, String& imageUrl, String& mimeType) {
    static const char* const directFields[] = {
        "image_url", "download_url", "url", "cdn_url", "origin_url", "thumb_url", "pic_url"
    };
    static const char* const mimeFields[] = {
        "mime_type", "content_type", "file_type"
    };
    static const char* const objectFields[] = {
        "image_item", "pic_item", "media_item", "file_item"
    };

    const char* direct = findStringField(item, directFields, sizeof(directFields) / sizeof(directFields[0]));
    if (direct[0]) {
        imageUrl = direct;
        mimeType = findStringField(item, mimeFields, sizeof(mimeFields) / sizeof(mimeFields[0]));
        return true;
    }

    for (size_t i = 0; i < sizeof(objectFields) / sizeof(objectFields[0]); i++) {
        JsonObject nested = item[objectFields[i]];
        if (nested.isNull()) continue;
        const char* nestedUrl = findStringField(nested, directFields, sizeof(directFields) / sizeof(directFields[0]));
        if (nestedUrl[0]) {
            imageUrl = nestedUrl;
            mimeType = findStringField(nested, mimeFields, sizeof(mimeFields) / sizeof(mimeFields[0]));
            return true;
        }
    }

    return false;
}

/* ── Handle incoming message ─────────────────────────── */
static void handleIncomingMessage(JsonObject msg) {
    int msgType = msg["message_type"] | 0;
    if (msgType != 1) return;

    const char* fromUserId = msg["from_user_id"] | "";
    const char* contextToken = msg["context_token"] | "";
    int messageId = msg["message_id"] | 0;

    if (!fromUserId[0]) return;

    rememberContextToken(fromUserId, contextToken);
    strlcpy(s_last_uid, fromUserId, sizeof(s_last_uid));

    char dedupKey[64];
    snprintf(dedupKey, sizeof(dedupKey), "%s_%d", fromUserId, messageId);
    if (messageId && dedupCheck(dedupKey)) return;

    JsonArray items = msg["item_list"];
    if (items.isNull()) return;

    String messageText;
    char imagePath[96] = {0};
    char imageMime[32] = {0};
    bool hasImage = false;

    for (JsonVariant itemVar : items) {
        JsonObject item = itemVar.as<JsonObject>();
        int type = item["type"] | 0;
        if (type == 1) {
            JsonObject textItem = item["text_item"];
            if (textItem.isNull()) continue;
            const char* text = textItem["text"] | "";
            if (!text[0]) continue;
            if (messageText.length() > 0) messageText += "\n";
            messageText += text;
            continue;
        }

        if (!hasImage) {
            String imageUrl;
            String mimeType;
            if (extractImageCandidate(item, imageUrl, mimeType) && imageUrl.length() > 0) {
                snprintf(imagePath, sizeof(imagePath), "/tmp_wx_%d.bin",
                         messageId ? messageId : (int)millis());
                if (MediaUtils::downloadHttpsUrlToFile(imageUrl.c_str(), imagePath,
                                                       imageMime, sizeof(imageMime),
                                                       M5CLAW_WECHAT_MEDIA_MAX_BYTES)) {
                    if (!imageMime[0] && mimeType.length() > 0) {
                        strlcpy(imageMime, mimeType.c_str(), sizeof(imageMime));
                    }
                    hasImage = true;
                    continue;
                }
                Serial.printf("[WECHAT] Image download failed: %s\n", imageUrl.c_str());
            }
        }

        String raw;
        serializeJson(item, raw);
        if (raw.length() > 180) raw = raw.substring(0, 180);
        Serial.printf("[WECHAT] Unsupported item type=%d raw=%s\n", type, raw.c_str());
    }

    if (messageText.length() == 0 && !hasImage) return;

    String preview = messageText;
    if (hasImage) {
        if (preview.length() > 0) preview += " ";
        preview += "[image]";
    }
    if (preview.length() == 0) preview = "[image]";

    Serial.printf("[WECHAT] Msg from %s: %.200s\n", fromUserId, preview.c_str());
    enqueueDisplayText(preview.c_str());

    BusMessage busMsg = {};
    strlcpy(busMsg.channel, M5CLAW_CHAN_WECHAT, sizeof(busMsg.channel));
    strlcpy(busMsg.chat_id, fromUserId, sizeof(busMsg.chat_id));
    snprintf(busMsg.msg_id, sizeof(busMsg.msg_id), "%d", messageId);
    if (messageText.length() > 0) busMsg.content = strdup(messageText.c_str());
    if (hasImage) {
        strlcpy(busMsg.media_path, imagePath, sizeof(busMsg.media_path));
        strlcpy(busMsg.media_mime, imageMime[0] ? imageMime : "image/jpeg", sizeof(busMsg.media_mime));
        busMsg.media_kind = BUS_MEDIA_IMAGE;
    }

    if (busMsg.content || busMsg.media_kind != BUS_MEDIA_NONE) {
        if (!MessageBus::pushInbound(&busMsg)) {
            free(busMsg.content);
            if (busMsg.media_path[0]) SPIFFS.remove(busMsg.media_path);
        }
    }
}

/* ── Stop/resume ──────────────────────────────────── */
static volatile bool s_stopRequested = false;
static volatile bool s_stopped = false;

/* ── Polling task ─────────────────────────────────── */
static void wechatTask(void* arg) {
    Serial.println("[WECHAT] Task started");
    int failCount = 0;

    while (true) {
        if (s_stopRequested) {
            s_stopped = true;
            s_polling = false;
            while (s_stopRequested) vTaskDelay(pdMS_TO_TICKS(200));
            s_stopped = false;
            failCount = 0;
            Serial.println("[WECHAT] Resumed");
        }

        if (WiFi.status() != WL_CONNECTED || !s_token[0] || !s_api_host[0]) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        JsonDocument reqDoc;
        reqDoc["get_updates_buf"] = s_update_buf;
        reqDoc["base_info"]["channel_version"] = "1.0.2";
        String body;
        serializeJson(reqDoc, body);

        s_polling = true;
        String resp = httpsPost(s_api_host, M5CLAW_WECHAT_API_GETUPDATES, body.c_str());
        s_polling = false;

        if (resp.length() == 0) {
            // Normal for long-poll timeout / connection reset
            if (failCount < 3) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            } else {
                int backoffSec = (failCount <= 8) ? 15 : 60;
                Serial.printf("[WECHAT] Poll empty (%d), retry in %ds\n", failCount, backoffSec);
                vTaskDelay(pdMS_TO_TICKS(backoffSec * 1000));
            }
            failCount++;
            continue;
        }

        int jsonStart = findJsonStart(resp);
        if (jsonStart < 0) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        JsonDocument respDoc;
        if (deserializeJson(respDoc, resp.c_str() + jsonStart)) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        int errcode = respDoc["errcode"] | 0;
        if (errcode == -14) {
            Serial.println("[WECHAT] Session expired, token may be invalid");
            failCount++;
            vTaskDelay(pdMS_TO_TICKS(30000));
            continue;
        }

        int ret = respDoc["ret"] | 0;
        if (ret != 0) {
            failCount++;
            Serial.printf("[WECHAT] getUpdates ret=%d errcode=%d\n", ret, errcode);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        failCount = 0;

        const char* newBuf = respDoc["get_updates_buf"] | "";
        if (newBuf[0]) {
            strlcpy(s_update_buf, newBuf, sizeof(s_update_buf));
        }

        JsonArray msgs = respDoc["msgs"];
        if (!msgs.isNull()) {
            for (JsonVariant mv : msgs) {
                JsonObject msg = mv.as<JsonObject>();
                handleIncomingMessage(msg);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ── Public API ────────────────────────────────────── */

void WechatBot::init() {
    String token = Config::getWechatToken();
    String host  = Config::getWechatApiHost();
    if (token.length() > 0) strlcpy(s_token, token.c_str(), sizeof(s_token));
    if (host.length() > 0)  strlcpy(s_api_host, host.c_str(), sizeof(s_api_host));

    generateUin();

    if (s_token[0] && s_api_host[0])
        Serial.printf("[WECHAT] Credentials loaded (host=%s)\n", s_api_host);
    else
        Serial.println("[WECHAT] Not configured");
}

void WechatBot::start() {
    if (!s_token[0] || !s_api_host[0]) {
        Serial.println("[WECHAT] Not configured, skipping");
        return;
    }
    if (s_task) return;

    s_stopRequested = false;
    s_stopped = false;
    BaseType_t ok = xTaskCreatePinnedToCore(wechatTask, "wechat", M5CLAW_WECHAT_TASK_STACK,
                                            nullptr, M5CLAW_WECHAT_TASK_PRIO, &s_task, M5CLAW_WECHAT_TASK_CORE);
    if (ok != pdPASS || !s_task) {
        s_task = nullptr;
        s_running = false;
        Serial.println("[WECHAT] Failed to start task");
        return;
    }
    s_running = true;
    Serial.println("[WECHAT] Started");
}

bool WechatBot::isRunning() { return s_running; }
bool WechatBot::isPaired()  { return s_token[0] != '\0' && s_api_host[0] != '\0'; }

const char* WechatBot::getApiHost() { return s_api_host; }
const char* WechatBot::getLastUserId() { return s_last_uid; }

void WechatBot::stop() {
    if (!s_running || s_stopped) return;
    s_stopRequested = true;
    unsigned long t = millis();
    while (!s_stopped && millis() - t < 5000) delay(50);
    Serial.printf("[WECHAT] Paused, heap=%d largest=%d\n",
                  ESP.getFreeHeap(), (int)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

void WechatBot::requestPause() {
    if (!s_running) return;
    s_stopRequested = true;
}

void WechatBot::resume() {
    if (!s_running) return;
    s_stopRequested = false;
    if (!s_stopped) return;
    unsigned long t = millis();
    while (s_stopped && millis() - t < 1000) delay(50);
    Serial.println("[WECHAT] Resumed");
}

bool WechatBot::sendMessage(const char* userId, const char* text) {
    if (!s_token[0] || !s_api_host[0] || !userId || !userId[0] || !text) return false;

    size_t textLen = strlen(text);
    size_t offset = 0;
    bool allOk = true;

    if (textLen == 0) return true;

    while (offset < textLen) {
        size_t chunk = textLen - offset;
        if (chunk > M5CLAW_WECHAT_MAX_MSG_LEN) chunk = M5CLAW_WECHAT_MAX_MSG_LEN;

        char clientId[32];
        snprintf(clientId, sizeof(clientId), "m5claw-%lu-%u", millis(), ++s_msg_seq);

        JsonDocument doc;
        JsonObject msg = doc["msg"].to<JsonObject>();
        msg["from_user_id"] = "";
        msg["to_user_id"] = userId;
        msg["client_id"] = clientId;
        msg["message_type"] = 2;
        msg["message_state"] = 2;
        const char* ctx = findContextToken(userId);
        if (ctx[0]) msg["context_token"] = ctx;
        JsonArray itemList = msg["item_list"].to<JsonArray>();
        JsonObject item = itemList.add<JsonObject>();
        item["type"] = 1;
        JsonObject textItem = item["text_item"].to<JsonObject>();
        textItem["text"] = String(text + offset, chunk);
        doc["base_info"]["channel_version"] = "1.0.2";

        String body;
        serializeJson(doc, body);

        String resp = httpsPost(s_api_host, M5CLAW_WECHAT_API_SENDMSG, body.c_str());
        if (resp.length() > 0) {
            int js = findJsonStart(resp);
            if (js >= 0) {
                JsonDocument rdoc;
                if (!deserializeJson(rdoc, resp.c_str() + js)) {
                    int ret = rdoc["ret"] | 0;
                    if (ret == 0) Serial.printf("[WECHAT] Sent to %s (%d bytes)\n", userId, (int)chunk);
                    else { Serial.printf("[WECHAT] Send err ret=%d\n", ret); allOk = false; }
                }
            }
        } else { allOk = false; }

        offset += chunk;
    }
    return allOk;
}

bool WechatBot::sendTyping(const char* userId, int status) {
    if (!s_token[0] || !s_api_host[0] || !userId || !userId[0]) return false;

    JsonDocument doc;
    doc["ilink_user_id"] = userId;
    doc["typing_ticket"] = "";
    doc["status"] = status;
    doc["base_info"]["channel_version"] = "1.0.2";

    String body;
    serializeJson(doc, body);

    String resp = httpsPost(s_api_host, M5CLAW_WECHAT_API_TYPING, body.c_str());
    return resp.length() > 0;
}

bool WechatBot::hasIncomingForDisplay() {
    return s_displayHead != s_displayTail;
}

char* WechatBot::takeIncomingForDisplay() {
    if (s_displayHead == s_displayTail) return nullptr;
    char* text = s_displayQueue[s_displayTail];
    s_displayQueue[s_displayTail] = nullptr;
    s_displayTail = (s_displayTail + 1) % WX_DISPLAY_QUEUE_SIZE;
    return text;
}

/* ── Pairing (iLink QR login) ─────────────────────── */
static WechatBot::PairState s_pairState = WechatBot::PAIR_IDLE;
static char s_pairQrUrl[512]  = {0};
static char s_pairQrCode[256] = {0};
static char s_pairHost[128]   = {0};

WechatBot::PairState WechatBot::getPairState() { return s_pairState; }

const char* WechatBot::startPairing() {
    s_pairQrUrl[0] = '\0';
    s_pairQrCode[0] = '\0';

    const char* host = s_api_host[0] ? s_api_host : M5CLAW_WECHAT_DEFAULT_HOST;
    strlcpy(s_pairHost, host, sizeof(s_pairHost));

    Serial.printf("[WECHAT] Fetching QR from %s%s\n", host, M5CLAW_WECHAT_QR_PATH);
    String resp = httpsGet(host, M5CLAW_WECHAT_QR_PATH, 15000);

    if (resp.length() == 0) {
        Serial.println("[WECHAT] QR fetch failed (no response)");
        s_pairState = PAIR_FAIL;
        return nullptr;
    }

    int js = findJsonStart(resp);
    if (js < 0) { Serial.println("[WECHAT] QR fetch: no JSON"); s_pairState = PAIR_FAIL; return nullptr; }

    JsonDocument rdoc;
    if (deserializeJson(rdoc, resp.c_str() + js)) { s_pairState = PAIR_FAIL; return nullptr; }

    const char* qrImgContent = rdoc["qrcode_img_content"] | (const char*)nullptr;
    const char* qrCode       = rdoc["qrcode"] | "";
    if (!qrImgContent || !qrImgContent[0]) {
        Serial.println("[WECHAT] QR response missing qrcode_img_content");
        s_pairState = PAIR_FAIL;
        return nullptr;
    }

    strlcpy(s_pairQrUrl, qrImgContent, sizeof(s_pairQrUrl));
    strlcpy(s_pairQrCode, qrCode, sizeof(s_pairQrCode));
    s_pairState = PAIR_WAITING;

    Serial.printf("[WECHAT] QR ready, scan with WeChat\n");
    return s_pairQrUrl;
}

bool WechatBot::pollPairing() {
    if (s_pairState != PAIR_WAITING || !s_pairQrCode[0]) return false;

    char path[384];
    snprintf(path, sizeof(path), "%s%s", M5CLAW_WECHAT_QR_STATUS_PATH, s_pairQrCode);

    String resp = httpsGet(s_pairHost, path, 40000);
    if (resp.length() == 0) return false;

    int js = findJsonStart(resp);
    if (js < 0) return false;

    JsonDocument rdoc;
    if (deserializeJson(rdoc, resp.c_str() + js)) return false;

    const char* status = rdoc["status"] | "wait";
    Serial.printf("[WECHAT] QR status: %s\n", status);

    if (strcmp(status, "expired") == 0) {
        Serial.println("[WECHAT] QR expired, need refresh");
        s_pairState = PAIR_FAIL;
        return false;
    }

    if (strcmp(status, "confirmed") == 0) {
        const char* botToken  = rdoc["bot_token"] | "";
        const char* baseUrl   = rdoc["baseurl"] | "";
        const char* botId     = rdoc["ilink_bot_id"] | "";

        if (!botToken[0]) {
            Serial.println("[WECHAT] Confirmed but no bot_token!");
            s_pairState = PAIR_FAIL;
            return false;
        }

        strlcpy(s_token, botToken, sizeof(s_token));

        if (baseUrl[0]) {
            String host = baseUrl;
            if (host.startsWith("https://")) host = host.substring(8);
            if (host.startsWith("http://"))  host = host.substring(7);
            int slash = host.indexOf('/');
            if (slash > 0) host = host.substring(0, slash);
            strlcpy(s_api_host, host.c_str(), sizeof(s_api_host));
        } else {
            strlcpy(s_api_host, s_pairHost, sizeof(s_api_host));
        }

        Config::setWechatToken(s_token);
        Config::setWechatApiHost(s_api_host);
        Config::save();
        generateUin();

        s_pairState = PAIR_OK;
        Serial.printf("[WECHAT] Paired! botId=%s host=%s\n", botId, s_api_host);
        return true;
    }

    return false;
}

void WechatBot::cancelPairing() {
    s_pairState = PAIR_IDLE;
    s_pairQrUrl[0] = '\0';
    s_pairQrCode[0] = '\0';
}
