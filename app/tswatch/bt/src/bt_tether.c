/****************************************************************************
 * bt_tether.c - 手表蓝牙上网(PAN / BNEP)实现
 *
 * 数据流:
 *   1. bt_tether_init(): 注册 PAN 回调(连接状态 + 网卡状态), 由 bt_manager 调用。
 *   2. 用户在设置页打开"蓝牙上网" -> bt_tether_start():
 *        手表作为 PANU(src) 连接手机(dst=NAP)。
 *        手机地址取自已"已连接"或"已配对"的蓝牙设备(由蓝牙栈直接查询,
 *        与 SPP 数据通道无关 —— 蓝牙共享不需要 SPP 先连)。
 *   3. 栈回调 pan_connection_state_cb: 通知 PAN 连接建立/断开。
 *   4. 栈回调 pan_netif_state_cb(PAN_STATE_ENABLED): bt-pan 网卡 up,
 *        此时调用 netlib_obtain_ipv4addr() 经 DHCP 从手机 NAP 取得 IP,
 *        该函数会自动设置 IP / 掩码 / 默认网关(默认路由指向手机)。
 *   5. 成功后置 g_connected=true 并通过状态监听通知 UI。
 *
 * 注意: 所有回调在 bluetoothd IPC 线程触发; netlib 是网络栈、非 UI,
 * 可直接调用; 仅 UI 刷新需经 lv_async_call(由监听方处理)。
 *
 * 本模块随固件默认编译, 不依赖任何编译期开关。
 *
 * 日志前缀: [bt_tether]
 ****************************************************************************/
#include <string.h>

#include "bluetooth.h"
#include "bt_adapter.h"     /* bt_adapter_get_connected/bonded_devices, BT_TRANSPORT_BREDR */
#include "bt_device.h"      /* profile_connection_state_t, bt_device_connect_all_profile */
#include "bt_pan.h"         /* bt_pan_* / pan_ *_t / PAN_ROLE_* */
#include <netutils/netlib.h>/* netlib_obtain_ipv4addr */
#include "bt_tether.h"

#define TSW_LOG_TAG "bt_tether"
#include "tsw_log.h"

#define BT_TETHER_IFNAME_DEFAULT "bt-pan"

static bt_instance_t            *g_ins        = NULL;
static void                     *g_pan_cookie = NULL;
static bool                      g_inited     = false;
static bool                      g_connected  = false;  /* PAN 已联网 */
static char                      g_ifname[32] = BT_TETHER_IFNAME_DEFAULT;
static bt_tether_state_cb_t      g_state_cb   = NULL;

/* 地址转字符串 */
static const char *tt_addr_str(const bt_address_t *addr, char *buf, int len)
{
    if (!addr || !buf || len < 18)
        return (addr ? "<addr?>" : "<null>");
    snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr->addr[0], addr->addr[1], addr->addr[2],
             addr->addr[3], addr->addr[4], addr->addr[5]);
    return buf;
}

/* 供 bt_adapter_get_*_devices 回传已分配数组的分配器 */
static bool tt_allocator(void **data, uint32_t size)
{
    *data = malloc(size);
    return (*data != NULL);
}

/* 解析手机(对端)地址, 优先级: 已连接的 BR/EDR 设备 > 已配对的 BR/EDR 设备。
 * 蓝牙共享(PANU->手机NAP)只需要手机"已配对/已连接", 不要求 SPP 数据通道先行建立。
 * 返回 0 表示已写入 out, <0 表示没有可连接的手机(需先配对)。 */
static int bt_tether_resolve_peer(bt_address_t *out)
{
    bt_address_t *addrs = NULL;
    int num = 0;

    if (!out || !g_ins) {
        return -1;
    }

    /* 1) 优先: 当前已连接的 BR/EDR 设备(最可能就是已自动连接上的手机) */
    if (bt_adapter_get_connected_devices(g_ins, BT_TRANSPORT_BREDR,
                                         &addrs, &num, tt_allocator) == BT_STATUS_SUCCESS
        && addrs && num > 0) {
        memcpy(out, &addrs[0], sizeof(bt_address_t));
        free(addrs);
        return 0;
    }
    if (addrs) { free(addrs); addrs = NULL; }

    /* 2) 回退: 已配对的 BR/EDR 设备(手机配对过但未连, 先尝试把 ACL 连起来) */
    num = 0;
    if (bt_adapter_get_bonded_devices(g_ins, BT_TRANSPORT_BREDR,
                                     &addrs, &num, tt_allocator) == BT_STATUS_SUCCESS
        && addrs && num > 0) {
        memcpy(out, &addrs[0], sizeof(bt_address_t));
        free(addrs);
        /* 先把底层 ACL 链路拉起, 让后续 bt_pan_connect 有承载。
         * 注意: bt_device_connect_all_profile 返回值为 void(无返回值),
         * 失败与否只能由后续 bt_pan_connect 兜底判断, 这里仅发起连接。 */
        bt_device_connect_all_profile(g_ins, out);
        return 0;
    }
    if (addrs) { free(addrs); }

    return -1;
}

