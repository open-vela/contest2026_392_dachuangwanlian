#!/bin/bash
# BES2800BP (best1700_ep) 编译脚本
# 用法: ./build.sh <repo_root> [ap|apc1|all]
#   repo_root - openvela 仓库根目录
#   target    - 编译目标: ap (默认), apc1, all (两者都编译)

set -e

REPO_ROOT="${1:?用法: $0 <repo_root> [ap|apc1|all]}"
TARGET="${2:-ap}"

if [ ! -d "$REPO_ROOT" ]; then
    echo "错误: 仓库目录不存在: $REPO_ROOT"
    exit 1
fi

cd "$REPO_ROOT"

build_ap() {
    echo "=== 编译 AP 核（主核）==="
    echo "清理旧缓存..."
    rm -rf cmake_out/aos_evb_ap
    ./build.sh vendor/bes/boards/best1700_ep/aos_evb/configs/ap --cmake -j8 \
        DSP_HIFI4_TRC_TO_MCU=1 CHIP_DMA_CFG_IDX=3 UTILS_ESHELL_BTRF_TEST=1 \
        ONLY_BT_DRIVE_INIT=1 NET_MUSIC_SUPPORT=1 NET_MUSIC_BASE_SUPPORT=1 \
        NET_MUSIC_SINK_SUPPORT=0 NET_MUSIC_CJSON_SUPPORT=0 \
        NET_WEBSVR_SUPPORT=1 WEBSVR_VERSION=v2 WEBSVR_FILE_SYS_SUPPORT=1
    echo "AP 编译完成，产物: cmake_out/aos_evb_ap/nuttx_ap.bin"
}

build_apc1() {
    echo "=== 编译 APC1 核（副核）==="
    echo "清理旧缓存..."
    rm -rf cmake_out/aos_evb_apc1
    ./build.sh vendor/bes/boards/best1700_ep/aos_evb/configs/apc1 --cmake -j8 \
        CVSD_BYPASS=1 RF_TRACE_BY_SPRINTF=1
    echo "APC1 编译完成，产物: cmake_out/aos_evb_apc1/nuttx_apc1.bin"
}

case "$TARGET" in
    ap)
        build_ap
        ;;
    apc1)
        build_apc1
        ;;
    all)
        build_ap
        build_apc1
        ;;
    *)
        echo "错误: 未知目标 '$TARGET'，可选: ap, apc1, all"
        exit 1
        ;;
esac

echo "=== 全部完成 ==="
