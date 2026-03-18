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
#define M5CLAW_LLM_RESPONSE_BUF        (4 * 1024)

// Memory / SPIFFS  (SPIFFS.open() auto-prepends mount point "/spiffs")
#define M5CLAW_SOUL_FILE               "/config/SOUL.md"
#define M5CLAW_USER_FILE               "/config/USER.md"
#define M5CLAW_MEMORY_FILE             "/memory/MEMORY.md"
#define M5CLAW_MEMORY_DIR              "/memory"
#define M5CLAW_SESSION_DIR             "/sessions"
#define M5CLAW_CONTEXT_BUF_SIZE        (4 * 1024)
#define M5CLAW_SESSION_MAX_MSGS        10

// Feishu Bot
#define M5CLAW_FEISHU_TASK_STACK       (16 * 1024)
#define M5CLAW_FEISHU_TASK_PRIO        5
#define M5CLAW_FEISHU_TASK_CORE        0
#define M5CLAW_FEISHU_MAX_MSG_LEN      2048
#define M5CLAW_FEISHU_API_BASE         "open.feishu.cn"
#define M5CLAW_FEISHU_DEDUP_SIZE       64

// GLM Web Search (Zhipu AI)
#define M5CLAW_SEARCH_HOST             "open.bigmodel.cn"
#define M5CLAW_SEARCH_PATH             "/api/paas/v4/web_search"
#define M5CLAW_SEARCH_ENGINE           "search_std"
#define M5CLAW_SEARCH_BUF_SIZE         (4 * 1024)
#define M5CLAW_SEARCH_MAX_RESULTS      3

// Cron Service
#define M5CLAW_CRON_FILE               "/cron.json"
#define M5CLAW_CRON_MAX_JOBS           8
#define M5CLAW_CRON_CHECK_MS           (60 * 1000)

// Heartbeat
#define M5CLAW_HEARTBEAT_FILE          "/HEARTBEAT.md"
#define M5CLAW_HEARTBEAT_INTERVAL_MS   (30 * 60 * 1000)

// Skills
#define M5CLAW_SKILLS_PREFIX           "/skills/"

// Message Bus
#define M5CLAW_BUS_QUEUE_LEN           8

// Channel identifiers
#define M5CLAW_CHAN_LOCAL               "local"
#define M5CLAW_CHAN_FEISHU              "feishu"
#define M5CLAW_CHAN_SYSTEM              "system"

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
