/****************************************************************************
 * bt_pair.c - tswatch 配对/绑定 回调(自动接受)
 *
 * 背景: bluetoothd 框架收到配对(SSP/legacy)请求后, 仅通过 on_pair_display /
 *   on_pair_request 回调上报给上层, 自身不自动接受; 必须由上层调用
 *   bt_device_set_pairing_confirmation 等确认 API 完成握手。
 *
 * 本模块为手表(无配对 UI)提供"自动接受任意配对请求(Just Works)"的回调,
 * 并通过 bt_pair_attach_cbs() 装配到 bt_manager 的统一适配器回调表中。
 *
 * 安全提示: 自动接受是任意设备的, 若产品需要配对白名单/鉴权, 在
 *   on_pair_display / on_pair_request 中增加过滤即可。
 *
 * 日志前缀: [bt_pair]
 ****************************************************************************/
#include <string.h>
#include <unistd.h>
#include "bluetooth.h"
#include "bt_adapter.h"
#include "bt_device.h"
#include "bt_pair.h"
#include "bt_pair_ui.h"   /* bt_pair_ui_hide_passkey: 配对结束收起配对码页面 */

#define TSW_LOG_TAG "bt_pair"
#include "tsw_log.h"

/* legacy PIN 配对的默认 PIN(常见默认 0000) */
#define BT_PAIR_DEFAULT_PIN "0000"

static bt_instance_t *g_pair_ins = NULL;

/* 配对码显示回调: UI 注册后, 数字比较配对时把 6 位配对码画到屏幕; 未注册则只打日志 */
static bt_pair_passkey_disp_cb_t g_passkey_disp_cb = NULL;

/* 本地地址转字符串(避免对蓝牙库地址打印函数的额外链接依赖) */
static const char *btpair_addr_str(const bt_address_t *addr, char *buf, int len)
{
    if (!addr || !buf || len < 18) {
        return (addr ? "<addr?>" : "<null>");
    }
    snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr->addr[0], addr->addr[1], addr->addr[2],
             addr->addr[3], addr->addr[4], addr->addr[5]);
    return buf;
}

/* 把配对码交给 UI 显示(或默认打日志) */
static void bt_pair_show_passkey(uint32_t passkey, const bt_address_t *addr)
{
    char str[18];
    btpair_addr_str(addr, str, sizeof(str));
    TSW_LOGI(">>> PAIRING PASSKEY %06u for %s <<<", passkey, str);
    TSW_LOGI(">>> 请在手表屏幕核对此码, 并在手机端点击 '配对/确认' <<<");
    if (g_passkey_disp_cb) {
        g_passkey_disp_cb(passkey, addr);
    }
}

void bt_pair_set_passkey_display_cb(bt_pair_passkey_disp_cb_t cb)
{
    g_passkey_disp_cb = cb;
}

/* ---------------- 回调实现 ---------------- */

/* 经典蓝牙 legacy 配对请求(无 SSP, 旧设备): 自动接受 */
static void on_pair_request(void *cookie, bt_address_t *addr)
{
    char str[18];

    (void)cookie;
    btpair_addr_str(addr, str, sizeof(str));
    TSW_LOGI("on_pair_request %s -> auto accept", str);
    if (g_pair_ins) {
        bt_device_pair_request_reply(g_pair_ins, addr, true);
    }
}

/* SSP / 配对显示请求: 根据配对类型自动接受 */
static void on_pair_display(void *cookie, bt_address_t *addr,
                           bt_transport_t transport,
                           bt_pair_type_t type, uint32_t passkey)
{
    bt_status_t ret = BT_STATUS_SUCCESS;
    char str[18];

    (void)cookie;
    btpair_addr_str(addr, str, sizeof(str));
    TSW_LOGI("on_pair_display %s transport=%d type=%d passkey=%u",
             str, transport, type, passkey);

    if (!g_pair_ins) {
        TSW_LOGE("on_pair_display: no bt instance, drop");
        return;
    }

    switch (type) {
    case PAIR_TYPE_PASSKEY_CONFIRMATION:
        /* 数字比较: 先把配对码显示出来, 再在手表侧确认(手机侧由用户点确认) */
        bt_pair_show_passkey(passkey, addr);
        ret = bt_device_set_pairing_confirmation(g_pair_ins, addr, transport, true);
        break;
    case PAIR_TYPE_CONSENT:
        /* 仅确认(无配对码可显示) */
        ret = bt_device_set_pairing_confirmation(g_pair_ins, addr, transport, true);
        break;
    case PAIR_TYPE_PASSKEY_ENTRY:
    case PAIR_TYPE_PASSKEY_NOTIFICATION:
        /* 提供 / 确认 passkey */
        ret = bt_device_set_pass_key(g_pair_ins, addr, transport, true, passkey);
        break;
    case PAIR_TYPE_PIN_CODE:
        /* legacy PIN: 用默认 PIN 回复 */
        ret = bt_device_set_pin_code(g_pair_ins, addr, true,
                                     (char *)BT_PAIR_DEFAULT_PIN,
                                     (int)strlen(BT_PAIR_DEFAULT_PIN));
        break;
    default:
        TSW_LOGW("on_pair_display: unknown type %d, fallback confirm", type);
        ret = bt_device_set_pairing_confirmation(g_pair_ins, addr, transport, true);
        break;
    }

    if (ret != BT_STATUS_SUCCESS) {
        TSW_LOGE("pair accept fail type=%d ret=%d", type, ret);
    }
}

/* 绑定(配对)状态变化: 仅日志 */
static void on_bond_state_changed(void *cookie, bt_address_t *addr,
                                 bt_transport_t transport,
                                 bond_state_t state, bool is_ctkd)
{
    const char *s = "?";
    char str[18];

    (void)cookie;
    btpair_addr_str(addr, str, sizeof(str));
    switch (state) {
    case BOND_STATE_NONE:    s = "NONE";    break;
    case BOND_STATE_BONDING: s = "BONDING"; break;
    case BOND_STATE_BONDED:  s = "BONDED";  break;
    default:                 break;
    }
    TSW_LOGI("bond_state_changed %s transport=%d state=%s ctkd=%d",
             str, transport, s, is_ctkd);

    /* 配对完成(BONDED)或取消/失败(NONE): 立即收起手表端配对码页面。
     * 之前这里只打日志, 页面只能等 30s 超时兜底才消失, 表现为"手机确认后页面不消失"。 */
    if (state == BOND_STATE_BONDED || state == BOND_STATE_NONE) {
        bt_pair_ui_hide_passkey();
    }
}

/* ---------------- 装配到统一回调表 ---------------- */
void bt_pair_attach_cbs(bt_instance_t *ins, adapter_callbacks_t *cbs)
{
    g_pair_ins = ins;

    /* 设置 IO 能力为"可显示+可确认", 让栈协商出数字比较(显示配对码)而非 Just Works。
     * 经典用 DISPLAYYESNO; LE 用 SMP DisplayYesNo(值 1)。 */
    bt_adapter_set_io_capability(ins, BT_IO_CAPABILITY_DISPLAYYESNO);
    bt_adapter_set_le_io_capability(ins, 1);

    cbs->on_pair_request       = on_pair_request;
    cbs->on_pair_display       = on_pair_display;
    cbs->on_bond_state_changed = on_bond_state_changed;
    /* on_connection_state_changed 由 bt_manager 填写 */
    TSW_LOGI("pair cbs attached (auto-accept pairing)");
}
