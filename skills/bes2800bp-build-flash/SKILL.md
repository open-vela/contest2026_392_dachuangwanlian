---
name: bes2800bp-build-flash
description: BEST1700_EP（BES2800BP，Cortex-M55 + HiFi4 DSP）开发板的编译与烧录操作指南。适用于 dev-ai-contest-2026 大赛分支。当用户需要编译 AP/APC1 固件、替换补丁文件、烧录板卡（增量/全量）、排查编译或烧录错误时使用。覆盖环境依赖、5 个必替换文件、双核编译命令、dldtool 烧录流程、官方厂包刷机及常见问题排查。
---

# BES2800BP 编译烧录

适用分支：`dev-ai-contest-2026`。板卡 best1700_ep 实际芯片为 BES2800BP（Cortex-M55 + HiFi4 DSP）。工具链 arm-none-eabi-gcc 随仓库提供（`prebuilts/gcc/linux-x86_64/arm-none-eabi/`），无需另装。

## 工作流总览

```
环境依赖 → 拉取代码 → 替换5个补丁文件 → 编译AP核 → 编译APC1核 → 烧录 → 串口验证
```

详细命令与参数见 [references/build-guide.md](references/build-guide.md) 和 [references/flash-guide.md](references/flash-guide.md)。常见错误见 [references/troubleshooting.md](references/troubleshooting.md)。

## 1. 环境依赖（一次性）

```bash
sudo apt install -y git cmake python3 build-essential curl bison flex cpio \
  gperf libncurses-dev libssl-dev libgmp-dev libmpfr-dev libmpc-dev libpulse-dev
sudo pip3 install kconfiglib
```

代码拉取参考：open-vela 官方 Ubuntu 快速开始文档（dev-ai-contest-2026 分支）。

## 2. 替换文件（编译前必须完成）

用附件中的文件直接替换仓库同名文件，共 5 个，均在 vendor/framework 层，不影响其他板子：

| # | 文件路径 | 作用 |
|---|---------|------|
| 1 | `prebuild/`（附件解压到根目录） | 解决恒玄开发板 make 构建缺目录 |
| 2 | `frameworks/multimedia/media/server/media_plugin.c` | 解决 ffmpeg 头文件缺失/链接错误 |
| 3 | `vendor/bes/boards/common/CMakeLists.txt` | 解决 up_nputs 重复定义（CMake 路径，关键） |
| 4 | `vendor/bes/chips/bes/Make.defs` | 解决 up_nputs 重复定义（Make 路径） |
| 5 | `vendor/bes/chips/bes/CMakeLists.txt` | 同上，条件加宽保持一致 |

> `apps/frameworks` 是指向 `../frameworks/` 的符号链接，只需替换 `frameworks/` 下一份。

## 3. 编译

在仓库根目录执行。**首次编译或替换过 CMakeLists 后，必须先 `rm -rf cmake_out/aos_evb_*` 整目录清理**（只删 CMakeCache.txt 不够，旧缓存会让修改不生效）。

### 3.1 AP 核（主核）

```bash
rm -rf cmake_out/aos_evb_ap
./build.sh vendor/bes/boards/best1700_ep/aos_evb/configs/ap --cmake -j8 \
  DSP_HIFI4_TRC_TO_MCU=1 CHIP_DMA_CFG_IDX=3 UTILS_ESHELL_BTRF_TEST=1 \
  ONLY_BT_DRIVE_INIT=1 NET_MUSIC_SUPPORT=1 NET_MUSIC_BASE_SUPPORT=1 \
  NET_MUSIC_SINK_SUPPORT=0 NET_MUSIC_CJSON_SUPPORT=0 \
  NET_WEBSVR_SUPPORT=1 WEBSVR_VERSION=v2 WEBSVR_FILE_SYS_SUPPORT=1
```

成功标志：`#### build completed successfully`。产物在 `cmake_out/aos_evb_ap/`：`nuttx_ap.bin`（~1.7MB 烧录镜像）、`nuttx_ap.elf`（调试用）、`nuttx_ap.map`。

### 3.2 APC1 核（副核）

```bash
rm -rf cmake_out/aos_evb_apc1
./build.sh vendor/bes/boards/best1700_ep/aos_evb/configs/apc1 --cmake -j8 \
  CVSD_BYPASS=1 RF_TRACE_BY_SPRINTF=1
```

产物在 `cmake_out/aos_evb_apc1/`：`nuttx_apc1.bin`、`nuttx_apc1.elf`。

### 3.3 HiFi4 核（音频核）

