#include "llm_client.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

static char s_api_key[320] = {0};
static char s_model[64] = M5CLAW_LLM_DEFAULT_MODEL;
static char s_provider[16] = M5CLAW_LLM_PROVIDER_DEFAULT;
static char s_custom_host[128] = {0};
static char s_custom_path[128] = {0};

static volatile bool* s_abort_flag = nullptr;
static bool is_aborted() { return s_abort_flag && *s_abort_flag; }

static LlmPreReadFreeFn s_pre_read_free_fn = nullptr;

void llm_client_set_abort_flag(volatile bool* flag) { s_abort_flag = flag; }
void llm_client_set_pre_read_free(LlmPreReadFreeFn fn) { s_pre_read_free_fn = fn; }

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

static bool resolve_host(const char* host, IPAddress& ip, const char* tag) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("%s WiFi not connected\n", tag);
        return false;
    }
    for (int attempt = 1; attempt <= 2; attempt++) {
        if (WiFi.hostByName(host, ip)) {
            Serial.printf("%s DNS %s -> %s\n", tag, host, ip.toString().c_str());
            return true;
        }
        Serial.printf("%s DNS failed (%d/2)\n", tag, attempt);
        delay(100);
    }
    return false;
}

static bool secure_connect(WiFiClientSecure& client, const char* host, uint16_t port, const char* tag) {
    IPAddress ip;
    if (!resolve_host(host, ip, tag)) return false;
    client.setInsecure();
    client.setTimeout(30000);
    for (int attempt = 1; attempt <= 2; attempt++) {
        if (client.connect(ip, port)) {
            return true;
        }
        Serial.printf("%s connect failed (%d/2)\n", tag, attempt);
        delay(200);
    }
    return false;
}

static void* alloc_prefer_psram(size_t size) {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    return p;
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
    free(resp->raw_content_json);
    resp->raw_content_json = nullptr;
    for (int i = 0; i < resp->call_count; i++) {
        free(resp->calls[i].input);
        resp->calls[i].input = nullptr;
    }
    resp->call_count = 0;
    resp->tool_use = false;
}

// ── HTTP header parser with chunked encoding detection ────────

static bool skip_http_headers(WiFiClientSecure& client, bool* out_chunked) {
    *out_chunked = false;
    char hdr[128];
    int hp = 0;
    int state = 0;
    while (client.connected()) {
        if (is_aborted()) return false;
        if (!client.available()) { delay(10); continue; }
        char c = client.read();
        if (c != '\r' && c != '\n' && hp < 126) hdr[hp++] = c;
        switch (state) {
            case 0: state = (c == '\r') ? 1 : (c == '\n') ? 2 : 0; break;
            case 1: state = (c == '\n') ? 2 : (c == '\r') ? 1 : 0; break;
            case 2: if (c == '\n') return true; state = (c == '\r') ? 3 : 0; break;
            case 3: if (c == '\n') return true; state = 0; break;
        }
        if (c == '\n') {
            hdr[hp] = '\0';
            if (strncasecmp(hdr, "transfer-encoding:", 18) == 0) {
                const char* v = hdr + 18;
                while (*v == ' ') v++;
                if (strncasecmp(v, "chunked", 7) == 0) *out_chunked = true;
            }
            hp = 0;
        }
    }
    return false;
}

// ── Chunked transfer encoding reader ──────────────────────────

struct ChunkedReader {
    WiFiClientSecure& client;
    bool chunked;
    int remaining;
    bool eof;

    ChunkedReader(WiFiClientSecure& c, bool isChunked)
        : client(c), chunked(isChunked), remaining(isChunked ? -1 : 0), eof(false) {}

    int readByte() {
        if (eof || is_aborted()) return -1;
        if (!chunked) return rawRead();
        if (remaining == 0) { skipTrailer(); remaining = -1; }
        if (remaining < 0) {
            remaining = nextChunkSize();
            if (remaining <= 0) { eof = true; return -1; }
        }
        int c = rawRead();
        if (c >= 0) remaining--;
        else eof = true;
        return c;
    }

private:
    int rawRead() {
        unsigned long t = millis();
        while (millis() - t < 30000) {
            if (is_aborted()) return -1;
            if (client.available()) return client.read();
            if (!client.connected()) break;
            delay(1);
        }
        return -1;
    }

