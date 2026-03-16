#pragma once

// Agent Loop
#define M5CLAW_AGENT_STACK             (10 * 1024)
#define M5CLAW_AGENT_PRIO              6
#define M5CLAW_AGENT_CORE              1
#define M5CLAW_AGENT_MAX_HISTORY       10
#define M5CLAW_AGENT_MAX_TOOL_ITER     10
#define M5CLAW_MAX_TOOL_CALLS          4

// LLM
#define M5CLAW_LLM_DEFAULT_MODEL       "claude-sonnet-4-20250514"
#define M5CLAW_LLM_PROVIDER_DEFAULT    "anthropic"
#define M5CLAW_LLM_MAX_TOKENS          2048
#define M5CLAW_LLM_ANTHROPIC_URL       "api.anthropic.com"
#define M5CLAW_LLM_ANTHROPIC_PATH      "/v1/messages"
#define M5CLAW_LLM_OPENAI_URL          "api.openai.com"
#define M5CLAW_LLM_OPENAI_PATH         "/v1/chat/completions"
#define M5CLAW_LLM_API_VERSION         "2023-06-01"
#define M5CLAW_LLM_RESPONSE_BUF        (8 * 1024)

// Memory / SPIFFS
#define M5CLAW_SPIFFS_BASE             "/spiffs"
#define M5CLAW_SOUL_FILE               M5CLAW_SPIFFS_BASE "/config/SOUL.md"
#define M5CLAW_USER_FILE               M5CLAW_SPIFFS_BASE "/config/USER.md"
#define M5CLAW_MEMORY_FILE             M5CLAW_SPIFFS_BASE "/memory/MEMORY.md"
#define M5CLAW_SESSION_DIR             M5CLAW_SPIFFS_BASE "/sessions"
#define M5CLAW_CONTEXT_BUF_SIZE        (4 * 1024)
#define M5CLAW_SESSION_MAX_MSGS        10

// DashScope STT
#define M5CLAW_STT_WS_HOST             "dashscope.aliyuncs.com"
#define M5CLAW_STT_WS_PORT             443
#define M5CLAW_STT_WS_PATH             "/api-ws/v1/inference"
#define M5CLAW_STT_MODEL               "fun-asr-realtime"
#define M5CLAW_STT_SAMPLE_RATE         16000
#define M5CLAW_STT_CHUNK_MS            100
#define M5CLAW_STT_MAX_SECONDS         120

// DashScope TTS
#define M5CLAW_TTS_HOST                "dashscope.aliyuncs.com"
#define M5CLAW_TTS_PATH                "/api/v1/services/aigc/multimodal-generation/generation"
#define M5CLAW_TTS_MODEL               "qwen3-tts-flash"
#define M5CLAW_TTS_VOICE               "Cherry"
#define M5CLAW_TTS_SAMPLE_RATE         24000

// Voice buffer (shared between mic recording and TTS playback)
#define M5CLAW_VOICE_BUF_SECONDS       10
#define M5CLAW_VOICE_BUF_SAMPLES       (M5CLAW_TTS_SAMPLE_RATE * M5CLAW_VOICE_BUF_SECONDS)

// NTP
#define M5CLAW_NTP_SERVER              "pool.ntp.org"
#define M5CLAW_GMT_OFFSET_SEC          (8 * 3600)
#define M5CLAW_DAYLIGHT_OFFSET_SEC     0
