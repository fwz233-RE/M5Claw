#pragma once

// Agent Loop
#define M5CLAW_AGENT_STACK             (10 * 1024)
#define M5CLAW_AGENT_PRIO              6
#define M5CLAW_AGENT_CORE              1
#define M5CLAW_AGENT_MAX_HISTORY       6
#define M5CLAW_AGENT_MAX_TOOL_ITER     10
#define M5CLAW_MAX_TOOL_CALLS          4

// Xiaomi MiMo
#define M5CLAW_LLM_DEFAULT_MODEL       "mimo-v2-omni"
#define M5CLAW_LLM_MAX_TOKENS          4096
#define M5CLAW_MIMO_HOST               "api.xiaomimimo.com"
#define M5CLAW_MIMO_CHAT_PATH          "/v1/chat/completions"
#define M5CLAW_MIMO_TTS_MODEL          "mimo-v2-tts"
#define M5CLAW_MIMO_TTS_VOICE          "mimo_default"
#define M5CLAW_MIMO_TTS_SAMPLE_RATE    24000
#define M5CLAW_MIMO_SEARCH_MAX_KEYWORD 3
#define M5CLAW_MIMO_SEARCH_LIMIT       5
#define M5CLAW_SSE_LINE_BUF            2048
#define M5CLAW_LLM_TEXT_MAX            (8 * 1024)
#define M5CLAW_TTS_TEXT_MAX            240

// Agent SPIFFS swap
#define M5CLAW_AGENT_SWAP_FILE         "/tmp_msgs.json"

// Memory / SPIFFS  (SPIFFS.open() auto-prepends mount point "/spiffs")
#define M5CLAW_SOUL_FILE               "/config/SOUL.md"
#define M5CLAW_USER_FILE               "/config/USER.md"
#define M5CLAW_BOOTSTRAP_CONFIG_FILE   "/config/BOOTSTRAP.json"
#define M5CLAW_MEMORY_FILE             "/memory/MEMORY.md"
#define M5CLAW_MEMORY_DIR              "/memory"
#define M5CLAW_SESSION_DIR             "/sessions"
#define M5CLAW_CONTEXT_BUF_SIZE        (4 * 1024)
#define M5CLAW_SESSION_MAX_MSGS        10

// WeChat Bot (iLink protocol)
#define M5CLAW_WECHAT_TASK_STACK       (10 * 1024)
#define M5CLAW_WECHAT_TASK_PRIO        5
#define M5CLAW_WECHAT_TASK_CORE        0
#define M5CLAW_WECHAT_MAX_MSG_LEN      2048
#define M5CLAW_WECHAT_DEDUP_SIZE       64
#define M5CLAW_WECHAT_DEFAULT_HOST     "ilinkai.weixin.qq.com"
#define M5CLAW_WECHAT_API_GETUPDATES   "/ilink/bot/getupdates"
#define M5CLAW_WECHAT_API_SENDMSG      "/ilink/bot/sendmessage"
#define M5CLAW_WECHAT_API_TYPING       "/ilink/bot/sendtyping"
#define M5CLAW_WECHAT_API_CONFIG       "/ilink/bot/getconfig"
#define M5CLAW_WECHAT_QR_PATH          "/ilink/bot/get_bot_qrcode?bot_type=3"
#define M5CLAW_WECHAT_QR_STATUS_PATH   "/ilink/bot/get_qrcode_status?qrcode="
#define M5CLAW_WECHAT_MEDIA_MAX_BYTES  (2 * 1024 * 1024)

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
#define M5CLAW_CHAN_WECHAT             "wechat"
#define M5CLAW_CHAN_SYSTEM              "system"

// Local audio capture / temp media
#define M5CLAW_AUDIO_RECORD_SAMPLE_RATE 16000
#define M5CLAW_AUDIO_CHUNK_SAMPLES      1600
#define M5CLAW_AUDIO_MAX_SECONDS        10
#define M5CLAW_AUDIO_TEMP_FILE          "/tmp_voice.wav"
#define M5CLAW_MEDIA_TEMP_PREFIX        "/tmp_"
#define M5CLAW_MEDIA_DATA_URI_MAX       (3 * 1024 * 1024)

// NTP
#define M5CLAW_NTP_SERVER              "pool.ntp.org"
#define M5CLAW_GMT_OFFSET_SEC          (8 * 3600)
#define M5CLAW_DAYLIGHT_OFFSET_SEC     0
