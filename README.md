# M5ClawPro

M5ClawPro 是一个运行在 M5Stack Cardputer 上的 AI 助手固件，围绕 `ESP32-S3 + SPIFFS + PlatformIO + Xiaomi MiMo` 构建，提供本地陪伴界面、键盘聊天、语音输入、语音播报、微信互联、天气显示、定时任务、记忆文件和技能扩展能力。

项目面向“可直接刷进设备并长期运行”的形态，而不是单次 Demo。仓库内已经包含固件代码、SPIFFS 数据、配置导入脚本和一键刷机脚本。

## 功能概览

- 本地陪伴主页：显示时间、电量、天气，并带有动态日落场景切换。
- 键盘聊天：本地输入问题，流式显示 AI 回复。
- 语音输入：在聊天页按住 `Fn` 录音，松开后上传音频给 MiMo 识别并生成回复。
- TTS 播报：语音输入对应的本地回复会自动调用 MiMo TTS 播放。
- 微信互联：支持 iLink 协议的微信机器人接入，能收发消息、扫码配对、主动推送。
- 图片消息处理：微信发来的图片会下载到本地临时文件，并作为多模态输入交给模型。
- 本地记忆与角色设定：通过 `SOUL.md`、`USER.md`、`MEMORY.md` 持久化人格、用户信息和长期记忆。
- 技能系统：启动时自动加载 `data/skills/*.md`，作为额外的提示词技能。
- 定时任务：支持创建周期任务和一次性任务，任务触发后可回推到本地或微信。
- 心跳检查：定期读取 `HEARTBEAT.md`，发现待处理事项时自动触发 Agent。
- 天气显示：基于 Open-Meteo 做地理解析和实时天气获取。
- 串口配置：支持串口 CLI，也兼容 M5Burner 风格的 NVS 配置协议。

## 技术栈

- 硬件：M5Stack Cardputer
- 主控：ESP32-S3
- 固件框架：Arduino on PlatformIO
- 主要库：
  - `M5Cardputer`
  - `ArduinoJson`
  - `WebSockets`
- 模型服务：Xiaomi MiMo
- 天气服务：Open-Meteo
- 文件系统：SPIFFS

## 仓库结构

```text
.
├─ src/                   固件源码
├─ data/
│  ├─ cert/               TLS 证书包
│  ├─ config/             人设、用户资料、引导配置
│  ├─ memory/             长期记忆
│  └─ skills/             内置技能
├─ flash.py               一键刷机脚本
├─ load_config.py         将 user_config.ini 转成 BOOTSTRAP.json
├─ platformio.ini         PlatformIO 构建配置
├─ partitions.csv         分区表
└─ user_config.ini.example
```

## 环境要求

- 一台 M5Stack Cardputer
- Python 3
- PlatformIO CLI，或安装了 PlatformIO 的 VS Code
- 可联网的 2.4GHz Wi-Fi
- 小米 MiMo API Key
- 可选：微信 iLink 机器人 `token + host`

## 快速开始

### 1. 安装依赖

确保本机可以使用以下命令：

```powershell
python --version
pio --version
```

如果你在 Windows 上，`python` 不可用时也可以改用 `py -3`。

### 2. 准备配置

复制示例配置：

```powershell
Copy-Item user_config.ini.example user_config.ini
```

然后编辑 `user_config.ini`：

```ini
[user]
wifi_ssid = YourWiFi
wifi_pass = YourPassword

mimo_model = mimo-v2-omni
mimo_api_key = YOUR_MIMO_API_KEY

city = Beijing

wechat_token =
wechat_api_host =
```

说明：

- `wifi_ssid`、`wifi_pass`：设备联网所需。
- `mimo_api_key`：必填，没有它无法使用 AI。
- `mimo_model`：默认是 `mimo-v2-omni`。
- `city`：用于天气，建议填英文城市名。
- `wechat_token`、`wechat_api_host`：可选，不填也能先使用本地 AI；之后也可以在设备上扫码配对或通过串口命令配置。

`user_config.ini` 已被 `.gitignore` 忽略，不会提交到仓库。

### 3. 刷写固件

推荐直接使用仓库自带脚本：

```powershell
python flash.py
```

脚本会自动完成：

1. 扫描串口
2. 擦除 Flash
3. 编译固件
4. 上传固件
5. 上传 SPIFFS 数据

如果你想手动执行：

```powershell
pio run
pio run -t upload --upload-port COMx
pio run -t uploadfs --upload-port COMx
```

查看串口日志：

```powershell
pio device monitor -b 115200
```

如果上传失败，可以尝试让设备进入下载模式后重试：按住 `G0`，按一下 `RST`，再松开 `G0`。

## 首次启动流程

设备启动后会按以下顺序初始化：

