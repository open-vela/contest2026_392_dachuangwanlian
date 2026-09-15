# 编译详细指南

## 目录

1. [环境依赖](#1-环境依赖)
2. [代码拉取](#2-代码拉取)
3. [替换文件清单](#3-替换文件清单)
4. [AP 核编译](#4-ap-核编译)
5. [APC1 核编译](#5-apc1-核编译)
6. [HiFi4 核说明](#6-hifi4-核说明)
7. [产物清单](#7-产物清单)
8. [注意事项](#8-注意事项)

## 1. 环境依赖

一次性安装：

```bash
sudo apt install -y git cmake python3 build-essential curl bison flex cpio \
  gperf libncurses-dev libssl-dev libgmp-dev libmpfr-dev libmpc-dev libpulse-dev
sudo pip3 install kconfiglib
```

工具链 arm-none-eabi-gcc 已随仓库提供，路径：`prebuilts/gcc/linux-x86_64/arm-none-eabi/`，无需额外安装。

## 2. 代码拉取

分支：`dev-ai-contest-2026`（大赛分支）。

参考文档：open-vela 官方 Ubuntu 快速开始
https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/quickstart/openvela_ubuntu_quick_start.md

## 3. 替换文件清单

编译前必须用附件替换以下 5 个文件。所有修改均在 vendor/framework 层，不影响其他板子（如 goldfish）。

| # | 文件路径 | 作用 | 附件名 |
|---|---------|------|--------|
| 1 | `prebuild/`（解压到仓库根目录） | 解决恒玄开发板 make 构建缺目录 | `prebuild.tar` |
| 2 | `frameworks/multimedia/media/server/media_plugin.c` | 解决 ffmpeg 头文件缺失 / 链接错误 | `media_plugin.c` |
| 3 | `vendor/bes/boards/common/CMakeLists.txt` | 解决 up_nputs 重复定义（CMake 路径，关键） | `CMakeLists.txt` |
| 4 | `vendor/bes/chips/bes/Make.defs` | 解决 up_nputs 重复定义（Make 路径） | `Make.defs` |
| 5 | `vendor/bes/chips/bes/CMakeLists.txt` | 同上，条件加宽保持一致 | `CMakeLists.txt` |

> `apps/frameworks` 是指向 `../frameworks/` 的符号链接，只需替换 `frameworks/` 下这一份。

## 4. AP 核编译

在仓库根目录执行。首次编译或替换过 CMakeLists 后必须先清理：

```bash
rm -rf cmake_out/aos_evb_ap
./build.sh vendor/bes/boards/best1700_ep/aos_evb/configs/ap --cmake -j8 \
  DSP_HIFI4_TRC_TO_MCU=1 CHIP_DMA_CFG_IDX=3 UTILS_ESHELL_BTRF_TEST=1 \
  ONLY_BT_DRIVE_INIT=1 NET_MUSIC_SUPPORT=1 NET_MUSIC_BASE_SUPPORT=1 \
  NET_MUSIC_SINK_SUPPORT=0 NET_MUSIC_CJSON_SUPPORT=0 \
  NET_WEBSVR_SUPPORT=1 WEBSVR_VERSION=v2 WEBSVR_FILE_SYS_SUPPORT=1
```

成功标志：输出 `#### build completed successfully`。

### 参数说明

| 参数 | 值 | 说明 |
|------|-----|------|
| `--cmake` | - | 使用 CMake 构建模式 |
| `-j8` | - | 并行编译任务数 |
| `DSP_HIFI4_TRC_TO_MCU` | 1 | DSP trace 输出到 MCU |
| `CHIP_DMA_CFG_IDX` | 3 | DMA 配置索引 |
| `UTILS_ESHELL_BTRF_TEST` | 1 | 启用蓝牙 RF 测试 shell |
| `ONLY_BT_DRIVE_INIT` | 1 | 仅初始化蓝牙驱动 |
| `NET_MUSIC_SUPPORT` | 1 | 网络音乐支持 |
| `NET_MUSIC_BASE_SUPPORT` | 1 | 网络音乐基础支持 |
| `NET_MUSIC_SINK_SUPPORT` | 0 | 关闭音乐 Sink |
| `NET_MUSIC_CJSON_SUPPORT` | 0 | 关闭音乐 cJSON |
| `NET_WEBSVR_SUPPORT` | 1 | 启用 Web 服务器 |
| `WEBSVR_VERSION` | v2 | Web 服务器版本 |
| `WEBSVR_FILE_SYS_SUPPORT` | 1 | Web 服务器文件系统支持 |

> 注意：`--cmake` 模式下 `KEY=VALUE` 参数实际不传递给 ninja，对编译结果无影响，保留仅为与厂家脚本一致。真正生效的配置来自板级 defconfig。

## 5. APC1 核编译

```bash
rm -rf cmake_out/aos_evb_apc1
./build.sh vendor/bes/boards/best1700_ep/aos_evb/configs/apc1 --cmake -j8 \
  CVSD_BYPASS=1 RF_TRACE_BY_SPRINTF=1
```

### 参数说明

| 参数 | 值 | 说明 |
|------|-----|------|
| `CVSD_BYPASS` | 1 | CVSD 旁路 |
| `RF_TRACE_BY_SPRINTF` | 1 | RF trace 通过 sprintf 输出 |

## 6. HiFi4 核说明

**本仓库无法编译 HiFi4 固件。**

- HiFi4 固件由 BES 以预编译镜像随完整 SDK 交付
- 所需源码目录 `framework/services_hifi4/` 及预编译产物均不在本仓库
- 如需 hifi4 镜像，从官方厂包 `rel_v1.0_bes_official.zip` 获取（文件名为 `nuttx_hifi.bin`）

## 7. 产物清单

### AP 核产物（`cmake_out/aos_evb_ap/`）

| 文件 | 说明 |
|------|------|
| `nuttx_ap.bin` | 烧录镜像（~1.7MB） |
| `nuttx_ap.elf` | 带符号 ELF（调试用） |
| `nuttx_ap.map` | 链接 map 文件 |

### APC1 核产物（`cmake_out/aos_evb_apc1/`）

| 文件 | 说明 |
|------|------|
| `nuttx_apc1.bin` | 烧录镜像 |
| `nuttx_apc1.elf` | 带符号 ELF（调试用） |

## 8. 注意事项

1. **CMake 缓存清理**：替换任何 CMakeLists.txt 或 Make.defs 后，必须 `rm -rf cmake_out/aos_evb_*` 整目录删除。只删 `CMakeCache.txt` 不够，旧缓存会让修改不生效。
2. **不要运行官方脚本**：`vendor/bes/readme/1700_ap.sh` 中 `cd "$SCRIPT_DIR"` 会切错工作目录导致报错，以本文档命令为准。
3. **NTC 问题**：新板子未焊接 NTC 温度传感器时，AP 编译需替换 `libnx_bestbsp_ap.a`，详见 troubleshooting。
