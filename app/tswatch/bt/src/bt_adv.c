/****************************************************************************
 * bt_adv.c - tswatch BLE 可连接广播
 *
 * 仅负责启动 BLE 广播(ADV_IND, 可连接), 让支持 BLE 的设备能发现/连接手表。
 * 经典蓝牙的可发现/可连接(scan mode)由 bt_manager 统一管理。
 *
 * 广播数据: Flags + SPP UUID(0x1101) + 扫描响应(完整本地名)。
 * 日志前缀: [bt_adv]
 ****************************************************************************/
#include <string.h>
#include <stdio.h>
#include "bluetooth.h"
#include "bt_adapter.h"
#include "bt_le_advertiser.h"
#include "advertiser_data.h"
#include "bt_adv.h"

#define TSW_LOG_TAG "bt_adv"
#include "tsw_log.h"

/* ---------------- 广播参数/回调 ---------------- */
static advertiser_callback_t g_adv_cbs;
static bt_advertiser_t *g_adv_handle = NULL;

static void on_adv_start(bt_advertiser_t *adv, uint8_t adv_id, uint8_t status)
{
    TSW_LOGI("advertising started, adv_id=%d status=%d", adv_id, status);
}

static void on_adv_stopped(bt_advertiser_t *adv, uint8_t adv_id)
{
    TSW_LOGW("advertising stopped, adv_id=%d", adv_id);
    if (g_adv_handle == adv) {
        g_adv_handle = NULL;
    }
}

/* 构造广播数据 + 扫描响应并发起广播 */
int bt_adv_start_ble(bt_instance_t *ins)
{
    ble_adv_params_t params = {0};
    advertiser_data_t *adv = NULL, *rsp = NULL;
    uint8_t *adv_data = NULL, *rsp_data = NULL;
    uint16_t adv_len = 0, rsp_len = 0;
    bt_uuid_t uuid;

    if (!ins) {
        TSW_LOGE("null instance");
        return -1;
    }
    if (g_adv_handle) {
        TSW_LOGI("advertising already running");
        return 0;
    }

    /* 可连接广播 */
    params.adv_type      = BT_LE_ADV_IND;
    params.interval      = 320;     /* 0.625ms * 320 = 200ms, 快速可发现 */
    params.tx_power      = 0;
    params.channel_map   = BT_LE_ADV_CHANNEL_DEFAULT;
    params.filter_policy = BT_LE_ADV_FILTER_WHITE_LIST_FOR_NONE;
    params.duration      = 0;
    params.own_addr_type = BT_LE_ADDR_TYPE_PUBLIC;

    /* 广播包: Flags + SPP UUID(0x1101) */
    adv = advertiser_data_new();
    if (!adv) {
        TSW_LOGE("advertiser_data_new fail");
        return -1;
    }
    advertiser_data_set_flags(adv, BT_AD_FLAG_DUAL_MODE | BT_AD_FLAG_GENERAL_DISCOVERABLE);
    bt_uuid16_create(&uuid, 0x1101);
    advertiser_data_add_service_uuid(adv, &uuid);
    adv_data = advertiser_data_build(adv, &adv_len);

    /* 扫描响应: 完整本地名(与 bt_manager 设置的 adapter 名保持一致) */
    rsp = advertiser_data_new();
    if (rsp) {
        advertiser_data_set_name(rsp, "TSWATCH");
        rsp_data = advertiser_data_build(rsp, &rsp_len);
    }

    g_adv_cbs.size = sizeof(g_adv_cbs);
    g_adv_cbs.on_advertising_start   = on_adv_start;
    g_adv_cbs.on_advertising_stopped = on_adv_stopped;

    g_adv_handle = bt_le_start_advertising(ins, &params,
                                           adv_data, adv_len,
                                           rsp_data, rsp_len,
                                           &g_adv_cbs);
    TSW_LOGI("bt_le_start_advertising handle=%p (adv_len=%d rsp_len=%d)",
             g_adv_handle, adv_len, rsp_len);

    if (adv)
        advertiser_data_free(adv);
    if (rsp)
        advertiser_data_free(rsp);

    return g_adv_handle ? 0 : -1;
}