    int nextChunkSize() {
        char buf[16];
        int p = 0;
        unsigned long t = millis();
        while (p < 15 && millis() - t < 10000) {
            if (is_aborted()) return 0;
            if (client.available()) {
                char c = client.read();
                if (c == '\n') break;
                if (c != '\r' && c != ' ') buf[p++] = c;
            } else if (!client.connected()) {
                return 0;
            } else {
                delay(1);
            }
        }
        buf[p] = '\0';
        return (int)strtol(buf, nullptr, 16);
    }

    void skipTrailer() {
        for (int i = 0; i < 2; i++) {
            unsigned long t = millis();
            while (millis() - t < 5000) {
                if (is_aborted()) return;
                if (client.available()) { client.read(); break; }
                if (!client.connected()) return;
                delay(1);
            }
        }
    }
};

// ── SSE line reader ───────────────────────────────────────────

static bool read_sse_line(ChunkedReader& reader, char* buf, int maxLen) {
    int pos = 0;
    while (pos < maxLen - 1) {
        int c = reader.readByte();
        if (c < 0) { buf[pos] = '\0'; return pos > 0; }
        if (c == '\n') { buf[pos] = '\0'; return true; }
        if (c != '\r') buf[pos++] = (char)c;
    }
    buf[pos] = '\0';
    return true;
}

// ── Text accumulator (capped at M5CLAW_LLM_TEXT_MAX) ─────────

static bool text_append(LlmResponse* resp, const char* str, size_t len) {
    if (!len) return true;
    size_t new_len = resp->text_len + len;
    if (new_len >= M5CLAW_LLM_TEXT_MAX) {
        if (resp->text_len >= M5CLAW_LLM_TEXT_MAX - 1) return true;
        len = M5CLAW_LLM_TEXT_MAX - 1 - resp->text_len;
        new_len = resp->text_len + len;
    }
    char* nb = (char*)realloc(resp->text, new_len + 1);
    if (!nb) return false;
    memcpy(nb + resp->text_len, str, len);
    nb[new_len] = '\0';
    resp->text = nb;
    resp->text_len = new_len;
    return true;
}

// ── Request body builders ─────────────────────────────────────

static void build_anthropic_body(JsonDocument& doc, const char* system_prompt,
                                  JsonDocument& messages, const char* tools_json) {
    doc["model"] = s_model;
    doc["max_tokens"] = M5CLAW_LLM_MAX_TOKENS;
    doc["stream"] = true;
    doc["system"] = system_prompt;

    JsonArray msgs = doc["messages"].to<JsonArray>();
    JsonArray src = messages.as<JsonArray>();
    for (JsonVariant v : src) msgs.add(v);

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
    doc["stream"] = true;

    JsonArray msgs = doc["messages"].to<JsonArray>();
    JsonObject sysMsg = msgs.add<JsonObject>();
    sysMsg["role"] = "system";
    sysMsg["content"] = system_prompt;

    JsonArray src = messages.as<JsonArray>();
    for (JsonVariant v : src) msgs.add(v);

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

// ── Build raw_content_json from streamed data ─────────────────

static void build_raw_content_json(LlmResponse* resp) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    if (resp->text && resp->text_len > 0) {
        JsonObject tb = arr.add<JsonObject>();
        tb["type"] = "text";
        tb["text"] = resp->text;
    }
    for (int i = 0; i < resp->call_count; i++) {
        JsonObject tu = arr.add<JsonObject>();
        tu["type"] = "tool_use";
        tu["id"] = resp->calls[i].id;
        tu["name"] = resp->calls[i].name;
        JsonDocument inputDoc;
        deserializeJson(inputDoc, resp->calls[i].input ? resp->calls[i].input : "{}");
        tu["input"] = inputDoc;
    }

    String raw;
    serializeJson(arr, raw);
    resp->raw_content_json = strdup(raw.c_str());
}

// ── Anthropic SSE stream processor ────────────────────────────

