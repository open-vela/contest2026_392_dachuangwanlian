# 基于 BES 2800 BP 的联网语音智能体手表

## 一、作品简介

一款面向日常效率场景、运行在 BES 2800 BP 智能手表上的**端云协同语音 AI 智能体**，让用户通过自然语言对话即可完成信息检索与设备控制，解决小屏穿戴设备"交互繁琐、信息获取慢"的核心痛点。用户对着手表说出自然语言指令，云端 ASR 识别语音后，LLM 通过 MCP（Model Context Protocol）工具调用驱动手表端执行对应操作（闹钟、运动、秒表、计时器、心率、睡眠监测等），无需触屏，全程语音交互。

决赛阶段新增**蓝牙录音转写系统**：手表与手机通过经典蓝牙 SPP 直连，录音音频经 Opus 压缩后实时推流到手机端 App 转写落库；蓝牙断连也能随时录音（离线缓存 eMMC，连上后自动回传补录），并支持小智语音指令「开启录音」一键跳转录音 App 自动录音。

## 二、选题方向

**手表应用创新**

选择理由：本作品核心在于将 MCP 工具调用机制落地到嵌入式手表端侧，通过端云协同架构，让云端 LLM 理解用户自然语言意图并自动路由到手表端预注册的 MCP 工具执行操作，实现真正的 AI 语音闭环控制。

## 三、目录结构

```
contest2026_392_dachuangwanlian/
├── app/
│   ├── tswatch/                    — 主应用（手表核心代码）
│   │   ├── xiaozhi_voice/          — 小智 AI 语音客户端 + MCP Server
│   │   ├── bt/                     — 蓝牙通信底座（SPP 传输/配对/网络共享）
│   │   ├── recorder/               — 录音应用（三态 UI 录音界面）
│   │   ├── spp_audio/              — SPP 音频流（采集/Opus 编码/离线缓存）
│   │   ├── offline_recording/      — 离线录音列表（待同步/同步中/已同步）
│   │   ├── stopwatch/              — 秒表应用（支持 MCP 语音启停/重置）
│   │   ├── clock/                  — 闹钟应用（支持 MCP 语音添加/删除闹钟）
│   │   ├── exercise/               — 运动应用（支持 MCP 语音开始/停止运动）
│   │   ├── heart/                  — 心率监测应用
│   │   ├── sleep/                  — 睡眠监测应用
│   │   ├── timer/                  — 计时器应用
│   │   ├── settings/               — 设置应用
│   │   ├── main_page/              — 表盘主页
│   │   ├── applist/                — 应用列表
│   │   ├── utils/                  — 工具函数（字体管理、日志等）
│   │   └── res/                    — 图片/资源文件
│   └── hello_app/                  — 应用形态样例（占位骨架）
├── board/
│   ├── contest_board/              — 板级适配（defconfig 等）
│   └── best1700_ep/                — BES1700 EP 板级支持（aos_evb 配置）
├── quickapp/
│   └── hello_quickapp/             — 快应用样例（占位骨架）
├── skills/
│   └── bes2800bp-build-flash/      — AI 辅助构建/烧录技能
├── logs/                           — AI Coding 对话日志
├── .claude/                        — Claude Code 项目配置
├── .github/                        — GitHub 工作流/Issue 模板
├── contest2026_392_dachuangwanlian.xml — 大赛 manifest
├── openvela.xml                    — OpenVela 代码仓 manifest
└── README.md                       — 本文件
```

## 四、运行方式

### 4.1 环境准备

**系统依赖（Ubuntu）：**

```bash
sudo apt install -y git cmake python3 build-essential curl bison flex cpio \
  gperf libncurses-dev libssl-dev libgmp-dev libmpfr-dev libmpc-dev libpulse-dev
sudo pip3 install kconfiglib
```

工具链 `arm-none-eabi-gcc` 已随仓库提供，路径 `prebuilts/gcc/linux-x86_64/arm-none-eabi/`。

### 4.2 代码拉取

