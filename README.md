# M5Claw

**MimiClaw AI Agent on M5Stack Cardputer**

将 [MimiClaw](https://github.com/pprp/mimiclaw) 的 AI Agent 核心移植到 M5Stack Cardputer 硬件上，融合 [ClawPuter](https://github.com/nicebug/ClawPuter) 的像素宠物 UI 和硬件交互层，并通过阿里 DashScope API 实现高性能实时语音识别（STT）和语音合成（TTS）。

## 功能特性

### 像素宠物伴侣
- 小龙虾像素角色 + 多种动画（待机、开心、睡觉、说话、伸懒腰、东张西望）
- 实时天气特效（雨、雪、雾、雷暴）+ 天气配饰
- 日夜循环 + 时光倒流天空
- 键盘方向控制（`;` 上 `.` 下 `,` 左 `/` 右）
- 30 秒无操作自动入睡

### AI Agent 聊天
- 支持 Anthropic Claude 和 OpenAI GPT
- ReAct Agent 循环 + 工具调用（最多 10 轮迭代）
- 内置工具：获取时间、文件读写、网页搜索
- 长期记忆系统（SOUL.md / USER.md / MEMORY.md）
- 会话历史持久化（SPIFFS）
- `/draw` 和 `/draw16` 像素画生成

### 语音交互
- **语音识别**：DashScope fun-asr-realtime（WebSocket 实时转写）
- **语音合成**：DashScope qwen3-tts-flash（HTTP 流式 PCM）
- 按住 Fn 说话，松开自动转写
- AI 回复自动 TTS 朗读，任意键可中断

### 设备与配网
- 首次启动 Setup 向导配置所有参数
- Fn+R 随时重新配置
- 支持双 WiFi（主 WiFi 失败自动切备用）
- 离线模式（仅伴侣功能可用）

## 硬件要求

| 项目 | 规格 |
|------|------|
| 设备 | M5Stack Cardputer |
| MCU | ESP32-S3（双核 240MHz） |
| RAM | 320KB SRAM + 8MB PSRAM |
| Flash | 8MB |
| 屏幕 | 1.14" IPS 240x135 |
| 键盘 | 56 键矩阵 |
| 麦克风 | PDM (SPM1423) |
| 扬声器 | 与麦克风共享 GPIO 43 |

## 快速开始

### 1. 安装 PlatformIO

```bash
pip install platformio
```

### 2. 克隆项目

```bash
git clone https://github.com/fwz233-RE/M5Claw.git
cd M5Claw
```

### 3. 编译烧录

```bash
pio run -t upload
```

### 4. 首次配置

设备启动后进入 Setup 向导，依次输入：

1. **WiFi SSID** — 你的 WiFi 名称
2. **WiFi Password** — WiFi 密码
3. **LLM API Key** — Anthropic 或 OpenAI 的 API Key
4. **Provider** — `anthropic` 或 `openai`
5. **Model** — 如 `claude-sonnet-4-20250514` 或 `gpt-4o`
6. **DashScope Key** — 阿里百炼 API Key（用于语音识别和合成）
7. **City** — 城市名（英文，用于天气）

> 所有 Key 存储在设备 NVS 中，不会上传到任何服务器。

## 操作指南

| 按键 | 伴侣模式 | 聊天模式 |
|------|----------|----------|
| Tab | 切换到聊天 | 切换到伴侣 |
| Fn (按住) | — | 语音输入 |
| Enter | 开心动画 | 发送消息 |
| 空格 | 开心动画 | 输入空格 |
| `;` `.` `,` `/` | 方向移动 | — |
| Fn + `;` | — | 向上滚动 |
| Fn + `/` | — | 向下滚动 |
| Fn + W | 天气模拟 | — |
| Fn + R | 重新配置 | — |
| 1-8 | 切换模拟天气 | — |

## 项目结构

```
M5Claw/
├── src/
│   ├── main.cpp              # 入口 + 模式切换
│   ├── m5claw_config.h       # 全局配置常量
│   ├── config.h/cpp          # NVS 持久化配置
│   ├── utils.h               # 屏幕/颜色/Timer 工具
│   ├── companion.h/cpp       # 像素宠物 + 天气特效
│   ├── chat.h/cpp            # 聊天 UI + 像素画
│   ├── sprites.h             # 像素精灵素材
│   ├── weather_client.h/cpp  # Open-Meteo 天气 API
│   ├── agent.h/cpp           # ReAct Agent 循环
│   ├── llm_client.h/cpp      # Anthropic/OpenAI LLM 客户端
│   ├── context_builder.h/cpp # System Prompt 构建
│   ├── tool_registry.h/cpp   # 工具注册与执行
│   ├── memory_store.h/cpp    # SPIFFS 文件存储
│   ├── session_mgr.h/cpp     # 会话历史管理
│   ├── dashscope_stt.h/cpp   # DashScope 语音识别
│   └── dashscope_tts.h/cpp   # DashScope 语音合成
├── data/                     # SPIFFS 预烧录数据
│   ├── config/SOUL.md        # AI 人设
│   ├── config/USER.md        # 用户信息
│   └── memory/MEMORY.md      # 长期记忆
├── platformio.ini
├── partitions.csv
└── .gitignore
```

## 致谢

- [ClawPuter](https://github.com/nicebug/ClawPuter) — 像素宠物 UI 和 M5Cardputer 硬件交互
- [MimiClaw](https://github.com/pprp/mimiclaw) — AI Agent 核心架构
- [DashScope](https://help.aliyun.com/zh/model-studio/) — 语音识别和语音合成 API
- [M5Stack](https://m5stack.com/) — Cardputer 硬件平台

## 许可证

GPL-3.0