1. 挂载 SPIFFS
2. 读取 NVS 配置
3. 从 `data/config/BOOTSTRAP.json` 导入引导配置
4. 初始化记忆、技能、工具、微信、定时器和 Agent
5. 如果配置完整则自动联网，否则进入 Setup 模式

`load_config.py` 会在构建前把 `user_config.ini` 转成 `data/config/BOOTSTRAP.json`。设备首次启动时会将这份引导配置导入 NVS，并删除该文件。

如果没有预置配置，设备会进入板载 Setup：

- 依次输入 Wi-Fi、MiMo Key、模型名、城市
- `Enter` 确认当前字段
- `Del` 删除字符
- `Tab` 跳过并进入离线模式

如果已经预置了 MiMo 配置，首次 Setup 只会要求输入 Wi-Fi。

## 设备操作

### Companion 页面

- `Tab`：进入聊天页
- `Ctrl`：进入微信状态页
- 单独按一下 `Fn`：切换日落场景
- `Fn + R`：清除当前 Wi-Fi 并重新进入配置

### Chat 页面

- 普通键盘输入：编辑消息
- `Enter`：发送消息
- `Del`：删除字符
- `Tab`：向上滚动聊天记录
- `Ctrl`：向下滚动；如果已经在底部，则返回 Companion 页面
- `Alt`：返回 Companion 页面
- 按住 `Fn`：开始录音
- 松开 `Fn`：结束录音并发送语音
- `Fn + C`：取消当前模型生成

### WeChat 状态页

- 从 Companion 页面按 `Ctrl` 进入
- `Tab`：返回 Companion 页面
- `Enter`：配对失败时重新尝试
- 未配置时会尝试拉起二维码配对

### 联网失败页

- `Enter`：重试联网
- `Fn + R`：重设 Wi-Fi
- `Tab`：进入离线模式

## 微信功能说明

项目内置了一个基于 iLink 协议的微信桥接层：

- 支持通过二维码配对获取 `bot_token`
- 也支持通过串口直接设置 `token` 和 `host`
- 微信来的文字消息会进入 Agent
- 微信来的图片会下载到 SPIFFS 临时文件后作为多模态输入
- 模型回复会自动回发给对应用户
- 模型也可以通过 `wechat_send` 工具主动给指定用户发消息

微信轮询与语音录制/模型调用之间做了暂停和恢复处理，避免在内存紧张时互相抢占。

## 内置工具

Agent 在运行时可以调用这些设备侧工具：

- `get_current_time`
- `read_file`
- `write_file`
- `edit_file`
- `list_dir`
- `cron_add`
- `cron_list`
- `cron_remove`
- `wechat_send`

此外，MiMo 侧还启用了内置 `web_search`。

## 持久化文件

以下文件位于设备的 SPIFFS 中：

- `data/config/SOUL.md`：助手人格与价值观
- `data/config/USER.md`：用户资料
- `data/memory/MEMORY.md`：长期记忆
- `data/skills/*.md`：技能文件

运行过程中还会生成：

- `/sessions/*.jsonl`：按会话保存的历史消息
- `/cron.json`：定时任务
- `/HEARTBEAT.md`：心跳任务来源文件
- `/tmp_voice.wav`：临时语音文件
- `/tmp_wx_*.bin`：微信图片临时文件

如果你修改了 `data/` 下的文件，需要重新执行：

```powershell
pio run -t uploadfs --upload-port COMx
```

## 串口命令

串口监视器下可用：

```text
help
set_wifi <ssid> <pass>
set_mimo_key <key>
set_mimo_model <model>
set_city <city>
set_wechat <token> <host>
show_config
reset_config
reboot
```

项目还实现了 M5Burner 兼容的 `CMD::GET / CMD::SET / CMD::LIST / CMD::INIT` 配置协议，方便图形化工具直接写入参数。

## 配置建议

- `SOUL.md` 用来定义设备人格，适合写风格、边界和行为原则。
- `USER.md` 用来写用户昵称、语言偏好、时区、所在地等信息。
- `MEMORY.md` 用来保存长期事实，例如习惯、偏好、计划。
- `skills/*.md` 适合存放特定任务的操作指南，比如日报、天气、翻译、提醒等。

## 注意事项

- 本地屏幕通道在系统提示中被限制为“尽量简短”，所以本地回复通常会比微信回复短。
- 设备离线时仍可进入 Companion 页面，但 AI、天气、微信等联网功能不可用。
- 修改 `data/skills`、`data/config`、`data/memory` 后，仅重新上传固件不够，还需要重新上传 SPIFFS。
- 证书文件 `data/cert/x509_crt_bundle.bin` 是 HTTPS 访问所需资源，不要删除。
- 分区表中给 SPIFFS 分配了较大的空间，便于保存技能、记忆、会话和临时媒体文件。

## 许可证

本项目使用 `GPL-3.0` 许可证，详见 [LICENSE](LICENSE)。
