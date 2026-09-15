# 基于 BES 2800 BP 的联网语音智能体手表

## 一、作品简介

一款面向日常效率场景、运行在 BES 2800 BP 智能手表上的**端云协同语音 AI 智能体**，让用户通过自然语言对话即可完成信息检索与设备控制，解决小屏穿戴设备"交互繁琐、信息获取慢"的核心痛点。用户对着手表说出自然语言指令，云端 ASR 识别语音后，LLM 通过 MCP（Model Context Protocol）工具调用驱动手表端执行对应操作（闹钟、运动、秒表、计时器、心率、睡眠监测等），无需触屏，全程语音交互。

## 二、选题方向

**手表应用创新**

选择理由：本作品核心在于将 MCP 工具调用机制落地到嵌入式手表端侧，通过端云协同架构，让云端 LLM 理解用户自然语言意图并自动路由到手表端预注册的 MCP 工具执行操作，实现真正的 AI 语音闭环控制。

## 三、目录结构

```
contest2026_392_dachuangwanlian/
├── app/
│   ├── tswatch/                    — 主应用（手表核心代码）
│   │   ├── xiaozhi_voice/          — 小智 AI 语音客户端 + MCP Server
│   │   ├── stopwatch/              — 秒表应用（支持 MCP 语音启停/重置）
│   │   ├── clock/                  — 闹钟应用（支持 MCP 语音添加/删除闹钟）
│   │   ├── exercise/               — 运动应用（支持 MCP 语音开始/停止运动）
│   │   ├── heart/                  — 心率监测应用
│   │   ├── sleep/                  — 睡眠监测应用
│   │   ├── timer/                  — 计时器应用
│   │   ├── settings/               — 设置应用
│   │   ├── main_page/              — 表盘主页
│   │   ├── applist/                — 应用列表
│   │   ├── utils/                  — 工具函数（字体管理等）
│   │   └── res/                    — 图片/资源文件
│   └── hello_app/                  — 应用形态样例（占位骨架）
├── board/
│   ├── contest_board/              — 板级适配（defconfig 等）
│   └── best1700_ep/                — BES1700 EP 板级支持（aos_evb 配置）
├── quickapp/
│   └── hello_quickapp/             — 快应用样例（占位骨架）
├── skills/
│   └── bes2800bp-build-flash/      — AI 辅助构建/烧录技能
├── prebuild/                       — 恒玄开发板cmake编译必备脚本和配置
├── logs/                           — AI Coding 对话日志
│   └── liyc0623/                   — 按日期归档的 JSONL 日志
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

### 4.3 替换补丁文件

编译前需用附件替换以下文件（所有修改在 vendor/framework 层）：

| # | 文件路径 | 作用 |
|---|---------|------|
| 1 | `prebuild/`（解压到仓库根目录） | 恒玄开发板构建所需目录 |
| 2 | `frameworks/multimedia/media/server/media_plugin.c` | 修复 ffmpeg 头文件缺失 |
| 3 | `vendor/bes/boards/common/CMakeLists.txt` | 修复 up_nputs 重复定义 |
| 4 | `vendor/bes/chips/bes/Make.defs` | 修复 up_nputs 重复定义 |
| 5 | `vendor/bes/chips/bes/CMakeLists.txt` | 条件加宽保持一致 |

### 4.4 编译

**AP 核（主应用）：**

```bash
cd ..
rm -rf cmake_out/aos_evb_ap
./build.sh vendor/bes/boards/best1700_ep/aos_evb/configs/ap --cmake -j8
```

成功标志：输出 `#### build completed successfully`。

产物：`cmake_out/aos_evb_ap/nuttx_ap.bin`（约 1.7MB）。

### 4.5 烧录

**增量烧录（仅更新 AP + APC1）：**

```bash
cd ..
sudo ./prebuild/m1/dldtool --pgm-rate 2000000 /dev/ttyUSB0 \
  ./prebuild/programmer1700_dual.bin --set-dual-chip 1 \
  -M ./cmake_out/aos_evb_ap/nuttx_ap.bin \
  -M ./cmake_out/aos_evb_apc1/nuttx_apc1.bin
```

**全量烧录（裸板）：** 需额外提供 bl/ota/bth/bthcp/audio 镜像，详见 `skills/bes2800bp-build-flash/references/flash-guide.md`。

### 4.6 运行验证

1. 串口连接板卡（USB 枚举设备），打开串口终端
2. 复位或重新上电板卡
3. 观察 NuttX 启动日志，出现 `nsh` 提示符即启动成功
4. LVGL 界面显示手表表盘，点击小智语音按钮开始对话

### 4.7 语音交互示例

对着板子麦克风说：
- "帮我定一个明早7点的闹钟" → MCP 调用 `self.alarm.add`
- "开始跑步" → MCP 调用 `self.exercise.start`
- "打开秒表" → MCP 调用 `self.stopwatch.start`
- "暂停" → MCP 调用 `self.stopwatch.pause`
- "打开心率" → MCP 调用 `self.heart.open`
- "打开设置" → MCP 调用 `self.settings.open`

## 五、AI Coding 使用说明

### 5.1 AI 辅助开发全流程

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

### 5.2 AI 带来的实际帮助

| 环节 | 传统方式 | AI 辅助后 | 效率提升 |
|------|---------|----------|---------|
| 协议分析 | 手动阅读 WebSocket RFC + 小智协议文档，耗时数天 | AI 提取关键协议帧格式，自动生成 C 结构体定义 | ~3x |
| WebSocket 客户端 | 从零实现 RFC6455，易出帧解析 bug | AI 生成骨架代码 + 边界处理，仅需验证 | ~4x |
| MCP 工具集成 | 手动为每个应用编写注册代码 | AI 根据头文件自动生成 12 个工具注册代码 | ~5x |
| 编译调试 | 反复试错查日志 | AI 分析错误信息直接给出修复方案 | ~3x |
| 文档编写 | 手动整理笔记 | AI 结构化输出完整技术文档 | ~6x |

### 5.3 工具与技术栈

- **AI 工具**：Claude Code（Claude Opus 模型）
- **代码图谱**：CodeGraph（`.codegraph/` 目录，用于 xiaozhi_voice 模块的代码导航）
- **对话日志**：完整记录见 `logs/` 目录（按日期归档的 JSONL 格式）