static bool process_anthropic_stream(ChunkedReader& reader, LlmResponse* resp,
                                      LlmStreamCallback on_token) {
    char* line = (char*)alloc_prefer_psram(M5CLAW_SSE_LINE_BUF);
    if (!line) return false;

    String tool_inputs[M5CLAW_MAX_TOOL_CALLS];
    int current_tool_idx = -1;
    bool got_response = false;

    while (!is_aborted()) {
        if (!read_sse_line(reader, line, M5CLAW_SSE_LINE_BUF)) break;

        if (strncmp(line, "data: ", 6) != 0) continue;
        const char* json = line + 6;

        JsonDocument chunk;
        if (deserializeJson(chunk, json) != DeserializationError::Ok) continue;

        const char* type = chunk["type"] | "";

        if (strcmp(type, "content_block_start") == 0) {
            JsonObject block = chunk["content_block"];
            const char* btype = block["type"] | "";
            if (strcmp(btype, "tool_use") == 0 && resp->call_count < M5CLAW_MAX_TOOL_CALLS) {
                current_tool_idx = resp->call_count;
                LlmToolCall& call = resp->calls[resp->call_count];
                memset(&call, 0, sizeof(call));
                strlcpy(call.id, block["id"] | "", sizeof(call.id));
                strlcpy(call.name, block["name"] | "", sizeof(call.name));
                resp->call_count++;
            }
        }
        else if (strcmp(type, "content_block_delta") == 0) {
            JsonObject delta = chunk["delta"];
            const char* dtype = delta["type"] | "";

            if (strcmp(dtype, "text_delta") == 0) {
                const char* text = delta["text"] | "";
                size_t tlen = strlen(text);
                if (tlen > 0) {
                    text_append(resp, text, tlen);
                    if (on_token) on_token(text);
                    got_response = true;
                }
            }
            else if (strcmp(dtype, "input_json_delta") == 0) {
                const char* partial = delta["partial_json"] | "";
                if (current_tool_idx >= 0 && current_tool_idx < M5CLAW_MAX_TOOL_CALLS)
                    tool_inputs[current_tool_idx] += partial;
            }
        }
        else if (strcmp(type, "content_block_stop") == 0) {
            if (current_tool_idx >= 0 && current_tool_idx < resp->call_count) {
                String& inp = tool_inputs[current_tool_idx];
                if (inp.length() > 0) {
                    resp->calls[current_tool_idx].input = strdup(inp.c_str());
                    resp->calls[current_tool_idx].input_len = inp.length();
                    inp = "";
                }
            }
            current_tool_idx = -1;
        }
        else if (strcmp(type, "message_delta") == 0) {
            const char* stop = chunk["delta"]["stop_reason"] | "";
            resp->tool_use = (strcmp(stop, "tool_use") == 0);
            got_response = true;
        }
        else if (strcmp(type, "message_stop") == 0) {
            got_response = true;
            break;
        }
        else if (strcmp(type, "error") == 0) {
            const char* msg = chunk["error"]["message"] | "unknown error";
            Serial.printf("[LLM] Stream error: %s\n", msg);
            break;
        }
    }

    if (resp->tool_use || resp->call_count > 0)
        build_raw_content_json(resp);

    heap_caps_free(line);
    return got_response;
}

// ── OpenAI SSE stream processor ───────────────────────────────

