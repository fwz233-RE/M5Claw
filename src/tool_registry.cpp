#include "tool_registry.h"
#include "memory_store.h"
#include "m5claw_config.h"
#include "config.h"
#include "cron_service.h"
#include "feishu_bot.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <time.h>

/* ── PSRAM allocation helper ───────────────────────── */
static void* allocPsram(size_t size) {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    return p;
}

/* ── get_current_time ──────────────────────────────── */
static bool tool_get_time(const char* input, char* output, size_t sz) {
    struct tm ti;
    if (getLocalTime(&ti, 0)) {
        snprintf(output, sz, "%04d-%02d-%02d %02d:%02d:%02d",
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                 ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        strlcpy(output, "Time not available", sz);
    }
    return true;
}

/* ── read_file ─────────────────────────────────────── */
static String tryReadFile(const char* path) {
    String content = MemoryStore::readFile(path);
    if (content.length() > 0) return content;
    static const char* prefixes[] = { "/config/", "/memory/", "/skills/", "/" };
    const char* basename = strrchr(path, '/');
    basename = basename ? basename + 1 : path;
    for (int i = 0; i < 4; i++) {
        char alt[128];
        snprintf(alt, sizeof(alt), "%s%s", prefixes[i], basename);
        if (strcmp(alt, path) == 0) continue;
        content = MemoryStore::readFile(alt);
        if (content.length() > 0) return content;
    }
    return "";
}

static bool tool_read_file(const char* input, char* output, size_t sz) {
    JsonDocument doc;
    if (deserializeJson(doc, input)) { strlcpy(output, "Invalid JSON", sz); return false; }
    const char* path = doc["path"] | "";
    if (!path[0]) { strlcpy(output, "Missing path", sz); return false; }
    char fullPath[128];
    if (path[0] == '/') snprintf(fullPath, sizeof(fullPath), "%s", path);
    else                snprintf(fullPath, sizeof(fullPath), "/%s", path);
    String content = tryReadFile(fullPath);
    if (content.length() == 0) {
        snprintf(output, sz, "File not found: %s. Use list_dir to see available files.", fullPath);
    } else {
        strlcpy(output, content.c_str(), sz);
    }
    return true;
}

/* ── write_file ────────────────────────────────────── */
static bool tool_write_file(const char* input, char* output, size_t sz) {
    JsonDocument doc;
    if (deserializeJson(doc, input)) { strlcpy(output, "Invalid JSON", sz); return false; }
    const char* path = doc["path"] | "";
    const char* content = doc["content"] | "";
    if (!path[0]) { strlcpy(output, "Missing path", sz); return false; }
    char fullPath[128];
    if (path[0] == '/') snprintf(fullPath, sizeof(fullPath), "%s", path);
    else                snprintf(fullPath, sizeof(fullPath), "/%s", path);
    if (MemoryStore::writeFile(fullPath, content)) {
        snprintf(output, sz, "Written %d bytes to %s", (int)strlen(content), fullPath);
    } else {
        snprintf(output, sz, "Failed to write %s", fullPath);
    }
    return true;
}

/* ── edit_file (find and replace) ──────────────────── */
static bool tool_edit_file(const char* input, char* output, size_t sz) {
    JsonDocument doc;
    if (deserializeJson(doc, input)) { strlcpy(output, "Invalid JSON", sz); return false; }
    const char* path = doc["path"] | "";
    const char* oldStr = doc["old_string"] | "";
    const char* newStr = doc["new_string"] | "";
    if (!path[0] || !oldStr[0]) { strlcpy(output, "Missing path or old_string", sz); return false; }

    char fullPath[128];
    if (path[0] == '/') snprintf(fullPath, sizeof(fullPath), "%s", path);
    else                snprintf(fullPath, sizeof(fullPath), "/%s", path);
    String content = MemoryStore::readFile(fullPath);
    if (content.length() == 0) { snprintf(output, sz, "File not found: %s", fullPath); return false; }

    int idx = content.indexOf(oldStr);
    if (idx < 0) { snprintf(output, sz, "old_string not found in %s", fullPath); return false; }

    String result = content.substring(0, idx) + String(newStr) + content.substring(idx + strlen(oldStr));
    if (MemoryStore::writeFile(fullPath, result.c_str())) {
        snprintf(output, sz, "Edited %s: replaced %d chars", fullPath, (int)strlen(oldStr));
    } else {
        snprintf(output, sz, "Failed to write %s", fullPath);
    }
    return true;
}

/* ── list_dir ──────────────────────────────────────── */
static bool tool_list_dir(const char* input, char* output, size_t sz) {
    JsonDocument doc;
    deserializeJson(doc, input);
    const char* prefix = doc["prefix"] | "";

    File root = SPIFFS.open("/");
    if (!root) { strlcpy(output, "Cannot open SPIFFS root", sz); return false; }

    size_t off = 0;
    File f = root.openNextFile();
    int count = 0;
    while (f && off < sz - 60) {
        const char* name = f.name();
        if (!prefix[0] || strstr(name, prefix) != nullptr) {
            int w = snprintf(output + off, sz - off, "%s (%d bytes)\n", name, (int)f.size());
            if (w > 0) off += w;
            count++;
        }
        f = root.openNextFile();
    }
    if (count == 0) {
        snprintf(output, sz, "No files found%s%s", prefix[0] ? " matching " : "", prefix);
    }
    return true;
}

/* ── chunked transfer decoding ─────────────────────── */
static void decode_chunked_search(char* buf, int* len) {
    if (!buf || !len || *len <= 0) return;
    int i = 0;
    while (i < *len && (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' || buf[i] == '\n')) i++;
    if (i < *len && (buf[i] == '{' || buf[i] == '[')) return;

    char* src = buf;
    char* dst = buf;
    char* end = buf + *len;
    while (src < end) {
        char* line_end = strstr(src, "\r\n");
        if (!line_end) break;
        unsigned long chunk_size = strtoul(src, nullptr, 16);
        if (chunk_size == 0) break;
        src = line_end + 2;
        if (src + chunk_size > end) break;
        memmove(dst, src, chunk_size);
        dst += chunk_size;
        src += chunk_size;
        if (src + 2 <= end && src[0] == '\r' && src[1] == '\n') src += 2;
    }
    *len = (int)(dst - buf);
    buf[*len] = '\0';
}

/* ── GLM web_search (Zhipu AI) ─────────────────────── */
static bool tool_web_search(const char* input, char* output, size_t sz) {
    JsonDocument doc;
    if (deserializeJson(doc, input)) { strlcpy(output, "Invalid JSON", sz); return false; }
    const char* query = doc["query"] | "";
    if (!query[0]) { strlcpy(output, "Missing query", sz); return false; }

    String glmKey = Config::getGlmSearchKey();
    if (glmKey.length() == 0) {
        strlcpy(output, "Web search not configured. Set glm_search_key in config.", sz);
        return true;
    }

    Serial.printf("[SEARCH] Query: %s\n", query);

    JsonDocument reqDoc;
    reqDoc["search_engine"] = M5CLAW_SEARCH_ENGINE;
    reqDoc["search_query"] = query;
    reqDoc["count"] = M5CLAW_SEARCH_MAX_RESULTS;
    reqDoc["search_recency_filter"] = "oneWeek";
    reqDoc["content_size"] = "low";
    String reqBody;
    serializeJson(reqDoc, reqBody);

    WiFiClientSecure* client = new WiFiClientSecure();
    if (!client) { strlcpy(output, "Out of memory for TLS", sz); return true; }
    client->setInsecure();
    client->setTimeout(30000);

    if (WiFi.status() != WL_CONNECTED) { delete client; strlcpy(output, "WiFi not connected", sz); return true; }

    IPAddress ip;
    bool resolved = false;
    for (int i = 0; i < 3; i++) {
        if (WiFi.hostByName(M5CLAW_SEARCH_HOST, ip)) { resolved = true; break; }
        delay(200);
    }
    if (!resolved) { delete client; strlcpy(output, "DNS failed for search API", sz); return true; }
    Serial.printf("[SEARCH] DNS %s -> %s\n", M5CLAW_SEARCH_HOST, ip.toString().c_str());

    if (!client->connect(ip, 443)) { delete client; strlcpy(output, "Connection failed to search API", sz); return true; }

    client->printf("POST %s HTTP/1.1\r\n", M5CLAW_SEARCH_PATH);
    client->printf("Host: %s\r\n", M5CLAW_SEARCH_HOST);
    client->println("Content-Type: application/json");
    client->println("Accept: application/json");
    client->printf("Authorization: Bearer %s\r\n", glmKey.c_str());
    client->printf("Content-Length: %d\r\n", reqBody.length());
    client->println("Connection: close");
    client->println();
    client->print(reqBody);

    int state = 0;
    bool headersDone = false;
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

    const size_t SEARCH_HEAP_RESERVE = 8 * 1024;
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    size_t bufSize = (largest > SEARCH_HEAP_RESERVE) ? largest - SEARCH_HEAP_RESERVE : 0;
    if (bufSize > 48 * 1024) bufSize = 48 * 1024;

    char* respBuf = nullptr;
    if (bufSize >= 6 * 1024) {
        respBuf = (char*)heap_caps_malloc(bufSize, MALLOC_CAP_8BIT);
    }
    if (!respBuf) { bufSize = 32 * 1024; respBuf = (char*)heap_caps_malloc(bufSize, MALLOC_CAP_8BIT); }
    if (!respBuf) { bufSize = 16 * 1024; respBuf = (char*)heap_caps_malloc(bufSize, MALLOC_CAP_8BIT); }
    if (!respBuf) { bufSize =  8 * 1024; respBuf = (char*)heap_caps_malloc(bufSize, MALLOC_CAP_8BIT); }
    if (!respBuf) { client->stop(); delete client; strlcpy(output, "Out of memory for search", sz); return true; }
    Serial.printf("[SEARCH] Buffer %dKB (heap=%d)\n", (int)(bufSize / 1024), ESP.getFreeHeap());

    int respLen = 0;
    while (respLen < (int)bufSize - 1) {
        if (client->available()) {
            respBuf[respLen++] = client->read();
        } else if (!client->connected()) break;
        else delay(1);
    }
    respBuf[respLen] = '\0';
    client->stop();
    delete client;

    decode_chunked_search(respBuf, &respLen);
    Serial.printf("[SEARCH] Response %d / %u bytes\n", respLen, (unsigned)bufSize);

    char* jsonStart = strchr(respBuf, '{');
    if (!jsonStart) {
        Serial.printf("[SEARCH] No JSON in response: %.120s\n", respBuf);
        heap_caps_free(respBuf);
        strlcpy(output, "Invalid search response", sz);
        return true;
    }

    JsonDocument respDoc;
    DeserializationError err = deserializeJson(respDoc, jsonStart);

    if (err == DeserializationError::IncompleteInput || err == DeserializationError::InvalidInput) {
        char* lastBound = nullptr;
        char* p = jsonStart;
        while ((p = strstr(p, "},{\"")) != nullptr) {
            lastBound = p;
            p++;
        }
        if (lastBound) {
            lastBound[1] = ']';
            lastBound[2] = '}';
            lastBound[3] = '\0';
            respDoc.clear();
            err = deserializeJson(respDoc, jsonStart);
            if (!err) {
                Serial.println("[SEARCH] Repaired truncated JSON");
            }
        }
    }

    heap_caps_free(respBuf);
    respBuf = nullptr;

    if (err) {
        Serial.printf("[SEARCH] JSON parse error: %s\n", err.c_str());
        strlcpy(output, "Failed to parse search results", sz);
        return true;
    }

    if (!respDoc["error"].isNull()) {
        const char* errMsg = respDoc["error"]["message"] | "Unknown API error";
        Serial.printf("[SEARCH] API error: %s\n", errMsg);
        snprintf(output, sz, "Search API error: %s", errMsg);
        return true;
    }

    JsonArray results = respDoc["search_result"].as<JsonArray>();
    if (results.isNull() || results.size() == 0) {
        Serial.println("[SEARCH] No results in response");
        strlcpy(output, "No search results found.", sz);
        return true;
    }
    Serial.printf("[SEARCH] Got %d results\n", (int)results.size());

    size_t off = 0;
    int idx = 0;
    for (JsonVariant item : results) {
        if (idx >= M5CLAW_SEARCH_MAX_RESULTS || off >= sz - 40) break;
        const char* title   = item["title"]   | "";
        const char* media   = item["media"]   | "";
        const char* content = item["content"] | "";
        const char* date    = item["publish_date"] | "";

        int w;
        if (date[0]) {
            w = snprintf(output + off, sz - off, "%d. [%s] %s (%s)\n%s\n\n",
                         idx + 1, media, title, date, content);
        } else {
            w = snprintf(output + off, sz - off, "%d. [%s] %s\n%s\n\n",
                         idx + 1, media, title, content);
        }
        if (w <= 0 || (size_t)w >= sz - off) break;
        off += w;
        idx++;
    }

    Serial.printf("[SEARCH] Formatted %d / %d results into %u bytes\n", idx, (int)results.size(), (unsigned)off);
    return true;
}

/* ── cron_add ──────────────────────────────────────── */
static bool tool_cron_add(const char* input, char* output, size_t sz) {
    JsonDocument doc;
    if (deserializeJson(doc, input)) { strlcpy(output, "Invalid JSON", sz); return false; }

    CronJob job = {};
    const char* name = doc["name"] | "";
    const char* schedType = doc["schedule_type"] | "every";
    const char* message = doc["message"] | "";
    const char* channel = doc["channel"] | M5CLAW_CHAN_LOCAL;
    const char* chatId = doc["chat_id"] | "cron";

    if (!name[0] || !message[0]) { strlcpy(output, "Missing name or message", sz); return false; }

    strlcpy(job.name, name, sizeof(job.name));
    strlcpy(job.message, message, sizeof(job.message));
    strlcpy(job.channel, channel, sizeof(job.channel));
    strlcpy(job.chatId, chatId, sizeof(job.chatId));
    job.enabled = true;

    if (strcmp(schedType, "at") == 0) {
        job.kind = CRON_KIND_AT;
        job.atEpoch = doc["at_epoch"] | 0;
        job.deleteAfterRun = true;
        if (job.atEpoch == 0) { strlcpy(output, "Missing at_epoch for one-shot job", sz); return false; }
    } else {
        job.kind = CRON_KIND_EVERY;
        job.intervalSec = doc["interval_s"] | 3600;
    }

    if (CronService::addJob(&job)) {
        snprintf(output, sz, "Cron job '%s' added (id=%s)", name, job.id);
    } else {
        strlcpy(output, "Failed to add cron job (max jobs reached?)", sz);
    }
    return true;
}

/* ── cron_list ─────────────────────────────────────── */
static bool tool_cron_list(const char* input, char* output, size_t sz) {
    (void)input;
    const CronJob* jobs = nullptr;
    int count = 0;
    CronService::listJobs(&jobs, &count);

    if (count == 0) { strlcpy(output, "No cron jobs scheduled.", sz); return true; }

    size_t off = 0;
    for (int i = 0; i < count && off < sz - 100; i++) {
        const CronJob& j = jobs[i];
        if (!j.enabled) continue;
        int w = snprintf(output + off, sz - off,
            "[%s] %s: %s (ch=%s, %s=%d)\n",
            j.id, j.name, j.message, j.channel,
            j.kind == CRON_KIND_AT ? "at" : "every_s",
            j.kind == CRON_KIND_AT ? (int)j.atEpoch : (int)j.intervalSec);
        if (w > 0) off += w;
    }
    return true;
}

/* ── cron_remove ───────────────────────────────────── */
static bool tool_cron_remove(const char* input, char* output, size_t sz) {
    JsonDocument doc;
    if (deserializeJson(doc, input)) { strlcpy(output, "Invalid JSON", sz); return false; }
    const char* jobId = doc["job_id"] | "";
    if (!jobId[0]) { strlcpy(output, "Missing job_id", sz); return false; }

    if (CronService::removeJob(jobId)) {
        snprintf(output, sz, "Removed cron job %s", jobId);
    } else {
        snprintf(output, sz, "Cron job %s not found", jobId);
    }
    return true;
}

/* ── feishu_send (AI-initiated message push) ───────── */
static bool tool_feishu_send(const char* input, char* output, size_t sz) {
    JsonDocument doc;
    if (deserializeJson(doc, input)) { strlcpy(output, "Invalid JSON", sz); return false; }
    const char* chatId = doc["chat_id"] | "";
    const char* text = doc["text"] | "";
    if (!chatId[0] || !text[0]) { strlcpy(output, "Missing chat_id or text", sz); return false; }

    if (FeishuBot::sendMessage(chatId, text)) {
        snprintf(output, sz, "Sent to Feishu %s", chatId);
    } else {
        strlcpy(output, "Failed to send Feishu message (not configured or network error)", sz);
    }
    return true;
}

/* ── Tool registry ─────────────────────────────────── */
static const ToolDef s_tools[] = {
    {
        "get_current_time",
        "Get the current date and time",
        "{\"type\":\"object\",\"properties\":{},\"required\":[]}",
        tool_get_time
    },
    {
        "read_file",
        "Read a file from the device storage",
        "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"File path relative to storage root\"}},\"required\":[\"path\"]}",
        tool_read_file
    },
    {
        "write_file",
        "Write content to a file on device storage",
        "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"File path relative to storage root\"},\"content\":{\"type\":\"string\",\"description\":\"Content to write\"}},\"required\":[\"path\",\"content\"]}",
        tool_write_file
    },
    {
        "edit_file",
        "Find and replace text in a file",
        "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"File path relative to storage root\"},\"old_string\":{\"type\":\"string\",\"description\":\"Text to find\"},\"new_string\":{\"type\":\"string\",\"description\":\"Replacement text\"}},\"required\":[\"path\",\"old_string\",\"new_string\"]}",
        tool_edit_file
    },
    {
        "list_dir",
        "List files on device storage, optionally filtered by prefix",
        "{\"type\":\"object\",\"properties\":{\"prefix\":{\"type\":\"string\",\"description\":\"Optional path prefix filter\"}},\"required\":[]}",
        tool_list_dir
    },
    {
        "web_search",
        "Search the web for current information using Zhipu AI",
        "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\",\"description\":\"Search query\"}},\"required\":[\"query\"]}",
        tool_web_search
    },
    {
        "cron_add",
        "Schedule a recurring or one-shot task",
        "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\",\"description\":\"Job name\"},\"schedule_type\":{\"type\":\"string\",\"description\":\"'every' for recurring or 'at' for one-shot\"},\"interval_s\":{\"type\":\"integer\",\"description\":\"Interval in seconds (for every)\"},\"at_epoch\":{\"type\":\"integer\",\"description\":\"Unix timestamp (for at)\"},\"message\":{\"type\":\"string\",\"description\":\"Message to trigger when job fires\"},\"channel\":{\"type\":\"string\",\"description\":\"Reply channel: local or feishu\"},\"chat_id\":{\"type\":\"string\",\"description\":\"Chat ID for reply routing\"}},\"required\":[\"name\",\"message\"]}",
        tool_cron_add
    },
    {
        "cron_list",
        "List all scheduled cron jobs",
        "{\"type\":\"object\",\"properties\":{},\"required\":[]}",
        tool_cron_list
    },
    {
        "cron_remove",
        "Remove a scheduled cron job by ID",
        "{\"type\":\"object\",\"properties\":{\"job_id\":{\"type\":\"string\",\"description\":\"8-char job ID\"}},\"required\":[\"job_id\"]}",
        tool_cron_remove
    },
    {
        "feishu_send",
        "Send a message to a Feishu chat proactively",
        "{\"type\":\"object\",\"properties\":{\"chat_id\":{\"type\":\"string\",\"description\":\"Feishu open_id or chat_id\"},\"text\":{\"type\":\"string\",\"description\":\"Message text\"}},\"required\":[\"chat_id\",\"text\"]}",
        tool_feishu_send
    },
};

static const int TOOL_COUNT = sizeof(s_tools) / sizeof(s_tools[0]);
static char* s_tools_json = nullptr;

void ToolRegistry::init() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < TOOL_COUNT; i++) {
        JsonObject t = arr.add<JsonObject>();
        t["name"] = s_tools[i].name;
        t["description"] = s_tools[i].description;
        JsonDocument schema;
        deserializeJson(schema, s_tools[i].input_schema_json);
        t["input_schema"] = schema;
    }
    String json;
    serializeJson(doc, json);
    s_tools_json = strdup(json.c_str());
    Serial.printf("[TOOLS] Registered %d tools\n", TOOL_COUNT);
}

const char* ToolRegistry::getToolsJson() { return s_tools_json; }

bool ToolRegistry::execute(const char* name, const char* input, char* output, size_t output_size) {
    for (int i = 0; i < TOOL_COUNT; i++) {
        if (strcmp(s_tools[i].name, name) == 0) {
            Serial.printf("[TOOLS] Execute: %s\n", name);
            return s_tools[i].execute(input, output, output_size);
        }
    }
    snprintf(output, output_size, "Unknown tool: %s", name);
    return false;
}
