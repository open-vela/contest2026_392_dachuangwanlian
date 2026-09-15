#!/bin/bash
# BES2800BP (best1700_ep) 烧录脚本
# 用法: ./flash.sh <repo_root> <scenario> [tty_device]
#   repo_root  - openvela 仓库根目录
#   scenario   - 烧录场景: incremental (增量,只更新AP/APC1), full (全量,需旧基线), official (官方厂包)
#   tty_device - 串口设备, 默认 /dev/ttyUSB0
#
# 环境变量:
#   OLD_BASELINE - 全量烧录时旧基线目录(含 nuttx_bl.bin 等), scenario=full 时必填
#   OFFICIAL_PKG - 官方厂包目录, 默认 rel_v1.0_bes_official (scenario=official)

set -e

REPO_ROOT="${1:?用法: $0 <repo_root> <incremental|full|official> [tty_device]}"
SCENARIO="${2:?用法: $0 <repo_root> <incremental|full|official> [tty_device]}"
TTY="${3:-/dev/ttyUSB0}"

if [ ! -d "$REPO_ROOT" ]; then
    echo "错误: 仓库目录不存在: $REPO_ROOT"
    exit 1
fi

DLDTOOL="$REPO_ROOT/prebuild/m1/dldtool"
PROGRAMMER="$REPO_ROOT/prebuild/programmer1700_dual.bin"

if [ ! -x "$DLDTOOL" ]; then
    echo "错误: dldtool 不存在或不可执行: $DLDTOOL"
    exit 1
fi
if [ ! -f "$PROGRAMMER" ]; then
    echo "错误: programmer 不存在: $PROGRAMMER"
    exit 1
fi
if [ ! -e "$TTY" ]; then
    echo "警告: 串口设备不存在: $TTY (请确认实际设备名，用 ls /dev/ttyUSB* 查看)"
fi

cd "$REPO_ROOT"

flash_incremental() {
    echo "=== 增量烧录（只更新 AP/APC1）==="
    AP_BIN="$REPO_ROOT/cmake_out/aos_evb_ap/nuttx_ap.bin"
    APC1_BIN="$REPO_ROOT/cmake_out/aos_evb_apc1/nuttx_apc1.bin"
    if [ ! -f "$AP_BIN" ]; then
        echo "错误: AP 镜像不存在，请先编译: $AP_BIN"
        exit 1
    fi
    if [ ! -f "$APC1_BIN" ]; then
        echo "错误: APC1 镜像不存在，请先编译: $APC1_BIN"
        exit 1
    fi
    sudo "$DLDTOOL" --pgm-rate 2000000 "$TTY" \
        "$PROGRAMMER" --set-dual-chip 1 \
        -M "$AP_BIN" \
        -M "$APC1_BIN"
    echo "增量烧录完成"
}

flash_full() {
    echo "=== 全量烧录（裸板）==="
    OLD="${OLD_BASELINE:?全量烧录需设置 OLD_BASELINE 环境变量指向旧基线目录}"
    AP_BIN="$REPO_ROOT/cmake_out/aos_evb_ap/nuttx_ap.bin"
    APC1_BIN="$REPO_ROOT/cmake_out/aos_evb_apc1/nuttx_apc1.bin"

    for f in nuttx_bl.bin nuttx_ota.bin nuttx_bth.bin nuttx_bthcp.bin nuttx_audio.bin; do
        if [ ! -f "$OLD/$f" ]; then
            echo "错误: 旧基线缺少文件: $OLD/$f"
            exit 1
        fi
    done
    if [ ! -f "$AP_BIN" ]; then
        echo "错误: AP 镜像不存在，请先编译: $AP_BIN"
        exit 1
    fi
    if [ ! -f "$APC1_BIN" ]; then
        echo "错误: APC1 镜像不存在，请先编译: $APC1_BIN"
        exit 1
    fi

    sudo "$DLDTOOL" --pgm-rate 2000000 "$TTY" \
        "$PROGRAMMER" \
        -M "$OLD/nuttx_bl.bin" --set-dual-chip 1 \
        -M "$OLD/nuttx_ota.bin" \
        -M "$AP_BIN" \
        -M "$APC1_BIN" \
        -M "$OLD/nuttx_bth.bin" \
        -M "$OLD/nuttx_bthcp.bin" \
        --addr 0x30D90000 "$OLD/nuttx_audio.bin"
    echo "全量烧录完成"
}

flash_official() {
    echo "=== 官方厂包全量烧录 ==="
    PKG="${OFFICIAL_PKG:-$REPO_ROOT/rel_v1.0_bes_official}"
    if [ ! -d "$PKG" ]; then
        echo "错误: 官方厂包目录不存在: $PKG"
        echo "请先下载 rel_v1.0_bes_official.zip 并在仓库根目录解压"
        exit 1
    fi
    OFFICIAL_PROGRAMMER="$PKG/programmer1700_dual.bin"
    for f in programmer1700_dual.bin nuttx_bl.bin nuttx_ota.bin nuttx_ap.bin \
             nuttx_apc1.bin nuttx_bth.bin nuttx_bthcp.bin nuttx_hifi.bin; do
        if [ ! -f "$PKG/$f" ]; then
            echo "错误: 厂包缺少文件: $PKG/$f"
            exit 1
        fi
    done

    sudo "$DLDTOOL" --pgm-rate 2000000 "$TTY" \
        "$OFFICIAL_PROGRAMMER" \
        -M "$PKG/nuttx_bl.bin" --set-dual-chip 1 \
        -M "$PKG/nuttx_ota.bin" \
        -M "$PKG/nuttx_ap.bin" \
        -M "$PKG/nuttx_apc1.bin" \
        -M "$PKG/nuttx_bth.bin" \
        -M "$PKG/nuttx_bthcp.bin" \
        --addr 0x30D90000 "$PKG/nuttx_hifi.bin"
    echo "官方厂包烧录完成"
}

case "$SCENARIO" in
    incremental|incr)
        flash_incremental
        ;;
    full)
        flash_full
        ;;
    official)
        flash_official
        ;;
    *)
        echo "错误: 未知场景 '$SCENARIO'，可选: incremental, full, official"
        exit 1
        ;;
esac

echo "=== 烧录完成，请复位板卡并通过串口验证 ==="
