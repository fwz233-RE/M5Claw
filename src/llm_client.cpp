#include "llm_client.h"
#include "m5claw_config.h"
#include "tls_utils.h"
#include <M5Cardputer.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>

static char s_api_key[320] = {0};
static char s_model[64] = M5CLAW_LLM_DEFAULT_MODEL;
static char s_custom_host[128] = {0};
static char s_custom_path[128] = {0};

static volatile bool* s_abort_flag = nullptr;
static bool is_aborted() { return s_abort_flag && *s_abort_flag; }

static LlmPreReadFreeFn s_pre_read_free_fn = nullptr;

static const char* kMediaPlaceholderPrefix = "__M5CLAW_MEDIA|";
static const char* kMediaPlaceholderSuffix = "__";
static constexpr int kMaxRequestMediaRefs = 4;
static constexpr size_t kErrorBodyPreviewMax = 4096;

struct RequestMediaRef {
    size_t pos;
    size_t token_len;
    size_t replacement_len;
    char mime[48];
    char path[128];
};

struct HttpResponseMeta {
    int status_code;
    bool chunked;
    char content_type[64];
};

void llm_client_set_abort_flag(volatile bool* flag) { s_abort_flag = flag; }
void llm_client_set_pre_read_free(LlmPreReadFreeFn fn) { s_pre_read_free_fn = fn; }

static const char* llm_host() {
    return s_custom_host[0] ? s_custom_host : M5CLAW_MIMO_HOST;
}

static const char* llm_path() {
    return s_custom_path[0] ? s_custom_path : M5CLAW_MIMO_CHAT_PATH;
}

static void* alloc_prefer_psram(size_t size) {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    return p;
}

