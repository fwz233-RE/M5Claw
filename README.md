# M5Claw

在 M5Stack Cardputer 上运行的全功能 AI 助手，融合本地交互与云端智能。

## 功能特性

### AI 对话
- **ReAct Agent**：支持多轮工具调用的智能代理循环
- **多模型支持**：Anthropic Claude / OpenAI 兼容 API（含 MiniMax 等第三方）
- **语音交互**：DashScope 实时语音识别 + TTS 语音合成
- **持久记忆**：SPIFFS 存储的长期记忆、每日笔记、对话历史

### 飞书机器人
- **WebSocket 长连接**：自动重连的飞书消息通道
- **双向通信**：接收飞书消息 → AI 处理 → 自动回复
- **私聊/群聊**：支持 DM 和群组消息路由
- **消息去重**：FNV-1a 哈希环形缓冲区防重复处理
- **主动推送**：AI 可通过 `feishu_send` 工具主动发送消息

### 网络搜索
- **智谱 AI GLM Search**：基于智谱 AI Web Search API 的实时网络搜索
- **搜索引擎**：`search_pro_quark`（夸克版，0.05元/次）
- **结构化结果**：返回标题、链接、摘要，便于 AI 分析

### 定时任务 (Cron)
- **周期任务**：按固定间隔执行（`every` 模式）
- **定时任务**：在指定时间点执行（`at` 模式，一次性）
- **SPIFFS 持久化**：重启后任务不丢失
- **多通道投递**：任务触发消息可路由到本地或飞书

### 心跳检测
- 每 30 分钟检查 `HEARTBEAT.md`
- 发现待办事项自动推送给 AI 处理

### 技能系统
- 可扩展的 Markdown 技能文件（`skills/*.md`）
- 自动扫描并注入 system prompt
- AI 可自行创建新技能

### 本地交互
- **像素宠物**：Mode-7 公路背景 + 天气特效（雨/雪/雾/雷）
- **聊天界面**：UTF-8 多语言支持，滚动消息列表
- **键盘输入**：全键盘 + 快捷键操作

### 工具集
| 工具 | 说明 |
|------|------|
| `get_current_time` | 获取当前日期时间 |
| `read_file` | 读取设备存储文件 |
| `write_file` | 写入/覆盖文件 |
| `edit_file` | 查找替换文件内容 |
| `list_dir` | 列出目录文件 |
| `web_search` | 智谱 AI 网络搜索 |
| `cron_add` | 添加定时/周期任务 |
| `cron_list` | 列出所有定时任务 |
| `cron_remove` | 删除定时任务 |
| `feishu_send` | 主动发送飞书消息 |

## 硬件要求

- M5Stack Cardputer (ESP32-S3, 8MB PSRAM)
- 内置键盘、麦克风、扬声器、240x135 屏幕

## 快速开始

### 1. 配置

编辑 `user_config.ini`：

```ini
[user]
wifi_ssid = 你的WiFi名称
wifi_pass = WiFi密码

llm_provider = anthropic
llm_model = claude-sonnet-4-20250514
llm_api_key = sk-你的API密钥

; 自定义 LLM 端点（可选，如 MiniMax）
; llm_host = api.minimaxi.com
; llm_path = /anthropic/v1/messages

; 语音识别/合成（可选）
dashscope_key = 你的DashScope密钥

; 飞书机器人（可选）
feishu_app_id = 你的飞书应用ID
feishu_app_secret = 你的飞书应用密钥

; 网络搜索（可选）
glm_search_key = 你的智谱AI密钥

city = Beijing
```

### 2. 编译烧录

```bash
pio run -t uploadfs    # 上传 SPIFFS 数据
pio run -t upload      # 编译并烧录固件
pio device monitor     # 查看串口日志
```

### 3. 串口配置（运行时）

连接串口后输入 `help` 查看所有命令：

```
set_wifi <ssid> <pass>      设置 WiFi
set_llm_key <key>           设置 LLM API 密钥
set_llm_provider <p>        设置 LLM 提供商
set_llm_model <model>       设置模型
set_ds_key <key>            设置 DashScope 密钥
set_feishu <id> <secret>    设置飞书应用凭证
set_glm_key <key>           设置智谱搜索密钥
set_city <city>             设置城市
show_config                 显示当前配置
reset_config                清除所有配置
reboot                      重启设备
```

## 飞书机器人配置

