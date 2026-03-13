#include "tool_registry.h"
#include "memory_store.h"
#include "m5claw_config.h"
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <time.h>

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

static bool tool_read_file(const char* input, char* output, size_t sz) {
    JsonDocument doc;
    if (deserializeJson(doc, input)) { strlcpy(output, "Invalid JSON", sz); return false; }
    const char* path = doc["path"] | "";
    if (!path[0]) { strlcpy(output, "Missing path", sz); return false; }
    char fullPath[128];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", M5CLAW_SPIFFS_BASE, path);
    String content = MemoryStore::readFile(fullPath);
    if (content.length() == 0) {
        snprintf(output, sz, "File not found or empty: %s", path);
    } else {
        strlcpy(output, content.c_str(), sz);
    }
    return true;
}

static bool tool_write_file(const char* input, char* output, size_t sz) {
    JsonDocument doc;
    if (deserializeJson(doc, input)) { strlcpy(output, "Invalid JSON", sz); return false; }
    const char* path = doc["path"] | "";
    const char* content = doc["content"] | "";
    if (!path[0]) { strlcpy(output, "Missing path", sz); return false; }
    char fullPath[128];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", M5CLAW_SPIFFS_BASE, path);
    if (MemoryStore::writeFile(fullPath, content)) {
        snprintf(output, sz, "Written %d bytes to %s", (int)strlen(content), path);
    } else {
        snprintf(output, sz, "Failed to write %s", path);
    }
    return true;
}

static bool tool_web_search(const char* input, char* output, size_t sz) {
    JsonDocument doc;
    if (deserializeJson(doc, input)) { strlcpy(output, "Invalid JSON", sz); return false; }
    const char* query = doc["query"] | "";
    if (!query[0]) { strlcpy(output, "Missing query", sz); return false; }

    strlcpy(output, "Web search not yet implemented on this device.", sz);
    return true;
}

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
        "web_search",
        "Search the web for information",
        "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\",\"description\":\"Search query\"}},\"required\":[\"query\"]}",
        tool_web_search
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
