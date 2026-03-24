# M5Claw

M5Stack Cardputer 上的全功能 AI 助手 — 本地交互 × 云端智能 × 多通道协作。

---

## 功能概览

| 模块 | 能力 |
|------|------|
| **AI Agent** | ReAct 循环 + 10 种工具 + 自动重试 |
| **多模型** | Anthropic Claude / OpenAI 兼容 API（MiniMax 等） |
| **语音输入** | DashScope 实时语音识别（长按 Fn） |
| **微信机器人** | iLink 协议长轮询，私聊双向通信 |
| **网络搜索** | 智谱 AI Web Search，结构化结果 |
| **定时任务** | 周期 / 定时，SPIFFS 持久化 |
| **心跳巡检** | 每 30 分钟检查待办并自动处理 |
| **技能系统** | Markdown 技能文件，AI 可自行扩展 |
| **像素宠物** | Mode-7 公路 + 实时天气特效 |
| **持久记忆** | 长期记忆 / 每日笔记 / 对话历史 |

---

## 硬件要求

- **M5Stack Cardputer**（ESP32-S3, 8 MB PSRAM）
- 内置键盘、麦克风、扬声器、240×135 TFT

---

## 快速开始

### 1. 填写配置

编辑 `user_config.ini`：

```ini
[user]
wifi_ssid = YourWiFi
wifi_pass = YourPassword

llm_provider = anthropic          ; anthropic | openai
llm_model = claude-sonnet-4-20250514
llm_api_key = sk-xxx

; 自定义 LLM 端点（MiniMax 等第三方）
; llm_host = api.minimaxi.com
; llm_path = /anthropic/v1/messages

; 语音识别（可选）
; dashscope_key = sk-xxx

; 微信机器人（可选）
; wechat_token = Bearer_xxx
; wechat_api_host = api.example.com

; 网络搜索（可选）
; glm_search_key = xxx

city = Beijing
```

### 2. 编译烧录

```bash
# PlatformIO CLI
pio run -t uploadfs      # 上传 SPIFFS 数据
pio run -t upload         # 编译 + 烧录固件
pio device monitor        # 串口日志
```

或使用一键刷机脚本：

```bash
python flash.py           # 自动扫描串口 → 清除 → 编译 → 烧录
```

### 3. 运行时配置（串口）

连接串口（115200），输入 `help` 查看所有命令：

| 命令 | 说明 |
|------|------|
| `set_wifi <ssid> <pass>` | 设置 WiFi |
| `set_llm_key <key>` | 设置 LLM API 密钥 |
| `set_llm_provider <p>` | 设置 LLM 提供商 |
| `set_llm_model <model>` | 设置模型 |
| `set_ds_key <key>` | 设置 DashScope 密钥 |
| `set_wechat <token> <host>` | 设置微信 Bot 凭证 |
| `set_glm_key <key>` | 设置智谱搜索密钥 |
| `set_city <city>` | 设置城市 |
| `show_config` | 显示当前配置 |
| `reset_config` | 清除所有配置 |
| `reboot` | 重启设备 |

---

## 操作指南

### 伴侣模式（像素宠物）

Mode-7 风格透视公路背景 + 4 帧行走动画宠物 + 实时天气特效。

顶部状态栏显示：`[Tab]chat` | 时间 | 温度 | 电量

| 按键 | 操作 |
|------|------|
| `Tab` | 切换到聊天模式 |
| `Fn+W` | 切换天气模拟 |
| `Fn+R` | 重置 WiFi，进入配网 |
| `1-8` | 天气模拟时切换天气类型 |

天气类型：晴 → 多云 → 阴 → 雾 → 小雨 → 大雨 → 雪 → 雷暴

### 聊天模式

UTF-8 多语言聊天界面，支持 20 条消息滚动，用户消息蓝色右对齐，AI 回复绿色左对齐。

| 按键 | 操作 |
|------|------|
| `Enter` | 发送消息 |
| `Delete` | 退格 |
| `Tab` | 向上滚动 |
| `Ctrl` | 向下滚动 |
| `Alt` | 返回伴侣模式 |
| `Fn`（长按） | 语音输入 |
| `Fn+C` | 取消 AI 生成 |

### 配网模式

首次启动或 WiFi 未配置时自动进入。

