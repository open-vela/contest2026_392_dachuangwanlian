# 烧录详细指南

## 目录

1. [烧录工具与前提](#1-烧录工具与前提)
2. [场景 A：增量烧录（只更新 AP/APC1）](#2-场景-a增量烧录只更新-apapc1)
3. [场景 B：全量烧录（裸板）](#3-场景-b全量烧录裸板)
4. [官方厂包全量刷机](#4-官方厂包全量刷机)
5. [烧录后验证](#5-烧录后验证)
6. [镜像地址说明](#6-镜像地址说明)

## 1. 烧录工具与前提

| 工具 | 路径 | 说明 |
|------|------|------|
| dldtool | `prebuild/m1/dldtool` | 烧录工具（Linux 版，仓库自带） |
| programmer | `prebuild/programmer1700_dual.bin` | 烧录引导器（把芯片置为编程态） |

前提条件：
- 串口设备以实际枚举为准（示例使用 `/dev/ttyUSB0`）
- 烧录需要 `sudo` 权限
- 板卡通过 USB 连接，串口已枚举

dldtool 通用参数：
- `--pgm-rate 2000000`：编程速率 2Mbps
- `--set-dual-chip 1`：双核模式（必须在第一个 `-M` 之前设置）
- `-M <file>`：烧录镜像（按顺序指定多个）
- `--addr <hex> <file>`：指定地址烧录（用于 audio/hifi 镜像）

## 2. 场景 A：增量烧录（只更新 AP/APC1）

适用条件：板上已有完整旧基线镜像，只需更新自行编译的 AP 和 APC1 固件。

```bash
cd <仓库根目录>
sudo ./prebuild/m1/dldtool --pgm-rate 2000000 /dev/ttyUSB0 \
  ./prebuild/programmer1700_dual.bin --set-dual-chip 1 \
  -M ./cmake_out/aos_evb_ap/nuttx_ap.bin \
  -M ./cmake_out/aos_evb_apc1/nuttx_apc1.bin
```

此命令仅烧录 AP 和 APC1 两个镜像，板上原有的 bl/ota/bth/bthcp/audio 保持不变。

## 3. 场景 B：全量烧录（裸板）

适用条件：全新裸板，或需要刷入完整固件。

本仓库只能编译出 AP 和 APC1 两个镜像，其余 5 个镜像（bl/ota/bth/bthcp/audio）需使用旧基线 `rtos/nuttx/` 下的同名文件，或向组委会/BES 获取完整包。

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

### 全量烧录镜像顺序

| 顺序 | 镜像 | 来源 | 特殊参数 |
|------|------|------|---------|
| 1 | `nuttx_bl.bin` | 旧基线 | `--set-dual-chip 1`（紧跟其后） |
| 2 | `nuttx_ota.bin` | 旧基线 | - |
| 3 | `nuttx_ap.bin` | 本仓库编译 | - |
| 4 | `nuttx_apc1.bin` | 本仓库编译 | - |
| 5 | `nuttx_bth.bin` | 旧基线 | - |
| 6 | `nuttx_bthcp.bin` | 旧基线 | - |
| 7 | `nuttx_audio.bin` | 旧基线 | `--addr 0x30D90000` |

## 4. 官方厂包全量刷机

BES2800 官方验证过屏幕/触摸正常的全套固件。

### 获取厂包

附件：`rel_v1.0_bes_official.zip`，下载后在代码根目录解压，得到 `rel_v1.0_bes_official/` 目录。

厂包含以下文件：
- `programmer1700_dual.bin`
- `nuttx_bl.bin`
- `nuttx_ota.bin`
- `nuttx_ap.bin`
- `nuttx_apc1.bin`
- `nuttx_bth.bin`
- `nuttx_bthcp.bin`
- `nuttx_hifi.bin`

### 刷机命令

```bash
cd <仓库根目录>
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

> 注意：厂包使用自带的 `programmer1700_dual.bin`（在 `rel_v1.0_bes_official/` 目录下），而非 `prebuild/` 下的。

## 5. 烧录后验证

1. 连接串口（USB 枚举出的串口设备），串口参数与目标镜像控制台一致
2. 复位或重新上电板卡
3. 观察串口输出，应能看到 NuttX 启动日志
4. 启动完成后应出现 `nsh` 提示符

如果串口无输出或报错，参考 [troubleshooting.md](troubleshooting.md)。

## 6. 镜像地址说明

- 大部分镜像通过 `-M` 参数按默认地址烧录
- audio/hifi 镜像需要显式指定地址 `--addr 0x30D90000`
- `--set-dual-chip 1` 必须在第一个 `-M` 参数之前设置，用于启用双核模式