static void safe_copy(char* dst, size_t sz, const char* src) {
    if (!dst || !sz) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strnlen(src, sz - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void llm_client_init(const char* api_key, const char* model, const char* provider,
                     const char* custom_host, const char* custom_path) {
    (void)provider;
    if (api_key && api_key[0]) safe_copy(s_api_key, sizeof(s_api_key), api_key);
    if (model && model[0]) safe_copy(s_model, sizeof(s_model), model);
    if (custom_host && custom_host[0]) safe_copy(s_custom_host, sizeof(s_custom_host), custom_host);
    if (custom_path && custom_path[0]) safe_copy(s_custom_path, sizeof(s_custom_path), custom_path);
    Serial.printf("[LLM] Init MiMo model=%s host=%s path=%s\n", s_model, llm_host(), llm_path());
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
        delay(100);
    }
    return false;
}

static bool secure_connect(WiFiClientSecure& client, const char* host, uint16_t port, const char* tag) {
    IPAddress ip;
    if (!resolve_host(host, ip, tag)) return false;
    TlsConfig::configureClient(client, 30000);
    for (int attempt = 1; attempt <= 2; attempt++) {
        if (client.connect(host, port)) return true;
        Serial.printf("%s connect failed (%d/2)\n", tag, attempt);
        delay(200);
    }
    return false;
}

static bool read_http_headers(WiFiClientSecure& client, HttpResponseMeta* meta) {
    if (!meta) return false;
    meta->status_code = 0;
    meta->chunked = false;
    meta->content_type[0] = '\0';
    char hdr[256];
    int hp = 0;
    int state = 0;
    bool firstLine = true;
    while (client.connected()) {
        if (is_aborted()) return false;
        if (!client.available()) { delay(10); continue; }
        char c = client.read();
        if (c != '\r' && c != '\n' && hp < (int)sizeof(hdr) - 1) hdr[hp++] = c;
        switch (state) {
            case 0: state = (c == '\r') ? 1 : (c == '\n') ? 2 : 0; break;
            case 1: state = (c == '\n') ? 2 : (c == '\r') ? 1 : 0; break;
            case 2: if (c == '\n') return true; state = (c == '\r') ? 3 : 0; break;
            case 3: if (c == '\n') return true; state = 0; break;
        }
        if (c == '\n') {
            hdr[hp] = '\0';
            if (firstLine) {
                firstLine = false;
                const char* sp = strchr(hdr, ' ');
                if (sp) meta->status_code = atoi(sp + 1);
            } else if (strncasecmp(hdr, "transfer-encoding:", 18) == 0) {
                const char* v = hdr + 18;
                while (*v == ' ') v++;
                if (strncasecmp(v, "chunked", 7) == 0) meta->chunked = true;
            } else if (strncasecmp(hdr, "content-type:", 13) == 0) {
                const char* v = hdr + 13;
                while (*v == ' ') v++;
                size_t n = strcspn(v, ";");
                if (n >= sizeof(meta->content_type)) n = sizeof(meta->content_type) - 1;
                memcpy(meta->content_type, v, n);
                meta->content_type[n] = '\0';
            }
            hp = 0;
        }
    }
    return false;
}

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

static bool str_contains_nocase(const char* haystack, const char* needle) {
    if (!haystack || !needle || !needle[0]) return false;
    size_t needleLen = strlen(needle);
    for (const char* p = haystack; *p; ++p) {
        if (strncasecmp(p, needle, needleLen) == 0) return true;
    }
    return false;
}

static bool parse_media_placeholder(const String& body, size_t tokenPos, RequestMediaRef* outRef) {
    if (!outRef) return false;
    size_t prefixLen = strlen(kMediaPlaceholderPrefix);
    size_t suffixLen = strlen(kMediaPlaceholderSuffix);
    size_t bodyLen = body.length();
    if (tokenPos + prefixLen >= bodyLen) return false;
    if (body.substring(tokenPos, tokenPos + prefixLen) != kMediaPlaceholderPrefix) return false;

    size_t mimeSep = body.indexOf('|', tokenPos + prefixLen);
    if (mimeSep == (size_t)-1) return false;
    size_t suffixPos = body.indexOf(kMediaPlaceholderSuffix, mimeSep + 1);
    if (suffixPos == (size_t)-1) return false;
    if (suffixPos <= mimeSep + 1) return false;

    String mime = body.substring(tokenPos + prefixLen, mimeSep);
    String path = body.substring(mimeSep + 1, suffixPos);
    if (mime.isEmpty() || path.isEmpty()) return false;
    if (mime.length() >= sizeof(outRef->mime) || path.length() >= sizeof(outRef->path)) return false;

    strlcpy(outRef->mime, mime.c_str(), sizeof(outRef->mime));
    strlcpy(outRef->path, path.c_str(), sizeof(outRef->path));
    outRef->pos = tokenPos;
    outRef->token_len = (suffixPos + suffixLen) - tokenPos;

    File f = SPIFFS.open(outRef->path, "r");
    if (!f) {
        Serial.printf("[LLM] Media file open failed: %s\n", outRef->path);
        return false;
    }
    size_t fileSize = f.size();
    f.close();
    if (fileSize == 0) {
        Serial.printf("[LLM] Media file is empty: %s\n", outRef->path);
        return false;
    }
    Serial.printf("[LLM] Media file %s size=%u mime=%s\n",
                  outRef->path, (unsigned)fileSize, outRef->mime);

    size_t base64Len = 4 * ((fileSize + 2) / 3);
    outRef->replacement_len = strlen("data:;base64,") + strlen(outRef->mime) + base64Len;
    if (outRef->replacement_len > M5CLAW_MEDIA_DATA_URI_MAX) {
        Serial.printf("[LLM] Media data URI too large: %u\n", (unsigned)outRef->replacement_len);
        return false;
    }
    return true;
}

static bool collect_request_media_refs(const String& body,
                                       RequestMediaRef* refs,
                                       int* outCount,
                                       size_t* outContentLen) {
    if (!outCount || !outContentLen) return false;
    *outCount = 0;
    *outContentLen = body.length();

    size_t prefixLen = strlen(kMediaPlaceholderPrefix);
    int from = 0;
    while (true) {
        int found = body.indexOf(kMediaPlaceholderPrefix, from);
        if (found < 0) return true;
        if (*outCount >= kMaxRequestMediaRefs) {
            Serial.println("[LLM] Too many media attachments in one request");
            return false;
        }

        RequestMediaRef ref = {};
        if (!parse_media_placeholder(body, (size_t)found, &ref)) {
            Serial.println("[LLM] Invalid media placeholder");
            return false;
        }

        refs[*outCount] = ref;
        (*outCount)++;
        if (ref.replacement_len >= ref.token_len) {
            *outContentLen += ref.replacement_len - ref.token_len;
        } else {
            *outContentLen -= ref.token_len - ref.replacement_len;
        }
        from = (int)(ref.pos + ref.token_len);
    }
}

static bool write_all(WiFiClientSecure& client, const char* data, size_t len, size_t* written_total = nullptr) {
    size_t sent = 0;
    while (sent < len) {
        if (is_aborted()) return false;
        size_t wrote = client.write((const uint8_t*)data + sent, len - sent);
        if (wrote == 0) {
            delay(1);
            if (!client.connected()) return false;
            continue;
        }
        sent += wrote;
    }
    if (written_total) *written_total += sent;
    return true;
}

static bool write_media_data_uri(WiFiClientSecure& client, const RequestMediaRef& ref, size_t* written_total = nullptr) {
    File f = SPIFFS.open(ref.path, "r");
    if (!f) {
        Serial.printf("[LLM] Media file open failed during send: %s\n", ref.path);
        return false;
    }

    char prefix[80];
    int prefixLen = snprintf(prefix, sizeof(prefix), "data:%s;base64,", ref.mime);
    if (prefixLen <= 0 || prefixLen >= (int)sizeof(prefix)) {
        f.close();
        return false;
    }
    if (!write_all(client, prefix, prefixLen, written_total)) {
        f.close();
        return false;
    }

    uint8_t rawBuf[768 + 2];
    unsigned char encBuf[4 * ((sizeof(rawBuf) + 2) / 3) + 4];
    size_t carry = 0;

    while (!is_aborted()) {
        size_t got = f.read(rawBuf + carry, sizeof(rawBuf) - carry);
        if (got == 0) break;

        size_t total = carry + got;
        size_t chunkLen = (total / 3) * 3;
        if (chunkLen > 0) {
            size_t encLen = 0;
            if (mbedtls_base64_encode(encBuf, sizeof(encBuf), &encLen, rawBuf, chunkLen) != 0) {
                f.close();
                return false;
            }
            if (!write_all(client, (const char*)encBuf, encLen, written_total)) {
                f.close();
                return false;
            }
        }

        carry = total - chunkLen;
        if (carry > 0) {
            memmove(rawBuf, rawBuf + chunkLen, carry);
        }
    }

    if (carry > 0) {
        size_t encLen = 0;
        if (mbedtls_base64_encode(encBuf, sizeof(encBuf), &encLen, rawBuf, carry) != 0) {
            f.close();
            return false;
        }
        if (!write_all(client, (const char*)encBuf, encLen, written_total)) {
            f.close();
            return false;
        }
    }

    f.close();
    return !is_aborted();
}

static bool send_request_body(WiFiClientSecure& client, const String& body, size_t* outContentLen = nullptr, size_t* outSentLen = nullptr) {
    RequestMediaRef refs[kMaxRequestMediaRefs];
    int refCount = 0;
    size_t contentLen = 0;
    if (!collect_request_media_refs(body, refs, &refCount, &contentLen)) return false;
    if (outContentLen) *outContentLen = contentLen;
    if (outSentLen) *outSentLen = 0;

    client.printf("Content-Length: %u\r\n", (unsigned)contentLen);
    client.println("Connection: close");
    client.println();

    if (refCount == 0) {
        return write_all(client, body.c_str(), body.length(), outSentLen);
    }

    size_t cursor = 0;
    for (int i = 0; i < refCount; i++) {
        const RequestMediaRef& ref = refs[i];
        if (ref.pos < cursor || ref.pos > body.length()) return false;
        if (!write_all(client, body.c_str() + cursor, ref.pos - cursor, outSentLen)) return false;
        if (!write_media_data_uri(client, ref, outSentLen)) return false;
        cursor = ref.pos + ref.token_len;
    }

    if (cursor < body.length()) {
        if (!write_all(client, body.c_str() + cursor, body.length() - cursor, outSentLen)) return false;
    }
    return true;
}

static void build_tool_response_json(LlmResponse* resp) {
    if (resp->call_count <= 0) return;
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < resp->call_count; i++) {
        JsonObject tc = arr.add<JsonObject>();
        tc["id"] = resp->calls[i].id;
        tc["name"] = resp->calls[i].name;
        JsonDocument args;
        deserializeJson(args, resp->calls[i].input ? resp->calls[i].input : "{}");
        tc["arguments"] = args.as<JsonVariant>();
    }
    String raw;
    serializeJson(arr, raw);
    resp->raw_content_json = strdup(raw.c_str());
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

    JsonArray dstTools = doc["tools"].to<JsonArray>();
    JsonObject webSearch = dstTools.add<JsonObject>();
    webSearch["type"] = "web_search";
    webSearch["max_keyword"] = M5CLAW_MIMO_SEARCH_MAX_KEYWORD;
    webSearch["force_search"] = false;
    webSearch["limit"] = M5CLAW_MIMO_SEARCH_LIMIT;

    if (tools_json && tools_json[0]) {
        JsonDocument toolsDoc;
        deserializeJson(toolsDoc, tools_json);
        JsonArray srcTools = toolsDoc.as<JsonArray>();
        for (JsonVariant t : srcTools) {
            JsonObject wrap = dstTools.add<JsonObject>();
            wrap["type"] = "function";
            JsonObject func = wrap["function"].to<JsonObject>();
            func["name"] = t["name"];
            if (t["description"]) func["description"] = t["description"];
            if (t["input_schema"]) func["parameters"] = t["input_schema"];
        }
    }
}

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
        if (choice.isNull()) continue;
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
                if (idx >= resp->call_count) resp->call_count = idx + 1;
                LlmToolCall& call = resp->calls[idx];
                const char* tc_id = tc["id"] | (const char*)nullptr;
                const char* fn_name = tc["function"]["name"] | (const char*)nullptr;
                if (tc_id) strlcpy(call.id, tc_id, sizeof(call.id));
                if (fn_name) strlcpy(call.name, fn_name, sizeof(call.name));
                const char* args = tc["function"]["arguments"] | (const char*)nullptr;
                if (args) tool_inputs[idx] += args;
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
    if (resp->call_count > 0) {
        resp->tool_use = true;
        build_tool_response_json(resp);
    }

    heap_caps_free(line);
    return got_response;
}

