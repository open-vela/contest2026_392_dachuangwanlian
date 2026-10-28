/****************************************************************************
 * bt_manager.c - tswatch 蓝牙子系统编排
 *
 * 开机流程(后台线程, 调用 bt_manager_start 后异步执行):
 *   1. 等待 bluetoothd(IPC) 就绪并 enable adapter(BREDR + BLE)
 *   2. 设置本地名 TSWATCH
 *   3. 注册适配器回调:
 *        - 配对/绑定回调(自动接受, 见 bt_pair.c)
 *        - ACL 连接状态回调(用于切换可发现性)
 *   4. 启动 BLE 广播(bt_adv.c)
 *   5. 初始化 SPP 数据通道(bt_transport.c, 手表作为 SPP Server)
 *   6. 默认设为: 可发现 + 可连接 + 可配对(直到连接成功)
 *   7. 查询已绑定(已配对)设备, 若有则自动连接
 *
 * 可发现性策略:
 *   - 未连接(或无对端在范围内): CONNECTABLE_DISCOVERABLE(可见可连可配对)
 *   - 连接成功(ACL UP):         CONNECTABLE(可连但不可见, 避免被反复搜索)
 *   - 连接断开:                 重新 CONNECTABLE_DISCOVERABLE
 *
 * 日志前缀: [bt_mgr]
 ****************************************************************************/
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "bluetooth.h"
#include "bt_adapter.h"
#include "bt_device.h"
#include "bt_adv.h"
#include "bt_pair.h"
#include "bt_transport.h"
#include "bt_pair_ui.h"
#include "bt_tether.h"
#include "bt_manager.h"

#define TSW_LOG_TAG "bt_mgr"
#include "tsw_log.h"

#define BT_LOCAL_NAME        "TSWATCH"
#define BT_ENABLE_TIMEOUT_S  30      /* 等待 adapter ON 超时(秒) */

/* 适配器回调表: 必须用静态变量(框架只保存指针, 不拷贝, 见 bt_pair.h 说明) */
static adapter_callbacks_t g_adapter_cbs;
static bt_instance_t      *g_ins = NULL;

/* 本地地址转字符串 */
static const char *mgr_addr_str(const bt_address_t *addr, char *buf, int len)
{
    if (!addr || !buf || len < 18)
        return (addr ? "<addr?>" : "<null>");
    snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr->addr[0], addr->addr[1], addr->addr[2],
             addr->addr[3], addr->addr[4], addr->addr[5]);
    return buf;
}

/* ---------------- ACL 连接状态: 切换可发现性 ---------------- */
static void on_connection_state_changed(void *cookie, bt_address_t *addr,
                                        bt_transport_t transport,
                                        connection_state_t state)
{
    char str[18];

    (void)cookie;
    mgr_addr_str(addr, str, sizeof(str));
    TSW_LOGI("acl_state %s transport=%d state=%d", str, transport, state);

    if (state == CONNECTION_STATE_CONNECTED) {
        /* 已连接 -> 不可被发现(仅可连接) */
        bt_adapter_set_scan_mode(g_ins, BT_SCAN_MODE_CONNECTABLE, false);
        TSW_LOGI("connected -> set NON-discoverable");
    } else if (state == CONNECTION_STATE_DISCONNECTED) {
        /* 断开 -> 重新可被发现/可配对 */
        bt_adapter_set_scan_mode(g_ins, BT_SCAN_MODE_CONNECTABLE_DISCOVERABLE, true);
        TSW_LOGI("disconnected -> set DISCOVERABLE");
    }
}

/* ---------------- 内存分配器: 供框架回传已绑定设备地址数组 ---------------- */
static bool mgr_allocator(void **data, uint32_t size)
{
    *data = malloc(size);
    return (*data != NULL);
}

/* ---------------- 查询已绑定(已配对)设备并自动连接 ---------------- */
static void bt_mgr_auto_connect_bonded(bt_instance_t *ins)
{
    bt_address_t *addrs = NULL;
    int num = 0;
    int i;
    char str[18];

    if (bt_adapter_get_bonded_devices(ins, BT_TRANSPORT_BREDR,
                                      &addrs, &num, mgr_allocator)
            != BT_STATUS_SUCCESS) {
        TSW_LOGW("get bonded devices fail");
        return;
    }
    if (!addrs || num <= 0) {
        TSW_LOGI("no bonded device, skip auto connect");
        return;
    }
    TSW_LOGI("bonded devices num=%d -> auto connect", num);
    for (i = 0; i < num; i++) {
        mgr_addr_str(&addrs[i], str, sizeof(str));
        TSW_LOGI("auto connect bonded %s", str);
        bt_device_connect_all_profile(ins, &addrs[i]);
    }
    free(addrs);
}

