#include "llm_client.h"
#include "config.h"
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

static char s_api_key[320] = {0};
static char s_model[64] = M5CLAW_LLM_DEFAULT_MODEL;
static char s_provider[16] = M5CLAW_LLM_PROVIDER_DEFAULT;
static char s_custom_host[128] = {0};
static char s_custom_path[128] = {0};

static void safe_copy(char* dst, size_t sz, const char* src) {
    if (!dst || !sz) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strnlen(src, sz - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool provider_is_openai() { return strcmp(s_provider, "openai") == 0; }

static const char* llm_host() {
    if (s_custom_host[0]) return s_custom_host;
    return provider_is_openai() ? M5CLAW_LLM_OPENAI_URL : M5CLAW_LLM_ANTHROPIC_URL;
}
static const char* llm_path() {
    if (s_custom_path[0]) return s_custom_path;
    return provider_is_openai() ? M5CLAW_LLM_OPENAI_PATH : M5CLAW_LLM_ANTHROPIC_PATH;
}

static bool provider_is_minimax_compatible() {
    return strstr(llm_host(), "minimax") != nullptr;
}

void llm_client_init(const char* api_key, const char* model, const char* provider,
                     const char* custom_host, const char* custom_path) {
    if (api_key && api_key[0]) safe_copy(s_api_key, sizeof(s_api_key), api_key);
    if (model && model[0]) safe_copy(s_model, sizeof(s_model), model);
    if (provider && provider[0]) safe_copy(s_provider, sizeof(s_provider), provider);
    if (custom_host && custom_host[0]) safe_copy(s_custom_host, sizeof(s_custom_host), custom_host);
    if (custom_path && custom_path[0]) safe_copy(s_custom_path, sizeof(s_custom_path), custom_path);
    Serial.printf("[LLM] Init provider=%s model=%s host=%s path=%s\n",
                  s_provider, s_model, llm_host(), llm_path());
}

void llm_response_free(LlmResponse* resp) {
    free(resp->text);
    resp->text = nullptr;
    resp->text_len = 0;
    for (int i = 0; i < resp->call_count; i++) {
        free(resp->calls[i].input);
        resp->calls[i].input = nullptr;
    }
    resp->call_count = 0;
    resp->tool_use = false;
}

static bool skip_http_headers(WiFiClientSecure& client, unsigned long deadline) {
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

static int read_http_body(WiFiClientSecure& client, char* buf, int bufSize, unsigned long deadline) {
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

static void decode_chunked_in_place(char* buf, int* len) {
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

        if (src + 2 <= end && src[0] == '\r' && src[1] == '\n') {
            src += 2;
        }
    }

    *len = (int)(dst - buf);
    buf[*len] = '\0';
}

static void build_anthropic_body(JsonDocument& doc, const char* system_prompt,
                                  JsonDocument& messages, const char* tools_json) {
    doc["model"] = s_model;
    doc["max_tokens"] = M5CLAW_LLM_MAX_TOKENS;
    doc["system"] = system_prompt;

    JsonArray msgs = doc["messages"].to<JsonArray>();
    JsonArray src = messages.as<JsonArray>();
    for (JsonVariant v : src) {
        msgs.add(v);
    }

    if (tools_json) {
        JsonDocument toolsDoc;
        deserializeJson(toolsDoc, tools_json);
        doc["tools"] = toolsDoc.as<JsonArray>();
    }
}

static void build_openai_body(JsonDocument& doc, const char* system_prompt,
                               JsonDocument& messages, const char* tools_json) {
    doc["model"] = s_model;
    doc["max_completion_tokens"] = M5CLAW_LLM_MAX_TOKENS;

    JsonArray msgs = doc["messages"].to<JsonArray>();

    JsonObject sysMsg = msgs.add<JsonObject>();
    sysMsg["role"] = "system";
    sysMsg["content"] = system_prompt;

    JsonArray src = messages.as<JsonArray>();
    for (JsonVariant v : src) {
        msgs.add(v);
    }

    if (tools_json) {
        JsonDocument toolsDoc;
        deserializeJson(toolsDoc, tools_json);
        JsonArray srcTools = toolsDoc.as<JsonArray>();
        JsonArray dstTools = doc["tools"].to<JsonArray>();
        for (JsonVariant t : srcTools) {
            JsonObject wrap = dstTools.add<JsonObject>();
            wrap["type"] = "function";
            JsonObject func = wrap["function"].to<JsonObject>();
            func["name"] = t["name"];
            if (t["description"]) func["description"] = t["description"];
            if (t["input_schema"]) func["parameters"] = t["input_schema"];
        }
        doc["tool_choice"] = "auto";
    }
}

static void parse_anthropic_response(const char* json, size_t len, LlmResponse* resp) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, len);
    if (err) {
        Serial.printf("[LLM] Parse error: %s\n", err.c_str());
        return;
    }

    const char* stop = doc["stop_reason"] | "";
    resp->tool_use = (strcmp(stop, "tool_use") == 0);

    JsonArray content = doc["content"].as<JsonArray>();
    if (content.isNull()) return;

    size_t total_text = 0;
    for (JsonVariant block : content) {
        if (strcmp(block["type"] | "", "text") == 0) {
            total_text += strlen(block["text"] | "");
        }
    }

    if (total_text > 0) {
        resp->text = (char*)calloc(1, total_text + 1);
        if (resp->text) {
            for (JsonVariant block : content) {
                if (strcmp(block["type"] | "", "text") != 0) continue;
                const char* t = block["text"] | "";
                size_t tl = strlen(t);
                memcpy(resp->text + resp->text_len, t, tl);
                resp->text_len += tl;
            }
            resp->text[resp->text_len] = '\0';
        }
    }

    for (JsonVariant block : content) {
        if (strcmp(block["type"] | "", "tool_use") != 0) continue;
        if (resp->call_count >= M5CLAW_MAX_TOOL_CALLS) break;
        LlmToolCall& call = resp->calls[resp->call_count];
        memset(&call, 0, sizeof(call));
        strlcpy(call.id, block["id"] | "", sizeof(call.id));
        strlcpy(call.name, block["name"] | "", sizeof(call.name));
        String inputStr;
        serializeJson(block["input"], inputStr);
        call.input = strdup(inputStr.c_str());
        call.input_len = inputStr.length();
        resp->call_count++;
    }
}

static void parse_openai_response(const char* json, size_t len, LlmResponse* resp) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, len);
    if (err) {
        Serial.printf("[LLM] Parse error: %s\n", err.c_str());
        return;
    }

    JsonObject choice = doc["choices"][0];
    const char* finish = choice["finish_reason"] | "";
    resp->tool_use = (strcmp(finish, "tool_calls") == 0);

    JsonObject message = choice["message"];
    const char* content = message["content"] | "";
    if (strlen(content) > 0) {
        resp->text = strdup(content);
        resp->text_len = strlen(content);
    }

    JsonArray tool_calls = message["tool_calls"].as<JsonArray>();
    if (!tool_calls.isNull()) {
        for (JsonVariant tc : tool_calls) {
            if (resp->call_count >= M5CLAW_MAX_TOOL_CALLS) break;
            LlmToolCall& call = resp->calls[resp->call_count];
            memset(&call, 0, sizeof(call));
            strlcpy(call.id, tc["id"] | "", sizeof(call.id));
            strlcpy(call.name, tc["function"]["name"] | "", sizeof(call.name));
            const char* args = tc["function"]["arguments"] | "{}";
            call.input = strdup(args);
            call.input_len = strlen(args);
            resp->call_count++;
        }
        if (resp->call_count > 0) resp->tool_use = true;
    }
}

