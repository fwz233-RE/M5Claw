#include "tool_registry.h"
#include "memory_store.h"
#include "m5claw_config.h"
#include "cron_service.h"
#include "wechat_bot.h"
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <time.h>

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
    size_t prefixLen = strlen(prefix);

    File root = SPIFFS.open("/");
    if (!root) { strlcpy(output, "Cannot open SPIFFS root", sz); return false; }

    size_t off = 0;
    File f = root.openNextFile();
    int count = 0;
    while (f && off < sz - 60) {
        const char* name = f.name();
        if (!prefix[0] || strncmp(name, prefix, prefixLen) == 0) {
            int w = snprintf(output + off, sz - off, "%s (%d bytes)\n", name, (int)f.size());
            if (w > 0) off += w;
            count++;
        }
        f.close();
        f = root.openNextFile();
    }
    root.close();
    if (count == 0) {
        snprintf(output, sz, "No files found%s%s", prefix[0] ? " matching " : "", prefix);
    }
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

    const char* ctxCh = ToolRegistry::getCtxChannel();
    const char* ctxId = ToolRegistry::getCtxChatId();
    const char* channel = ctxCh[0] ? ctxCh : M5CLAW_CHAN_LOCAL;
    const char* chatId  = ctxId[0] ? ctxId : "cron";

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
        job.deleteAfterRun = false;
        if (job.intervalSec == 0) {
            strlcpy(output, "interval_s must be greater than 0", sz);
            return false;
        }
    }

    if (CronService::addJob(&job)) {
        snprintf(output, sz, "Cron job '%s' added (id=%s)", name, job.id);
    } else {
        strlcpy(output, "Failed to add cron job (time not synced, invalid schedule, or max jobs reached)", sz);
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

/* ── wechat_send (AI-initiated message push) ───────── */
static bool tool_wechat_send(const char* input, char* output, size_t sz) {
    JsonDocument doc;
    if (deserializeJson(doc, input)) { strlcpy(output, "Invalid JSON", sz); return false; }
    const char* chatId = doc["chat_id"] | "";
    const char* text = doc["text"] | "";
    if (!chatId[0] || !text[0]) { strlcpy(output, "Missing chat_id or text", sz); return false; }

    if (WechatBot::sendMessage(chatId, text)) {
        snprintf(output, sz, "Sent to WeChat %s", chatId);
    } else {
        strlcpy(output, "Failed to send WeChat message (not configured or network error)", sz);
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
        "cron_add",
        "Schedule a recurring or one-shot task. Notification is automatically sent to the channel where this request originated.",
        "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\",\"description\":\"Job name\"},\"schedule_type\":{\"type\":\"string\",\"description\":\"'every' for recurring or 'at' for one-shot\"},\"interval_s\":{\"type\":\"integer\",\"description\":\"Interval in seconds (for every)\"},\"at_epoch\":{\"type\":\"integer\",\"description\":\"Unix timestamp (for at)\"},\"message\":{\"type\":\"string\",\"description\":\"Message to trigger when job fires\"}},\"required\":[\"name\",\"message\"]}",
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
        "wechat_send",
        "Send a message to a WeChat user proactively",
        "{\"type\":\"object\",\"properties\":{\"chat_id\":{\"type\":\"string\",\"description\":\"WeChat user_id\"},\"text\":{\"type\":\"string\",\"description\":\"Message text\"}},\"required\":[\"chat_id\",\"text\"]}",
        tool_wechat_send
    },
};

static const int TOOL_COUNT = sizeof(s_tools) / sizeof(s_tools[0]);
static char* s_tools_json = nullptr;
static char s_ctx_channel[16] = {0};
static char s_ctx_chatId[96] = {0};

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

void ToolRegistry::setRequestContext(const char* channel, const char* chatId) {
    strlcpy(s_ctx_channel, channel ? channel : "", sizeof(s_ctx_channel));
    strlcpy(s_ctx_chatId, chatId ? chatId : "", sizeof(s_ctx_chatId));
}
const char* ToolRegistry::getCtxChannel() { return s_ctx_channel; }
const char* ToolRegistry::getCtxChatId()  { return s_ctx_chatId; }