**本仓库无法编译**。HiFi4 固件由 BES 以预编译镜像随完整 SDK 交付，源码目录 `framework/services_hifi4/` 及预编译产物均不在本仓库。如需 hifi4，从官方厂包获取（见第 5 节）。

> 命令中的 `KEY=VALUE` 参数在 `--cmake` 模式下实际不传递给 ninja，对编译结果无影响，保留仅为与厂家脚本一致；真正生效的配置来自板级 defconfig。

## 4. 烧录

烧录工具：`prebuild/m1/dldtool`（Linux 版，仓库自带），引导器：`prebuild/programmer1700_dual.bin`。串口设备以实际枚举为准（下文以 `/dev/ttyUSB0` 为例），烧录需要 `sudo`。

### 4.1 场景 A：板上已有完整旧基线镜像，只更新 AP/APC1

```bash
cd <仓库根目录>
sudo ./prebuild/m1/dldtool --pgm-rate 2000000 /dev/ttyUSB0 \
  ./prebuild/programmer1700_dual.bin --set-dual-chip 1 \
  -M ./cmake_out/aos_evb_ap/nuttx_ap.bin \
  -M ./cmake_out/aos_evb_apc1/nuttx_apc1.bin
```

### 4.2 场景 B：裸板 / 全量烧录

其余 5 个镜像（bl/ota/bth/bthcp/audio）本仓库产不出来，需使用旧基线 `rtos/nuttx/` 下的同名文件（或向组委会/BES 获取完整包）：

```bash
cd <仓库根目录>
sudo ./prebuild/m1/dldtool --pgm-rate 2000000 /dev/ttyUSB0 \
  ./prebuild/programmer1700_dual.bin \
  -M <旧基线>/nuttx_bl.bin --set-dual-chip 1 \
  -M <旧基线>/nuttx_ota.bin \
  -M ./cmake_out/aos_evb_ap/nuttx_ap.bin \
  -M ./cmake_out/aos_evb_apc1/nuttx_apc1.bin \
  -M <旧基线>/nuttx_bth.bin \
  -M <旧基线>/nuttx_bthcp.bin \
  --addr 0x30D90000 <旧基线>/nuttx_audio.bin
```

### 4.3 烧录后验证

串口参数与目标镜像控制台一致（USB 枚举出的串口设备），开机后应能看到 NuttX 启动日志（`nsh` 提示符）。

## 5. 官方厂包全量刷机

BES2800 官方验证过屏幕/触摸正常的全套固件。下载 `rel_v1.0_bes_official.zip` 后在代码根目录解压，然后：

```bash
sudo ./prebuild/m1/dldtool --pgm-rate 2000000 /dev/ttyUSB0 \
  ./rel_v1.0_bes_official/programmer1700_dual.bin \
  -M ./rel_v1.0_bes_official/nuttx_bl.bin --set-dual-chip 1 \
  -M ./rel_v1.0_bes_official/nuttx_ota.bin \
  -M ./rel_v1.0_bes_official/nuttx_ap.bin \
  -M ./rel_v1.0_bes_official/nuttx_apc1.bin \
  -M ./rel_v1.0_bes_official/nuttx_bth.bin \
  -M ./rel_v1.0_bes_official/nuttx_bthcp.bin \
  --addr 0x30D90000 ./rel_v1.0_bes_official/nuttx_hifi.bin
```

## 6. 重要警告

- **不要直接运行官方脚本** `vendor/bes/readme/1700_ap.sh`——其 `cd "$SCRIPT_DIR"` 会切错工作目录导致报错，以本文档命令为准。
- **NTC 温度传感器问题**：新板子未焊接 NTC 时，刷机后串口会报 `pmu_ntc_monitor_init: fail! Invalid gpadc read!`。需用附件替换 `vendor/bes/boards/best1700_ep/aos_evb/configs/ap/libnx_bestbsp_ap.a`（先备份原文件），重新编译 AP 后只烧录 AP 核即可。或运行附件中的 `patch_pmu_ntc_bes1700.sh` 自动完成。
- 替换文件后必须整目录清理 `cmake_out`，否则旧 CMake 缓存导致修改不生效。

## 7. 自动化脚本

- `scripts/build.sh` — 一键编译 AP/APC1（自动清理缓存），用法：`./build.sh <repo_root> [ap|apc1|all]`
- `scripts/flash.sh` — 烧录辅助，支持增量/全量场景，用法：`./flash.sh <repo_root> <scenario> [tty_device]`

## 参考文档

- [references/build-guide.md](references/build-guide.md) — 编译详细说明与产物清单
- [references/flash-guide.md](references/flash-guide.md) — 烧录详细说明与厂包信息
- [references/troubleshooting.md](references/troubleshooting.md) — 常见问题排查表