static bool read_json_body(WiFiClientSecure& client, bool chunked, char** outBuf, size_t* outLen, size_t maxLen) {
    *outBuf = nullptr;
    *outLen = 0;
    char* buf = (char*)alloc_prefer_psram(maxLen + 1);
    if (!buf) return false;

    size_t len = 0;
    ChunkedReader reader(client, chunked);
    while (len < maxLen) {
        int c = reader.readByte();
        if (c < 0) break;
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    *outBuf = buf;
    *outLen = len;
    return len > 0;
}

static bool parse_openai_json_response(const char* body, size_t bodyLen, LlmResponse* resp) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body, bodyLen);
    if (err) return false;

    JsonObject choice = doc["choices"][0];
    if (choice.isNull()) return false;

    JsonObject message = choice["message"];
    const char* content = message["content"] | (const char*)nullptr;
    if (content && content[0]) {
        if (!text_append(resp, content, strlen(content))) return false;
    }

    JsonArray toolCalls = message["tool_calls"];
    if (!toolCalls.isNull()) {
        int idx = 0;
        for (JsonVariant tc : toolCalls) {
            if (idx >= M5CLAW_MAX_TOOL_CALLS) break;
            LlmToolCall& call = resp->calls[idx];
            const char* tcId = tc["id"] | "";
            const char* fnName = tc["function"]["name"] | "";
            const char* args = tc["function"]["arguments"] | "{}";
            strlcpy(call.id, tcId, sizeof(call.id));
            strlcpy(call.name, fnName, sizeof(call.name));
            call.input = strdup(args);
            call.input_len = strlen(args);
            idx++;
        }
        resp->call_count = idx;
        if (resp->call_count > 0) {
            resp->tool_use = true;
            build_tool_response_json(resp);
        }
    }

    const char* finish = choice["finish_reason"] | "";
    if (strcmp(finish, "tool_calls") == 0) {
        resp->tool_use = true;
    }
    return resp->text_len > 0 || resp->call_count > 0;
}

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

    build_openai_body(*bodyDoc, system_prompt, messages, tools_json);

    String bodyStr;
    bodyStr.reserve(8192);
    serializeJson(*bodyDoc, bodyStr);
    bodyDoc->~JsonDocument();
    heap_caps_free(bodyDoc);

    Serial.printf("[LLM] Request %d bytes to %s%s\n", bodyStr.length(), llm_host(), llm_path());

    if (s_pre_read_free_fn) {
        s_pre_read_free_fn();
    }

    WiFiClientSecure client;
    if (!secure_connect(client, llm_host(), 443, "[LLM]")) {
        Serial.println("[LLM] Connection failed");
        return false;
    }

    client.printf("POST %s HTTP/1.1\r\n", llm_path());
    client.printf("Host: %s\r\n", llm_host());
    client.println("Content-Type: application/json");
    client.println("Accept: text/event-stream");
    client.printf("Authorization: Bearer %s\r\n", s_api_key);
    size_t contentLen = 0;
    size_t sentLen = 0;
    if (!send_request_body(client, bodyStr, &contentLen, &sentLen)) {
        client.stop();
        return false;
    }
    Serial.printf("[LLM] Sent %u/%u bytes\n", (unsigned)sentLen, (unsigned)contentLen);
    bodyStr = String();

    if (is_aborted()) {
        client.stop();
        return false;
    }

    HttpResponseMeta meta = {};
    if (!read_http_headers(client, &meta)) {
        client.stop();
        return false;
    }
    Serial.printf("[LLM] Response status=%d type=%s chunked=%d\n",
                  meta.status_code, meta.content_type[0] ? meta.content_type : "(unknown)", meta.chunked);

    if (meta.status_code < 200 || meta.status_code >= 300) {
        char* errBody = nullptr;
        size_t errLen = 0;
        if (read_json_body(client, meta.chunked, &errBody, &errLen, kErrorBodyPreviewMax) && errBody) {
            Serial.printf("[LLM] Error body: %.400s\n", errBody);
            heap_caps_free(errBody);
        }
        client.stop();
        return false;
    }

    if (str_contains_nocase(meta.content_type, "application/json")) {
        char* jsonBody = nullptr;
        size_t jsonLen = 0;
        bool ok = read_json_body(client, meta.chunked, &jsonBody, &jsonLen, 256 * 1024);
        client.stop();
        if (!ok || !jsonBody) return false;
        bool parsed = parse_openai_json_response(jsonBody, jsonLen, resp);
        if (!parsed) {
            Serial.printf("[LLM] JSON response parse failed: %.400s\n", jsonBody);
        }
        heap_caps_free(jsonBody);
        Serial.printf("[LLM] %d text bytes, %d tool calls, tool_use=%d\n",
                      (int)resp->text_len, resp->call_count, resp->tool_use);
        return parsed;
    }

    ChunkedReader reader(client, meta.chunked);
    bool ok = process_openai_stream(reader, resp, on_token);
    client.stop();

    if (!ok && (!resp->text || resp->text_len == 0) && resp->call_count == 0) {
        Serial.println("[LLM] Empty/failed stream response");
    }
    Serial.printf("[LLM] %d text bytes, %d tool calls, tool_use=%d\n",
                  (int)resp->text_len, resp->call_count, resp->tool_use);
    return ok;
}