```bash
repo init -u https://github.com/open-vela/manifest.git -b dev-ai-contest-2026
repo sync -j8
```

### 4.3 编译

**AP 核（主应用）：**

```bash
cd ..
rm -rf cmake_out/aos_evb_ap
./build.sh contest2026_392_dachuangwanlian/board/best1700_ep/aos_evb/configs/ap --cmake -j8
```

成功标志：输出 `#### build completed successfully`。

产物：`cmake_out/aos_evb_ap/nuttx_ap.bin`（约 1.7MB）。

### 4.4 烧录

**增量烧录（仅更新 AP + APC1）：**

```bash
cd ..
sudo ./prebuild/m1/dldtool --pgm-rate 2000000 /dev/ttyUSB0 \
  ./prebuild/programmer1700_dual.bin --set-dual-chip 1 \
  -M ./cmake_out/aos_evb_ap/nuttx_ap.bin \
  -M ./cmake_out/aos_evb_apc1/nuttx_apc1.bin
```

**全量烧录（裸板）：** 需额外提供 bl/ota/bth/bthcp/audio 镜像，详见 `skills/bes2800bp-build-flash/references/flash-guide.md`。

### 4.5 运行验证

1. 串口连接板卡（USB 枚举设备），打开串口终端
2. 复位或重新上电板卡
3. 观察 NuttX 启动日志，出现 `nsh` 提示符即启动成功
4. LVGL 界面显示手表表盘，用串口输入以下命令配置连接wifi:
ifup wlan0
wapi mode wlan0 2 
wapi psk wlan0 xxx(密码) 3
wapi essid wlan0 xxx(wifi名称) 1
renew wlan0
5. 左滑进入applist,点击进入小智AI，长按按钮开始对话

### 4.6 语音交互示例

对着板子麦克风说：
- "帮我定一个明早7点的闹钟" → MCP 调用 `self.alarm.add`
- "开始跑步" → MCP 调用 `self.exercise.start`
- "打开秒表" → MCP 调用 `self.stopwatch.start`
- "暂停" → MCP 调用 `self.stopwatch.pause`
- "打开心率" → MCP 调用 `self.heart.open`
- "打开设置" → MCP 调用 `self.settings.open`
- "开启录音" → MCP 调用 `self.recorder.open`（关闭小智并自动开始录音）

## 五、决赛新增：蓝牙录音转写系统

### 5.1 整体架构

手表与手机通过**经典蓝牙 SPP（串口协议）**直连，录音音频经 Opus 压缩后在 SPP 通道上实时推流；小智语音助手则走 WebSocket(TLS) 上云。手机端 App（Vela Companion，Kotlin + Compose）负责把蓝牙收到的音频解码后送云端 ASR，并把离线缓存的录音文件批量转写落库。

```
┌──────────────────────────┐         SPP(RFCOMM)          ┌──────────────────────────┐
│       手表 (TSWATCH)       │ ◄───────────音频帧─────────────► │    手机 (Vela Companion)  │
│  recorder (录音 App)      │   Opus/PCM/控制/文件/回执      │  SppManager 帧解析        │
│  spp_audio_stream        │                              │  Opus 解码 → 讯飞 RTASR   │
│  offline_recording       │         WebSocket(TLS)        │  离线文件批量转写(Ifasr)   │
│  小智 xiaozhi_voice       │ ────► 小智云 (STT/TTS/LLM)    │  Room 录音历史库          │
└──────────────────────────┘                              └──────────────────────────┘
```

两条独立链路互不阻塞：
- **录音/转写链路**：手表 ↔ 手机，走 SPP 蓝牙串口（实时性好、不依赖网络）；
- **小智 AI 助手链路**：手表 ↔ 小智云，走 WebSocket/TLS（依赖 WiFi）。

### 5.2 蓝牙通信底座（bt/）