| 按键 | 操作 |
|------|------|
| `Enter` | 确认 / 下一步 |
| `Tab` | 跳过，离线使用 |
| `Backspace` | 删除字符 |

---

## AI Agent

基于 ReAct（Reason + Act）架构的智能代理，运行在 FreeRTOS 独立任务（Core 1）上。

- 最多 10 轮工具调用迭代
- 单轮最多 4 个并行工具调用
- 自动重试（最多 10 次，2 秒间隔）
- 多通道输入：本地键盘 / 微信消息 / 定时任务 / 心跳巡检
- 独立会话管理：每个聊天通道维护独立对话历史

### 工具集

| 工具 | 参数 | 说明 |
|------|------|------|
| `get_current_time` | — | 获取当前日期时间 |
| `read_file` | `path` | 读取设备存储文件 |
| `write_file` | `path`, `content` | 写入/覆盖文件 |
| `edit_file` | `path`, `old_string`, `new_string` | 查找替换文件内容 |
| `list_dir` | `prefix`（可选） | 列出目录文件 |
| `web_search` | `query` | 智谱 AI 网络搜索 |
| `cron_add` | `name`, `message`, `schedule_type`, ... | 添加定时/周期任务 |
| `cron_list` | — | 列出所有定时任务 |
| `cron_remove` | `job_id` | 删除定时任务 |
| `wechat_send` | `chat_id`, `text` | 主动发送微信消息 |

---

## 微信机器人

通过 iLink 协议 HTTP 长轮询实现微信消息双向通信。

- 支持私聊消息
- FNV-1a 哈希环形缓冲区消息去重（64 条）
- AI 处理时自动发送 typing 状态，完成后取消
- 处理 LLM 请求时暂停轮询以释放内存
- AI 可通过 `wechat_send` 工具主动发消息

### 配置步骤

1. 在 PC 端执行 `npx -y @tencent-weixin/openclaw-weixin-cli@latest install` 获取 Bearer Token 和 API Host
2. 将 Token 和 Host 填入 `user_config.ini` 或通过串口 `set_wechat <token> <host>` 设置
3. 在首页按 Ctrl 进入微信状态页查看连接状态

---

## 网络搜索

基于智谱 AI Web Search API（`search_std` 引擎，0.01 元/次），返回结构化搜索结果。

### 配置