static bool tts_post_json(const char* path, const String& bodyStr,
                          HttpResponseMeta* meta, char** body, size_t* bodyLen) {
    *body = nullptr;
    *bodyLen = 0;

    WiFiClientSecure client;
    if (!secure_connect(client, llm_host(), 443, "[TTS]")) return false;

    client.printf("POST %s HTTP/1.1\r\n", path);
    client.printf("Host: %s\r\n", llm_host());
    client.println("Content-Type: application/json");
    client.println("Accept: application/json, audio/*, application/octet-stream");
    client.printf("Authorization: Bearer %s\r\n", s_api_key);
    client.printf("Content-Length: %d\r\n", bodyStr.length());
    client.println("Connection: close");
    client.println();
    client.print(bodyStr);

    HttpResponseMeta localMeta = {};
    if (!read_http_headers(client, &localMeta)) {
        client.stop();
        return false;
    }

    char* respBody = nullptr;
    size_t respLen = 0;
    bool ok = read_json_body(client, localMeta.chunked, &respBody, &respLen, 256 * 1024);
    client.stop();
    if (meta) *meta = localMeta;
    if (!ok || !respBody) return false;

    if (localMeta.status_code < 200 || localMeta.status_code >= 300) {
        Serial.printf("[TTS] HTTP %d path=%s type=%s\n",
                      localMeta.status_code, path,
                      localMeta.content_type[0] ? localMeta.content_type : "(unknown)");
        Serial.printf("[TTS] Error body: %.200s\n", respBody);
        heap_caps_free(respBody);
        return false;
    }

    *body = respBody;
    *bodyLen = respLen;
    return true;
}