- 手表端以标准 **SPP Server UUID `0x1101`** 注册串口服务，手机端经 SDP 查询建立 RFCOMM 连接；BLE 仅用于设备扫描发现（筛选名称以 `TSWATCH` 开头的设备）。
- 开机由 `bt_manager_start()` 启动蓝牙子系统：自动连接已配对设备、未连接时保持可发现可配对、连接成功后关闭可发现。
- 配套模块：`bt_pair`（配对管理）、`bt_pair_ui`（配对界面）、`bt_adv`（广播）、`bt_tether`（蓝牙网络共享）。

### 5.3 SPP 帧协议（手表 ↔ 手机一致）

统一帧格式 `magic(0xA5) + type + seq(2B 大端) + len(2B 大端) + payload`，双端常量严格对齐：

| type | 用途 |
|---|---|
| 0x01 PCM | 16k/16bit 小端原始 PCM（历史兼容） |
| 0x02 CONTROL | 会话开始/结束控制（驱动手机端 ASR 会话生命周期） |
| 0x03 TEXT | UTF-8 状态/调试字符串 |
| 0x04 OPUS | 手表编码后的 Opus 包（40ms@16k），手机解码回 PCM 再送 ASR |
| 0x05 FILE | 离线音频文件回传（START/DATA/END 三段） |
| 0x06 FILE_ACK | 手机→手表：离线文件处理结果回执 |

同一个蓝牙流里既能传实时音频（OPUS）、也能传离线文件（FILE）、还能传控制/文本，type 字段是双路解耦的关键。

### 5.4 录音应用（recorder/ + spp_audio/）

- **455×455 圆形屏三态 UI**：待机 → 录音中 → 已保存；主按钮命中区恒定（点击 100% 可靠），背后的振幅呼吸层 + 3 圈外围呼吸环随麦克风能量律动；顶部显示蓝牙连接状态胶囊，底部「离线 N」入口打开离线录音列表。
- **采集**：经 media framework（`media_recorder_open(MEDIA_SOURCE_MIC)`）取 16kHz/16bit/单声道/60ms 帧原始 PCM——本板 codec 直接裸开 `/dev/audio` 采集会触发 DMA 断言 kernel panic，media_recorder 是受支持、不崩溃的取麦路径。
- **软件麦克风增益**：采集后、发送前对每个样本乘增益（≈18dB），把近乎静音的原始麦能抬到可用语音区间。
- **Opus 编码**：每 40ms 合成一个 Opus 包，16kbps VOIP 模式（相对原始 PCM 32KB/s 降低约 16 倍），VBR 把静音段压到近乎 0 字节。
- **不间断发送**：轻量 VAD 仅用于日志与振幅诊断、不门控发送，所有帧（含静音）连续编码发送，保证 ASR 收到连续音频流。
- **电源键即停**：录音进行中按电源键优先结束录音流。

### 5.5 两条去向：实时推流 vs 离线缓存

每凑满一个 40ms Opus 包：
- **已连蓝牙**：实时经 SPP 直发手机 → 手机 Concentus 解码（纯 Java Opus 实现，规避厂商系统 Opus 解码器对裸包静默丢帧的兼容性问题）→ 48k→16k 降采样 → 讯飞实时语音识别（RTASR），会话结束后触发 LLM 摘要；
- **未连蓝牙**：按 SPP 帧格式落盘 eMMC（`rec_<UTCms>.bin`），连上后自动回传。

即**任何时候都能录**，不依赖蓝牙；未连时落盘，连上即回传。

### 5.6 离线语音闭环（offline_recording/）

- **回传协议**：`FILE_START`（fileId+总大小+编解码参数+文件名）→ `FILE_DATA`（分片，每片一个完整 Opus 包）→ `FILE_END`（CRC32）；手机端按 fileId 暂存分片，收齐后做总大小 + CRC32 双重校验。
- **回执与自愈**：处理成功手机回 `FILE_ACK`，手表删除本地文件、列表页置「已同步」；超时未收到 ACK 的文件自动回退待同步重试。
- **离线列表覆盖层**：叠加在录音 App 之上，扫描 eMMC 录音文件按时间倒序展示「时间 + 时长 + 状态徽标（待同步/同步中/已同步）」，支持长按删除本地文件。
- **手机端持久化**：离线文件经讯飞录音文件转写大模型接口批量转写后，以 Room 数据库落库，App 内可回看；与实时 RTASR 零耦合、互不抢占配额。