/* ---------------- PAN 连接状态回调 ---------------- */
static void tt_connection_state_cb(void *cookie, profile_connection_state_t state,
                                  bt_address_t *bd_addr, uint8_t local_role,
                                  uint8_t remote_role)
{
    char str[18];

    (void)cookie; (void)local_role; (void)remote_role;
    tt_addr_str(bd_addr, str, sizeof(str));
    TSW_LOGI("conn_state peer=%s state=%d", str, state);

    if (state == PROFILE_STATE_CONNECTED) {
        /* PAN 连接已建立; 真正的"可上网"要等 bt-pan 网卡 up(DHCP) */
        TSW_LOGI("PAN connected, waiting for bt-pan netif up ...");
    } else if (state == PROFILE_STATE_DISCONNECTED) {
        if (g_connected) {
            g_connected = false;
            if (g_state_cb) {
                g_state_cb(false);
            }
        }
        TSW_LOGI("PAN disconnected");
    }
}

/* ---------------- PAN 网卡状态回调(网卡 up/down) ---------------- */
static void tt_netif_state_cb(void *cookie, pan_netif_state_t state,
                             int local_role, const char *ifname)
{
    (void)cookie; (void)local_role;

    TSW_LOGI("netif_state state=%d ifname=%s", state,
             ifname ? ifname : "(null)");

    if (state == PAN_STATE_ENABLED && ifname && ifname[0]) {
        strncpy(g_ifname, ifname, sizeof(g_ifname) - 1);
        g_ifname[sizeof(g_ifname) - 1] = '\0';

        /* 网卡已 up, 通过 DHCP 从手机 NAP 取得 IP(并自动设默认路由)。
         * 若栈已自动跑过 DHCP, 此处为冗余但无害。 */
        int ret = netlib_obtain_ipv4addr(g_ifname);
        if (ret == 0) {
            g_connected = true;
            TSW_LOGI("bt-pan DHCP OK -> online via phone (%s)", g_ifname);
            if (g_state_cb) {
                g_state_cb(true);
            }
        } else {
            TSW_LOGE("netlib_obtain_ipv4addr(%s) fail ret=%d", g_ifname, ret);
        }
    } else if (state == PAN_STATE_DISABLED) {
        if (g_connected) {
            g_connected = false;
            if (g_state_cb) {
                g_state_cb(false);
            }
        }
        TSW_LOGI("bt-pan netif down");
    }
}

/* ---------------- 对外 API ---------------- */
void bt_tether_init(void *ins)
{
    static const pan_callbacks_t cbs = {
        .size = sizeof(pan_callbacks_t),
        .netif_state_cb      = tt_netif_state_cb,
        .connection_state_cb = tt_connection_state_cb,
    };

    if (g_inited) {
        TSW_LOGW("bt_tether already inited");
        return;
    }
    g_ins = (bt_instance_t *)ins;
    g_pan_cookie = bt_pan_register_callbacks(g_ins, &cbs);
    if (!g_pan_cookie) {
        TSW_LOGE("bt_pan_register_callbacks fail");
        return;
    }
    g_inited = true;
    TSW_LOGI("bt tether(PAN) registered");
}

bool bt_tether_is_supported(void)
{
    return true;
}

int bt_tether_start(void)
{
    bt_address_t peer;
    bt_status_t  ret;
    char         str[18];

    if (!g_inited || !g_ins) {
        TSW_LOGE("bt_tether not inited");
        return -1;
    }
    if (g_connected) {
        TSW_LOGW("bt_tether already connected");
        return 0;
    }

    /* 对端(手机)地址: 取自"已连接/已配对"的蓝牙设备, 与 SPP 无关。
     * 若都没有则提示先配对手机。 */
    if (bt_tether_resolve_peer(&peer) != 0) {
        TSW_LOGE("bt_tether_start: no paired/connected phone, pair phone first");
        return -1;
    }
    TSW_LOGI("bt_tether_start: target phone %s", tt_addr_str(&peer, str, sizeof(str)));

    /* 手表作为 PANU(src) 连手机 NAP(dst) */
    ret = bt_pan_connect(g_ins, &peer, PAN_ROLE_NAP, PAN_ROLE_PANU);
    if (ret != BT_STATUS_SUCCESS) {
        TSW_LOGE("bt_pan_connect fail ret=%d", ret);
        return -1;
    }
    TSW_LOGI("bt_pan_connect initiated (PANU -> NAP)");
    return 0;
}

int bt_tether_stop(void)
{
    bt_address_t peer;

    if (!g_inited || !g_ins) {
        return -1;
    }
    if (bt_tether_resolve_peer(&peer) == 0) {
        bt_pan_disconnect(g_ins, &peer);
        TSW_LOGI("bt_pan_disconnect initiated");
    } else {
        TSW_LOGW("bt_tether_stop: cannot resolve peer addr");
    }
    return 0;
}

bool bt_tether_is_connected(void)
{
    return g_connected;
}

const char *bt_tether_get_ifname(void)
{
    return g_ifname;
}

void bt_tether_set_state_listener(bt_tether_state_cb_t cb)
{
    g_state_cb = cb;
}