static bool play_pcm_audio(const uint8_t* decoded, size_t decodedLen) {
    if (!decoded || decodedLen < 2) return false;

    M5Cardputer.Speaker.stop();
    bool played = M5Cardputer.Speaker.playRaw((const int16_t*)decoded, decodedLen / 2,
                                              M5CLAW_MIMO_TTS_SAMPLE_RATE, false, 1, -1, true);
    if (played) {
        unsigned long waitUntil = millis() + (decodedLen * 1000UL / 2 / M5CLAW_MIMO_TTS_SAMPLE_RATE) + 1500;
        while (M5Cardputer.Speaker.isPlaying() && millis() < waitUntil) {
            delay(10);
        }
    }
    return played;
}

static bool play_wav_audio(const uint8_t* data, size_t len) {
    if (!data || len < 44) return false;
    if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) return false;

    uint16_t audioFormat = 0;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    const uint8_t* pcm = nullptr;
    size_t pcmLen = 0;

    size_t pos = 12;
    while (pos + 8 <= len) {
        const uint8_t* chunk = data + pos;
        uint32_t chunkSize = (uint32_t)chunk[4]
                           | ((uint32_t)chunk[5] << 8)
                           | ((uint32_t)chunk[6] << 16)
                           | ((uint32_t)chunk[7] << 24);
        size_t chunkData = pos + 8;
        if (chunkData + chunkSize > len) break;

        if (memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16) {
            audioFormat = (uint16_t)data[chunkData] | ((uint16_t)data[chunkData + 1] << 8);
            channels = (uint16_t)data[chunkData + 2] | ((uint16_t)data[chunkData + 3] << 8);
            sampleRate = (uint32_t)data[chunkData + 4]
                       | ((uint32_t)data[chunkData + 5] << 8)
                       | ((uint32_t)data[chunkData + 6] << 16)
                       | ((uint32_t)data[chunkData + 7] << 24);
            bitsPerSample = (uint16_t)data[chunkData + 14] | ((uint16_t)data[chunkData + 15] << 8);
        } else if (memcmp(chunk, "data", 4) == 0) {
            pcm = data + chunkData;
            pcmLen = chunkSize;
            break;
        }

        pos = chunkData + chunkSize + (chunkSize & 1U);
    }

    if (!pcm || pcmLen < 2) return false;
    if (audioFormat != 1 || bitsPerSample != 16 || sampleRate == 0) return false;

    M5Cardputer.Speaker.stop();
    bool played = M5Cardputer.Speaker.playRaw((const int16_t*)pcm, pcmLen / 2,
                                              sampleRate, channels > 1, 1, -1, true);
    if (played) {
        unsigned long waitUntil = millis() + (pcmLen * 1000UL / 2 / sampleRate) + 1500;
        while (M5Cardputer.Speaker.isPlaying() && millis() < waitUntil) {
            delay(10);
        }
    }
    return played;
}