### 5.7 小智 MCP 开启录音

MCP Server 新增工具 `self.recorder.open`（语音说「开启录音」即触发）：
1. 云端下发 `tools/call` → MCP Server 命中工具 → 回调经 `lv_async_call` 投到 LVGL 主线程；
2. 依次执行：关闭小智（同步释放 MIC，避免两模块抢麦克风）→ 等待网络资源释放 → 跳转录音 App 并自动开始录音 → 恢复渲染；
3. 录音页右滑退出回到应用列表。

## 六、AI Coding 使用说明

### 6.1 AI 辅助开发全流程

本作品全程使用 **Claude Code** 作为 AI 辅助开发工具，在以下环节深度协作：

#### 需求拆解与方案设计

- 使用 AI 分析小智 WebSocket 协议文档，拆解出 OTA 获取配置、wss 连接握手、音频上下行、MCP 工具调用等子任务
- AI 辅助设计系统架构：状态机（IDLE→RECORDING→THINKING→PLAYING）、线程模型（ws_recv + audio_uplink + 主线程 UI）
- 对比 MQTT+UDP vs WebSocket 两种协议方案，AI 提供嵌入式移植难度分析，最终选择 WebSocket 路线

#### 编码实现

- **自研 WebSocket 客户端**：AI 辅助基于 mbedTLS 实现 RFC6455 帧协议，约 300 行 C 代码覆盖文本帧/二进制帧收发
- **MCP Server 框架**：AI 设计了轻量级 MCP 工具注册/分发框架（`mcp_server.c`），支持 cJSON 参数解析和回调机制
- **MCP 工具注册**：AI 辅助将秒表、闹钟、运动等 12 个工具注册到 MCP Server，每个工具包含名称、描述、参数 schema 和回调函数
- **音频模块封装**：AI 辅助封装 media_recorder/media_player 的 buffer 模式，实现 Opus 编解码的透明调用
- **LVGL UI 开发**：AI 辅助设计手表界面（表盘、应用列表、语音对话界面、Listening 动画）

#### 调试与问题解决

- AI 辅助分析编译错误（up_nputs 重复定义、ffmpeg 头文件缺失等），提供补丁方案
- AI 辅助调试 WebSocket 握手失败、音频帧对齐等问题
- AI 辅助编写构建/烧录脚本（`build.sh`、`flash.sh`）

#### 文档与知识管理

- AI 辅助编写《编译详细指南》、《烧录详细指南》、《故障排除手册》
- AI 维护 CodeGraph 代码图谱，确保代码修改后图谱同步更新

### 6.2 AI 带来的实际帮助

| 环节 | 传统方式 | AI 辅助后 | 效率提升 |
|------|---------|----------|---------|
| 协议分析 | 手动阅读 WebSocket RFC + 小智协议文档，耗时数天 | AI 提取关键协议帧格式，自动生成 C 结构体定义 | ~3x |
| WebSocket 客户端 | 从零实现 RFC6455，易出帧解析 bug | AI 生成骨架代码 + 边界处理，仅需验证 | ~4x |
| MCP 工具集成 | 手动为每个应用编写注册代码 | AI 根据头文件自动生成 12 个工具注册代码 | ~5x |
| 编译调试 | 反复试错查日志 | AI 分析错误信息直接给出修复方案 | ~3x |
| 文档编写 | 手动整理笔记 | AI 结构化输出完整技术文档 | ~6x |

### 6.3 工具与技术栈

- **AI 工具**：Claude Code（Claude Opus 模型）
- **代码图谱**：CodeGraph（`.codegraph/` 目录，用于 xiaozhi_voice 模块的代码导航）
- **对话日志**：完整记录见 `logs/` 目录（按日期归档的 JSONL 格式）