bool llm_chat_tools(const char* system_prompt,
                    JsonDocument& messages,
                    const char* tools_json,
                    LlmResponse* resp) {
    memset(resp, 0, sizeof(*resp));
    if (s_api_key[0] == '\0') {
        Serial.println("[LLM] No API key configured");
        return false;
    }

    JsonDocument* bodyDoc = new (heap_caps_malloc(sizeof(JsonDocument), MALLOC_CAP_SPIRAM)) JsonDocument;
    if (!bodyDoc) return false;

    if (provider_is_openai()) {
        build_openai_body(*bodyDoc, system_prompt, messages, tools_json);
    } else {
        build_anthropic_body(*bodyDoc, system_prompt, messages, tools_json);
    }

    String bodyStr;
    bodyStr.reserve(4096);
    serializeJson(*bodyDoc, bodyStr);
    bodyDoc->~JsonDocument();
    heap_caps_free(bodyDoc);

    Serial.printf("[LLM] Request %d bytes to %s%s\n", bodyStr.length(), llm_host(), llm_path());

    WiFiClientSecure client;
    client.setInsecure();
    if (!client.connect(llm_host(), 443)) {
        Serial.println("[LLM] Connection failed");
        return false;
    }

    client.printf("POST %s HTTP/1.1\r\n", llm_path());
    client.printf("Host: %s\r\n", llm_host());
    client.println("Content-Type: application/json");
    if (provider_is_openai()) {
        client.printf("Authorization: Bearer %s\r\n", s_api_key);
    } else {
        client.printf("x-api-key: %s\r\n", s_api_key);
        if (provider_is_minimax_compatible()) {
            client.printf("Authorization: Bearer %s\r\n", s_api_key);
        }
        client.printf("anthropic-version: %s\r\n", M5CLAW_LLM_API_VERSION);
    }
    client.printf("Content-Length: %d\r\n", bodyStr.length());
    client.println("Connection: close");
    client.println();
    client.print(bodyStr);
    bodyStr = "";

    unsigned long deadline = millis() + 45000;

    if (!skip_http_headers(client, deadline)) {
        Serial.println("[LLM] Timeout reading headers");
        client.stop();
        return false;
    }

    char* respBuf = (char*)heap_caps_calloc(1, M5CLAW_LLM_RESPONSE_BUF, MALLOC_CAP_SPIRAM);
    if (!respBuf) {
        client.stop();
        return false;
    }

    int respLen = read_http_body(client, respBuf, M5CLAW_LLM_RESPONSE_BUF, deadline);
    client.stop();
    decode_chunked_in_place(respBuf, &respLen);

    Serial.printf("[LLM] Response %d bytes\n", respLen);

    if (respLen == 0) {
        heap_caps_free(respBuf);
        return false;
    }

    if (provider_is_openai()) {
        parse_openai_response(respBuf, respLen, resp);
    } else {
        parse_anthropic_response(respBuf, respLen, resp);
    }

    if (!resp->tool_use && (!resp->text || resp->text_len == 0)) {
        Serial.printf("[LLM] Empty/unknown response: %.300s\n", respBuf);
    }

    heap_caps_free(respBuf);

    Serial.printf("[LLM] %d text bytes, %d tool calls, tool_use=%d\n",
                  (int)resp->text_len, resp->call_count, resp->tool_use);
    return true;
}