/* ---------------- 后台线程 ---------------- */
static void *bt_manager_thread(void *arg)
{
    bt_instance_t      *ins;
    bt_adapter_state_t state;
    int                wait_s = 0;

    (void)arg;

    /* 1. 获取 Bluetooth IPC 客户端实例(自动连接 bluetoothd) */
    while (wait_s < 60) {
        ins = bluetooth_get_instance();
        if (ins) {
            break;
        }
        usleep(500 * 1000);
        wait_s++;
    }
    if (!ins) {
        TSW_LOGE("bluetooth_get_instance fail (bluetoothd not ready?), abort");
        return NULL;
    }
    g_ins = ins;
    TSW_LOGI("bt instance=%p", ins);

    /* 2. 若未开启则 enable(BREDR + BLE) */
    state = bt_adapter_get_state(ins);
    TSW_LOGI("adapter state=%d", state);
    if (state != BT_ADAPTER_STATE_ON) {
        bt_adapter_enable(ins);
        TSW_LOGI("bt_adapter_enable called");
    }

    /* 3. 等待 adapter ON(轮询, 每 500ms) */
    while (wait_s < BT_ENABLE_TIMEOUT_S * 2) {
        state = bt_adapter_get_state(ins);
        if (state == BT_ADAPTER_STATE_ON) {
            break;
        }
        usleep(500 * 1000);
        wait_s++;
    }
    if (state != BT_ADAPTER_STATE_ON) {
        TSW_LOGE("adapter not ON after %ds, abort", BT_ENABLE_TIMEOUT_S);
        return NULL;
    }

    /* 4. 设置本地名 */
    bt_adapter_set_name(ins, BT_LOCAL_NAME);
    TSW_LOGI("set local name=%s", BT_LOCAL_NAME);

    /* 5. 注册适配器回调(配对自动接受 + ACL 连接状态) */
    memset(&g_adapter_cbs, 0, sizeof(g_adapter_cbs));
    bt_pair_attach_cbs(ins, &g_adapter_cbs);
    g_adapter_cbs.on_connection_state_changed = on_connection_state_changed;
    bt_adapter_register_callback(ins, &g_adapter_cbs);
    TSW_LOGI("adapter callbacks registered");

    /* 6. BLE 可连接广播 */
    if (bt_adv_start_ble(ins) != 0) {
        TSW_LOGW("bt_adv_start_ble fail");
    }

    /* 7. SPP 数据通道(手表作为 Server) */
    bt_transport_init(ins);

    /* 7.1 蓝牙上网(PAN/BNEP): 注册 PAN 回调, 由用户在后设置页触发连接 */
    // bt_tether_init(ins);

    /* 8. 默认: 可发现 + 可连接 + 可配对(直到连接成功) */
    bt_adapter_set_scan_mode(ins, BT_SCAN_MODE_CONNECTABLE_DISCOVERABLE, true);
    TSW_LOGI("default scan mode = DISCOVERABLE (until connected)");

    /* 9. 若已配对(已绑定): 自动连接 */
    bt_mgr_auto_connect_bonded(ins);

    TSW_LOGI("bt_manager started");
    return NULL;
}

void bt_manager_start(void)
{
    static int g_started = 0;
    pthread_t tid;
    int ret;

    /* 幂等: 仅允许启动一次, 防止重复创建后台线程 */
    if (g_started) {
        TSW_LOGW("bt_manager already started, ignore");
        return;
    }
    g_started = 1;

    /* 注册配对码屏幕显示(数字比较配对时把 6 位码画到手表屏) */
    bt_pair_ui_init();

    ret = pthread_create(&tid, NULL, bt_manager_thread, NULL);
    if (ret != 0) {
        TSW_LOGE("pthread_create fail ret=%d", ret);
        return;
    }
    pthread_detach(tid);
    TSW_LOGI("bt_manager thread started");
}