1. 访问 [飞书开放平台](https://open.feishu.cn/app) 创建应用
2. 启用**机器人**能力
3. 在「事件与回调」中选择 **长连接** 模式
4. 添加事件订阅：`im.message.receive_v1`
5. 发布应用版本
6. 将 `App ID` 和 `App Secret` 填入 `user_config.ini` 或通过串口 `set_feishu` 命令设置

## 智谱 AI 搜索配置

1. 访问 [智谱 AI 开放平台](https://open.bigmodel.cn/usercenter/apikeys) 获取 API Key
2. 填入 `user_config.ini` 的 `glm_search_key` 或通过串口 `set_glm_key` 命令设置
3. 使用 `search_std` 引擎（0.01元/次）

## 操作指南

### 伴侣模式
- `Tab` — 切换到聊天模式
- `Fn+W` — 切换天气模拟
- `Fn+R` — 重置 WiFi
- `1-8` — 天气模拟模式下切换天气类型

### 聊天模式
- `Enter` — 发送消息
- `Delete` — 退格
- `Tab` — 向上滚动
- `Ctrl` — 向下滚动
- `Alt` — 返回伴侣模式
- `Fn (长按)` — 语音输入

## 内存管理

M5Claw 在 ESP32-S3 有限的内存环境下做了大量优化：

- 所有 >1KB 缓冲区优先分配 PSRAM
- 飞书/搜索/Cron 等服务仅在配置了密钥时才初始化
- 搜索响应缓冲区按需分配，用完即释放
- 语音缓冲区动态计算可用内存，自动适配大小
- 工具输出缓冲区在 Agent 任务中复用

## 项目结构

```
M5Claw/
├── src/
│   ├── main.cpp              主程序入口、模式切换、语音
│   ├── config.cpp/h          NVS 配置管理
│   ├── companion.cpp/h       像素宠物 + 天气特效
│   ├── chat.cpp/h            聊天界面
│   ├── agent.cpp/h           多通道 ReAct Agent
│   ├── llm_client.cpp/h      LLM HTTP 客户端
│   ├── context_builder.cpp/h System Prompt 构建
│   ├── tool_registry.cpp/h   工具定义与执行
│   ├── memory_store.cpp/h    SPIFFS 文件访问
│   ├── session_mgr.cpp/h     对话历史管理
│   ├── message_bus.cpp/h     FreeRTOS 消息总线
│   ├── feishu_bot.cpp/h      飞书 WebSocket 机器人
│   ├── cron_service.cpp/h    定时任务服务
│   ├── heartbeat.cpp/h       心跳检测服务
│   ├── skill_loader.cpp/h    技能加载系统
│   ├── weather_client.cpp/h  天气客户端
│   ├── dashscope_stt.cpp/h   语音识别
│   ├── dashscope_tts.cpp/h   语音合成
│   ├── sprites.h             像素精灵图
│   ├── utils.h               工具函数
│   └── m5claw_config.h       编译时常量
├── data/
│   ├── config/SOUL.md        AI 人格设定
│   ├── config/USER.md        用户信息
│   ├── memory/MEMORY.md      长期记忆
│   └── skills/               技能文件目录
├── user_config.ini           用户配置（编译时注入）
├── load_config.py            配置注入脚本
├── platformio.ini            PlatformIO 构建配置
├── partitions.csv            分区表
└── README.md
```

## 架构

```
┌─────────────────────────────────────────────────────────────┐
│                      main.cpp (loop)                         │
│      SETUP | COMPANION | CHAT + 语音录制/TTS + 消息分发       │
└─────────────────────────┬───────────────────────────────────┘
                          │
      ┌───────────────────┼───────────────────┐
      │                   │                   │
      ▼                   ▼                   ▼
┌───────────┐    ┌──────────────┐    ┌──────────────────┐
│ Companion │    │   Chat UI    │    │ Agent (Core 1)   │
│ 像素宠物   │    │   聊天界面    │    │ ReAct + 工具调用  │
└─────┬─────┘    └──────┬───────┘    └────────┬─────────┘
      │                 │                     │
      ▼                 ▼                     ▼
┌───────────┐    ┌──────────────┐    ┌──────────────────┐
│ Weather   │    │ SessionMgr   │    │ MessageBus       │
│ Client    │    │ MemoryStore  │    │ ┌─ FeishuBot     │
└───────────┘    └──────────────┘    │ ├─ CronService   │
                                     │ ├─ Heartbeat     │
                                     │ └─ SkillLoader   │
                                     └──────────────────┘
```

## 许可证

MIT License
