# 常见问题排查

## 编译错误

| 症状 | 原因 | 解决 |
|------|------|------|
| `fatal error: libavutil/mem.h` | 文件 2（media_plugin.c）未替换 | 替换 `frameworks/multimedia/media/server/media_plugin.c` |
| `multiple definition of 'up_nputs'` | 文件 3/4/5 未替换，或替换后未清理缓存 | 替换 `vendor/bes/boards/common/CMakeLists.txt`、`vendor/bes/chips/bes/Make.defs`、`vendor/bes/chips/bes/CMakeLists.txt`，然后 `rm -rf cmake_out/aos_evb_*` 重编 |
| 替换了文件但行为不变 | 旧 CMake 缓存未清理 | `rm -rf cmake_out/aos_evb_ap` 整目录删除（只删 CMakeCache.txt 不够） |
| `unknown statement 'osource'` | 缺少 kconfiglib | `sudo pip3 install kconfiglib` |
| Kconfig/menuconfig 报错 | 缺少 ncurses 开发库 | `sudo apt install -y libncurses-dev` |

## 烧录与运行问题

| 症状 | 原因 | 解决 |
|------|------|------|
| 刷机后板子无法点亮，串口显示 `pmu_ntc_monitor_init: fail! Invalid gpadc read!` | 板子上没焊接 NTC 温度传感器 | 用附件替换 `vendor/bes/boards/best1700_ep/aos_evb/configs/ap/libnx_bestbsp_ap.a`（先备份原文件），重新编译 AP，只烧录 AP 核。或运行附件中的 `patch_pmu_ntc_bes1700.sh` 自动完成备份和替换 |
| dldtool 报串口权限错误 | 当前用户无串口访问权限 | 使用 `sudo` 运行，或将用户加入 `dialout` 组 |
| 烧录失败 / 无响应 | 板卡未进入编程态或串口设备名不对 | 确认 USB 连接，用 `ls /dev/ttyUSB*` 查看实际串口设备名 |
| 官方脚本 `1700_ap.sh` 报错 | 脚本中 `cd "$SCRIPT_DIR"` 切错工作目录 | 不要运行该脚本，以本文档命令为准 |

## NTC 温度传感器问题详细说明

新板子（未焊接 NTC 温度传感器）刷机后会出现以下错误：

```
pmu_ntc_monitor_init: fail! Invalid gpadc read!
```

**解决步骤：**

1. 备份原文件：
   ```bash
   cp vendor/bes/boards/best1700_ep/aos_evb/configs/ap/libnx_bestbsp_ap.a \
      vendor/bes/boards/best1700_ep/aos_evb/configs/ap/libnx_bestbsp_ap.a.bak
   ```

2. 用附件中的 `libnx_bestbsp_ap.a` 替换原文件

3. 重新编译 AP 核（需清理缓存）：
   ```bash
   rm -rf cmake_out/aos_evb_ap
   ./build.sh vendor/bes/boards/best1700_ep/aos_evb/configs/ap --cmake -j8 \
     DSP_HIFI4_TRC_TO_MCU=1 CHIP_DMA_CFG_IDX=3 UTILS_ESHELL_BTRF_TEST=1 \
     ONLY_BT_DRIVE_INIT=1 NET_MUSIC_SUPPORT=1 NET_MUSIC_BASE_SUPPORT=1 \
     NET_MUSIC_SINK_SUPPORT=0 NET_MUSIC_CJSON_SUPPORT=0 \
     NET_WEBSVR_SUPPORT=1 WEBSVR_VERSION=v2 WEBSVR_FILE_SYS_SUPPORT=1
   ```

4. 只烧录 AP 核（场景 A 命令）

或者直接运行附件中的 `patch_pmu_ntc_bes1700.sh` 脚本自动完成备份和修改。

## 排查流程建议

1. 编译错误 → 先确认 5 个替换文件是否全部到位
2. 替换后行为不变 → 先执行 `rm -rf cmake_out/aos_evb_*` 再重编
3. 缺少 Python/系统依赖 → 对照环境依赖清单安装
4. 烧录后无法启动 → 先排查 NTC 问题，再检查串口参数和镜像完整性
5. 不确定用哪个烧录场景 → 板上有旧基线用场景 A，裸板用场景 B 或官方厂包