1. 访问 [智谱 AI 开放平台](https://open.bigmodel.cn/usercenter/apikeys) 获取 API Key
2. 填入 `user_config.ini` 的 `glm_search_key` 或通过串口 `set_glm_key` 设置

---

## 定时任务（Cron）

| 属性 | 说明 |
|------|------|
| 类型 | `every`（周期）/ `at`（一次性定时） |
| 上限 | 8 个任务 |
| 检查间隔 | 60 秒 |
| 持久化 | `/cron.json`，重启不丢失 |
| 投递通道 | `local`（本地）/ `wechat`（微信） |

AI 可通过 `cron_add` / `cron_list` / `cron_remove` 工具自主管理定时任务。

---

## 心跳巡检

每 30 分钟读取 `/HEARTBEAT.md`，若包含 TODO、REMIND、CHECK、TASK、ALERT 等关键词，自动推送给 AI 处理。适合设置提醒、定期检查等场景。

---

## 技能系统

Markdown 格式的可扩展技能文件，存放于 `/skills/*.md`，启动时自动扫描并注入 system prompt。

**内置技能：**
- `daily-briefing` — 每日简报（新闻 + 天气 + 待办）
- `weather` — 天气查询
- `skill-creator` — 教 AI 创建新技能

AI 可通过 `write_file` 工具自行创建新技能文件，下次对话即生效。

---

## 持久记忆

| 文件 | 用途 |
|------|------|
| `/config/SOUL.md` | AI 人格设定 |
| `/config/USER.md` | 用户信息（名字、语言、时区） |
| `/memory/MEMORY.md` | 长期记忆 |
| `/sessions/<id>.jsonl` | 分通道对话历史（JSONL 格式） |

AI 通过 `read_file` / `write_file` / `edit_file` 工具自主读写记忆文件。

---

## 天气系统

通过 Open-Meteo API 获取实时天气数据，每 15 分钟更新。

支持 8 种天气类型映射（基于 WMO 天气代码）：晴、多云、阴、雾、小雨、大雨、雪、雷暴。天气数据驱动伴侣模式的背景特效。

---

## 内存管理

M5Claw 针对 ESP32-S3 有限内存环境做了深度优化：

- 所有 >1 KB 缓冲区优先分配 PSRAM
- 微信 / 搜索 / Cron / 心跳等服务仅在配置了密钥时初始化
- LLM 请求前释放 JsonDocument 以腾出响应缓冲区
- 处理 LLM 请求时暂停微信轮询释放内存
- Agent 的 system_prompt 和 tool_output 缓冲区跨请求复用
- 语音缓冲区动态计算可用内存自适应大小

---

## 项目结构

```
M5Claw/
├── src/
│   ├── main.cpp              主程序、模式切换、语音录制
│   ├── config.cpp/h          NVS 配置管理
│   ├── companion.cpp/h       像素宠物 + 天气特效
│   ├── chat.cpp/h            聊天界面
│   ├── agent.cpp/h           多通道 ReAct Agent
│   ├── llm_client.cpp/h      LLM HTTP 客户端
│   ├── context_builder.cpp/h System Prompt 构建
│   ├── tool_registry.cpp/h   工具定义与执行
│   ├── memory_store.cpp/h    SPIFFS 文件操作
│   ├── session_mgr.cpp/h     对话历史管理
│   ├── message_bus.cpp/h     FreeRTOS 消息总线
│   ├── wechat_bot.cpp/h      微信 iLink 机器人
│   ├── cron_service.cpp/h    定时任务服务
│   ├── heartbeat.cpp/h       心跳巡检服务
│   ├── skill_loader.cpp/h    技能加载系统
│   ├── weather_client.cpp/h  Open-Meteo 天气客户端
│   ├── dashscope_stt.cpp/h   DashScope 语音识别
│   ├── sprites.h             像素精灵图（51×45, 4 帧）
│   ├── utils.h               颜色、屏幕常量、Timer
│   └── m5claw_config.h       编译时常量
├── data/
│   ├── config/SOUL.md        AI 人格设定
│   ├── config/USER.md        用户信息
│   ├── memory/MEMORY.md      长期记忆
│   └── skills/               技能文件目录
├── user_config.ini           用户配置（编译时注入）
├── load_config.py            配置注入脚本
├── flash.py                  一键刷机脚本
├── platformio.ini            PlatformIO 构建配置
├── partitions.csv            分区表（3 MB app + 5 MB SPIFFS）
└── README.md
```

---

## 架构

```
┌─────────────────────────────────────────────────────────┐
│                    main.cpp (loop)                       │
│    SETUP → COMPANION → CHAT   |  语音录制  |  消息分发   │
└───────────────────────┬─────────────────────────────────┘
                        │
        ┌───────────────┼───────────────┐
        ▼               ▼               ▼
┌─────────────┐  ┌────────────┐  ┌────────────────────┐
│  Companion  │  │  Chat UI   │  │  Agent (Core 1)    │
│  像素宠物    │  │  聊天界面   │  │  ReAct + Tool Call │
└──────┬──────┘  └─────┬──────┘  └─────────┬──────────┘
       │               │                   │
       ▼               ▼                   ▼
┌─────────────┐  ┌────────────┐  ┌────────────────────┐
│  Weather    │  │ SessionMgr │  │    MessageBus      │
│  Client     │  │ MemoryStore│  │  ┌─ FeishuBot      │
└─────────────┘  └────────────┘  │  ├─ CronService    │
                                 │  ├─ Heartbeat      │
                                 │  └─ SkillLoader    │
                                 └────────────────────┘
```

---

## 分区表

| 分区 | 类型 | 大小 |
|------|------|------|
| nvs | NVS | 20 KB |
| otadata | OTA | 8 KB |
| app0 | Factory | 3 MB |
| spiffs | SPIFFS | ~5 MB |

---

## 依赖

| 库 | 版本 |
|----|------|
| [M5Cardputer](https://github.com/m5stack/M5Cardputer) | ^1.0.2 |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | ^7.0.0 |
| [WebSockets](https://github.com/Links2004/arduinoWebSockets) | ^2.4.0 |

---

## 许可证

MIT License