static bool process_openai_stream(ChunkedReader& reader, LlmResponse* resp,
                                   LlmStreamCallback on_token) {
    char* line = (char*)alloc_prefer_psram(M5CLAW_SSE_LINE_BUF);
    if (!line) return false;

    String tool_inputs[M5CLAW_MAX_TOOL_CALLS];
    bool got_response = false;

    while (!is_aborted()) {
        if (!read_sse_line(reader, line, M5CLAW_SSE_LINE_BUF)) break;

        if (strncmp(line, "data: ", 6) != 0) continue;
        const char* data = line + 6;

        if (strcmp(data, "[DONE]") == 0) {
            got_response = true;
            break;
        }

        JsonDocument chunk;
        if (deserializeJson(chunk, data) != DeserializationError::Ok) continue;

        JsonObject choice = chunk["choices"][0];
        JsonObject delta = choice["delta"];

        const char* content = delta["content"] | (const char*)nullptr;
        if (content) {
            size_t clen = strlen(content);
            if (clen > 0) {
                text_append(resp, content, clen);
                if (on_token) on_token(content);
                got_response = true;
            }
        }

        JsonArray tool_calls = delta["tool_calls"];
        if (!tool_calls.isNull()) {
            for (JsonVariant tc : tool_calls) {
                int idx = tc["index"] | 0;
                if (idx >= M5CLAW_MAX_TOOL_CALLS) continue;

                const char* tc_id = tc["id"] | (const char*)nullptr;
                const char* fn_name = tc["function"]["name"] | (const char*)nullptr;
                if (tc_id && resp->call_count <= idx) {
                    LlmToolCall& call = resp->calls[idx];
                    memset(&call, 0, sizeof(call));
                    strlcpy(call.id, tc_id, sizeof(call.id));
                    if (fn_name) strlcpy(call.name, fn_name, sizeof(call.name));
                    if (idx >= resp->call_count) resp->call_count = idx + 1;
                }

                const char* args = tc["function"]["arguments"] | (const char*)nullptr;
                if (args && idx < M5CLAW_MAX_TOOL_CALLS)
                    tool_inputs[idx] += args;
            }
        }

        const char* finish = choice["finish_reason"] | (const char*)nullptr;
        if (finish) {
            resp->tool_use = (strcmp(finish, "tool_calls") == 0);
            got_response = true;
        }
    }

    for (int i = 0; i < resp->call_count; i++) {
        if (tool_inputs[i].length() > 0 && resp->calls[i].input == nullptr) {
            resp->calls[i].input = strdup(tool_inputs[i].c_str());
            resp->calls[i].input_len = tool_inputs[i].length();
        }
    }
    if (resp->call_count > 0) resp->tool_use = true;

    if (resp->tool_use)
        build_raw_content_json(resp);

    heap_caps_free(line);
    return got_response;
}

// ── Main LLM function (SSE streaming) ─────────────────────────

bool llm_chat_tools(const char* system_prompt,
                    JsonDocument& messages,
                    const char* tools_json,
                    LlmResponse* resp,
                    LlmStreamCallback on_token) {
    memset(resp, 0, sizeof(*resp));
    if (s_api_key[0] == '\0') {
        Serial.println("[LLM] No API key configured");
        return false;
    }

    void* bodyDocMem = alloc_prefer_psram(sizeof(JsonDocument));
    JsonDocument* bodyDoc = bodyDocMem ? new (bodyDocMem) JsonDocument : nullptr;
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
    if (!secure_connect(client, llm_host(), 443, "[LLM]")) {
        Serial.println("[LLM] Connection failed");
        return false;
    }

    client.printf("POST %s HTTP/1.1\r\n", llm_path());
    client.printf("Host: %s\r\n", llm_host());
    client.println("Content-Type: application/json");
    client.println("Accept: text/event-stream");
    if (provider_is_openai()) {
        client.printf("Authorization: Bearer %s\r\n", s_api_key);
    } else {
        client.printf("x-api-key: %s\r\n", s_api_key);
        client.printf("anthropic-version: %s\r\n", M5CLAW_LLM_API_VERSION);
    }
    client.printf("Content-Length: %d\r\n", bodyStr.length());
    client.println("Connection: close");
    client.println();
    client.print(bodyStr);
    bodyStr = "";

    if (s_pre_read_free_fn) {
        s_pre_read_free_fn();
    }

    if (is_aborted()) {
        Serial.println("[LLM] Aborted before reading response");
        client.stop();
        return false;
    }

    bool chunked = false;
    if (!skip_http_headers(client, &chunked)) {
        Serial.println("[LLM] Disconnected or aborted waiting for response");
        client.stop();
        return false;
    }

    Serial.printf("[LLM] Streaming response (chunked=%d, heap=%d)\n", chunked, ESP.getFreeHeap());

    ChunkedReader reader(client, chunked);
    bool ok;
    if (provider_is_openai()) {
        ok = process_openai_stream(reader, resp, on_token);
    } else {
        ok = process_anthropic_stream(reader, resp, on_token);
    }

    client.stop();

    if (!ok && (!resp->text || resp->text_len == 0) && resp->call_count == 0) {
        Serial.println("[LLM] Empty/failed stream response");
    }

    Serial.printf("[LLM] %d text bytes, %d tool calls, tool_use=%d\n",
                  (int)resp->text_len, resp->call_count, resp->tool_use);
    return ok;
}