static bool extract_audio_b64(const char* body, size_t bodyLen, String& audioB64) {
    JsonDocument respDoc;
    DeserializationError err = deserializeJson(respDoc, body, bodyLen);
    if (err) return false;

    const char* candidate = respDoc["choices"][0]["message"]["audio"]["data"] | "";
    if (!candidate[0]) candidate = respDoc["choices"][0]["audio"]["data"] | "";
    if (!candidate[0]) candidate = respDoc["audio"]["data"] | "";
    if (!candidate[0]) candidate = respDoc["data"] | "";
    if (!candidate[0]) return false;

    audioB64 = candidate;
    return true;
}

bool llm_speak_text(const char* text) {
    if (!text || !text[0] || s_api_key[0] == '\0' || WiFi.status() != WL_CONNECTED) return false;

    String clipped = text;
    if (clipped.length() > M5CLAW_TTS_TEXT_MAX) {
        clipped = clipped.substring(0, M5CLAW_TTS_TEXT_MAX);
    }

    auto tryPlayBody = [&](const HttpResponseMeta& meta, char* body, size_t bodyLen) -> bool {
        if (!body || bodyLen == 0) return false;

        bool isJson = str_contains_nocase(meta.content_type, "application/json");
        if (!isJson) {
            bool played = play_wav_audio((const uint8_t*)body, bodyLen);
            if (!played) played = play_pcm_audio((const uint8_t*)body, bodyLen);
            heap_caps_free(body);
            return played;
        }

        String audioB64;
        bool found = extract_audio_b64(body, bodyLen, audioB64);
        heap_caps_free(body);
        if (!found || audioB64.length() == 0) return false;

        size_t maxDecoded = (audioB64.length() * 3) / 4 + 4;
        uint8_t* decoded = (uint8_t*)alloc_prefer_psram(maxDecoded);
        if (!decoded) return false;

        size_t decodedLen = 0;
        if (mbedtls_base64_decode(decoded, maxDecoded, &decodedLen,
                                  (const unsigned char*)audioB64.c_str(), audioB64.length()) != 0) {
            heap_caps_free(decoded);
            return false;
        }

        bool played = play_wav_audio(decoded, decodedLen);
        if (!played) played = play_pcm_audio(decoded, decodedLen);
        heap_caps_free(decoded);
        return played;
    };

    auto tryChatTts = [&](bool addModalities) -> bool {
        JsonDocument doc;
        doc["model"] = M5CLAW_MIMO_TTS_MODEL;
        JsonArray msgs = doc["messages"].to<JsonArray>();
        JsonObject msg = msgs.add<JsonObject>();
        msg["role"] = "assistant";
        msg["content"] = clipped;
        if (addModalities) {
            JsonArray modalities = doc["modalities"].to<JsonArray>();
            modalities.add("audio");
        }
        JsonObject audio = doc["audio"].to<JsonObject>();
        audio["format"] = "pcm16";
        audio["voice"] = M5CLAW_MIMO_TTS_VOICE;
        doc["stream"] = false;

        String bodyStr;
        bodyStr.reserve(1024);
        serializeJson(doc, bodyStr);

        HttpResponseMeta meta = {};
        char* body = nullptr;
        size_t bodyLen = 0;
        if (!tts_post_json(llm_path(), bodyStr, &meta, &body, &bodyLen)) return false;
        return tryPlayBody(meta, body, bodyLen);
    };

    auto tryAudioSpeech = [&]() -> bool {
        JsonDocument doc;
        doc["model"] = M5CLAW_MIMO_TTS_MODEL;
        doc["input"] = clipped;
        doc["voice"] = M5CLAW_MIMO_TTS_VOICE;
        doc["format"] = "pcm16";
        doc["response_format"] = "pcm16";

        String bodyStr;
        bodyStr.reserve(512);
        serializeJson(doc, bodyStr);

        HttpResponseMeta meta = {};
        char* body = nullptr;
        size_t bodyLen = 0;
        if (!tts_post_json(M5CLAW_MIMO_TTS_PATH, bodyStr, &meta, &body, &bodyLen)) return false;
        return tryPlayBody(meta, body, bodyLen);
    };

    if (tryChatTts(false)) return true;
    if (tryChatTts(true)) return true;
    if (tryAudioSpeech()) return true;

    Serial.println("[TTS] No playable audio in response");
    return false;
}
