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

---

## 编译烧录教程

### 方法一：使用 VS Code + PlatformIO（推荐）

这是最简单的方式，全图形化操作。

#### 第一步：安装软件

1. 下载安装 [VS Code](https://code.visualstudio.com/)
2. 打开 VS Code，点击左侧扩展图标（或按 `Ctrl+Shift+X`）
3. 搜索 **PlatformIO IDE**，点击安装
4. 等待安装完成（首次需要几分钟下载工具链），VS Code 底部状态栏会出现 PlatformIO 图标

#### 第二步：获取代码

```bash
git clone https://github.com/fwz233-RE/M5Claw.git
```

或者直接在 GitHub 页面点击 **Code → Download ZIP**，解压到任意目录。

#### 第三步：打开项目

1. VS Code 菜单：**文件 → 打开文件夹**
2. 选择 `M5Claw` 文件夹（包含 `platformio.ini` 的那个目录）
3. PlatformIO 会自动识别项目，首次打开时会自动下载 ESP32 工具链和依赖库（需要几分钟）

#### 第四步：连接设备

1. 用 **USB-C 数据线**将 M5Stack Cardputer 连接到电脑
2. Windows 用户：如果设备管理器中没出现 COM 端口，需要安装 [CP210x 驱动](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
3. Mac/Linux 用户：通常免驱，设备会出现为 `/dev/ttyUSB0` 或 `/dev/cu.usbmodem*`

#### 第五步：编译并烧录

**方式 A — 点击按钮（推荐）：**

VS Code 底部状态栏有 PlatformIO 工具按钮：

```
 ✓ Build    →  Upload    🔌 Serial Monitor
```

- 点击 **→ (Upload)** 即可一键编译+烧录
- 点击 **🔌 (Serial Monitor)** 可查看设备日志输出

**方式 B — 命令行：**

```bash
cd M5Claw

# 仅编译（不烧录）
pio run

# 编译 + 烧录固件
pio run -t upload

# 烧录 SPIFFS 数据文件（AI 人设/记忆）
pio run -t uploadfs

# 打开串口监视器查看日志
pio device monitor
```

#### 第六步：烧录 SPIFFS 数据

SPIFFS 中存储了 AI 的人设和记忆文件，首次使用需要单独烧录：

```bash
pio run -t uploadfs
```

或在 VS Code 中：**PlatformIO 侧边栏 → Project Tasks → Upload Filesystem Image**

> 如果跳过这一步，设备启动后会自动创建空文件，AI 功能仍可使用，但没有预设人设。

### 方法二：使用命令行（无需 VS Code）

#### 安装 PlatformIO CLI

```bash
# Python 3.6+ 环境
pip install platformio

# 验证安装
pio --version
```

#### 完整编译烧录流程

```bash
# 1. 克隆代码
git clone https://github.com/fwz233-RE/M5Claw.git
cd M5Claw

# 2. 编译固件（首次会自动下载工具链，需要几分钟）
pio run

# 3. 连接 Cardputer 到 USB，烧录固件
pio run -t upload

# 4. 烧录 SPIFFS 数据文件
pio run -t uploadfs

# 5.（可选）查看串口日志
pio device monitor
```

#### 指定串口端口

如果电脑连了多个串口设备，需要手动指定端口：

```bash
# Windows
pio run -t upload --upload-port COM3

# Mac
pio run -t upload --upload-port /dev/cu.usbmodem3101

# Linux
pio run -t upload --upload-port /dev/ttyUSB0
```

### 方法三：使用 Arduino IDE

1. 安装 [Arduino IDE 2.x](https://www.arduino.cc/en/software)
2. **文件 → 首选项**，附加开发板管理器网址中添加：
   ```
   https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json
   ```
3. **工具 → 开发板管理器**，搜索 `M5Stack` 并安装
4. **工具 → 管理库**，安装以下库：
   - `M5Cardputer`
   - `ArduinoJson`
   - `WebSockets`（by Links2004）
5. **工具 → 开发板** 选择 `M5Stack-StampS3`
6. **工具** 中设置：
   - Flash Size: `8MB`
   - Partition Scheme: `自定义`（需手动导入 `partitions.csv`）
   - PSRAM: `OPI PSRAM`
   - USB CDC On Boot: `Enabled`
7. 打开 `src/main.cpp`，编译上传

> Arduino IDE 方式较繁琐，推荐使用 PlatformIO。

---

## 首次配置

设备烧录完成后，USB 拔掉重新上电（或按 RST 按钮），设备启动后会进入 **Setup 向导**：

| 步骤 | 配置项 | 说明 |
|------|--------|------|
| 1/7 | WiFi SSID | 你的 WiFi 名称（区分大小写） |
| 2/7 | WiFi Password | WiFi 密码 |
| 3/7 | LLM API Key | Anthropic 或 OpenAI 的 API Key |
| 4/7 | Provider | 输入 `anthropic` 或 `openai` |
| 5/7 | Model | 模型名称，如 `claude-sonnet-4-20250514` 或 `gpt-4o` |
| 6/7 | DashScope Key | 阿里百炼 API Key（用于语音识别和合成），去 [百炼控制台](https://bailian.console.aliyun.com/) 获取 |
| 7/7 | City | 天气城市名（英文），如 `Beijing`、`Shanghai`、`Tokyo` |

**操作说明：**
- 用键盘输入文字
- 按 **Enter** 确认当前项并进入下一步
- 按 **Enter**（不输入任何内容）保留当前已有值
- 按 **Tab** 跳过配置直接进入离线模式
- 配完成后设备自动连接 WiFi 并进入伴侣模式

> 所有 API Key 仅存储在设备本地 NVS 中，不会上传到任何服务器，也不会出现在代码里。

### API Key 获取方式

| API Key | 获取地址 | 说明 |
|---------|----------|------|
| Anthropic | https://console.anthropic.com/settings/keys | Claude 模型 |
| OpenAI | https://platform.openai.com/api-keys | GPT 模型 |
| DashScope | https://bailian.console.aliyun.com/#/api-key | 语音识别+合成，新用户有免费额度 |

---

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

---

## 常见问题

### Q: 编译报错 "fatal error: M5Cardputer.h: No such file"
PlatformIO 没有自动下载依赖库。运行 `pio lib install` 或删除 `.pio` 目录后重新编译。

### Q: 上传失败 "A]fatal error occurred: Failed to connect"
1. 确认 USB 线是**数据线**（不是纯充电线）
2. 尝试按住 Cardputer 上的 **G0 按钮**，同时按一下 **RST 按钮**，松开 G0 进入下载模式
3. Windows 用户检查是否安装了 CP210x 驱动

### Q: 设备启动后屏幕全黑
串口监视器检查日志（`pio device monitor`），可能是 WiFi 连接失败。按 **Tab** 进入离线模式查看是否正常。

### Q: 语音识别没反应
确认 DashScope API Key 已正确配置。在聊天模式下**按住 Fn**说话，**松开后**自动识别。如果 DashScope Key 未设置，语音功能会被禁用。

### Q: 如何重新配置 WiFi 和 Key
在伴侣模式下按 **Fn+R**，设备会清除配置并重新进入 Setup 向导。

---

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
├── platformio.ini            # PlatformIO 构建配置
├── partitions.csv            # ESP32 分区表
├── .gitignore
├── LICENSE
└── README.md
```

## 致谢

- [ClawPuter](https://github.com/bryant24hao/ClawPuter) — 像素宠物 UI 和 M5Cardputer 硬件交互
- [MimiClaw](https://github.com/memovai/mimiclaw/blob/main/README_CN.md) — AI Agent 核心架构
- [DashScope](https://help.aliyun.com/zh/model-studio/) — 语音识别和语音合成 API，新用户赠送免费额度
- [M5Stack](https://m5stack.com/) — Cardputer 硬件平台

## 许可证

GPL-3.0
