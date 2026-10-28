/****************************************************************************
 * bt_transport.c - 手表 <-> 手机 数据传输(SPP + euv_pipe)
 *
 * 数据流说明(与蓝牙框架 tools/spp.c 一致):
 *   - 手表作为 SPP Server; 手机作为 SPP Client 连接本机 SPP 服务。
 *   - SPP 连接建立后, 框架通过 proxy_state_cb 回调给一个 "proxy name"
 *     (如 "btspp-srv0"), 该 name 是 libuv 命名管道名(并非 /dev 节点)。
 *   - 本模块用 euv_pipe_connect() 连接到该管道, 之后即可像串口一样收发。
 *   - 所有 euv_pipe 操作必须在其专属 uv_thread_loop 线程内执行
 *     (通过 do_in_thread_loop 投递), 否则会破坏 libuv 事件循环。
 *
 * 对外 API 见 bt_transport.h: bt_transport_send / send_str /
 *   set_rx_callback / is_connected。
 *
 * 日志前缀: [bt_transport]
 ****************************************************************************/
#include <stdlib.h>
#include <string.h>
#include "bluetooth.h"
#include "bt_adapter.h"
#include "bt_spp.h"
#include "bt_uuid.h"
#include "bt_addr.h"
#include "euv_pipe.h"
#include "uv_thread_loop.h"
#include "bt_device.h"       /* profile_connection_state_t / PROFILE_STATE_CONNECTED */
#include "bt_transport.h"
#include "bt_pair_ui.h"   /* bt_pair_ui_hide_passkey: SPP 连上即收起配对码页 */

#define TSW_LOG_TAG "bt_transport"
#include "tsw_log.h"

/* SPP 服务参数 */
#define BT_SPP_SCN           1                       /* server channel */
#define BT_SPP_MAX_CONN      1                       /* 同时最大连接数 */
#define BT_SPP_APP_NAME      "tswatch"               /* SPP 应用名 */
#define BT_SPP_READ_SIZE     2048                    /* 单次读取缓冲 */

/* 单连接模型(手表一般只连一台手机) */
typedef struct {
    euv_pipe_t  *pipe;          /* 已连接的管道句柄, NULL 表示未连接 */
    bt_address_t addr;          /* 对端地址 */
    uint16_t     port;
    uint16_t     scn;
    char         name[32];      /* proxy name */
} bt_spp_dev_t;

static uv_loop_t      g_spp_loop;     /* SPP 专属事件循环 */
static bt_spp_dev_t   g_spp_dev;      /* 当前连接设备 */
static bool           g_spp_inited = false;
static void          *g_spp_handle = NULL;  /* bt_spp_register_app 返回的 handle */
static bt_transport_rx_cb_t g_rx_cb = NULL; /* 用户接收回调 */
static bt_transport_conn_cb_t g_conn_cb = NULL; /* 用户连接状态回调 */

/* ---------------- 工具 ---------------- */
static const char *tsp_addr_str(const bt_address_t *addr, char *buf, int len)
{
    if (!addr || !buf || len < 18)
        return (addr ? "<addr?>" : "<null>");
    snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr->addr[0], addr->addr[1], addr->addr[2],
             addr->addr[3], addr->addr[4], addr->addr[5]);
    return buf;
}

/* ---------------- 读取 / 发送 ---------------- */
static void tsp_read_cb(euv_pipe_t *handle, const uint8_t *buf, ssize_t size)
{
    char str[18];

    if (size > 0) {
        tsp_addr_str(&g_spp_dev.addr, str, sizeof(str));
        if (g_rx_cb) {
            g_rx_cb(buf, (uint16_t)size, &g_spp_dev.addr);
        } else {
            TSW_LOGI("recv %d bytes from %s (no rx cb)", (int)size, str);
        }
    } else if (size < 0) {
        /* 对端断开或读失败 */
        tsp_addr_str(&g_spp_dev.addr, str, sizeof(str));
        TSW_LOGW("read failed/closed, status:%d, peer=%s", (int)size, str);
        if (g_spp_dev.pipe) {
            euv_pipe_disconnect(g_spp_dev.pipe);
            g_spp_dev.pipe = NULL;
        }
        memset(&g_spp_dev.addr, 0, sizeof(g_spp_dev.addr));
    }
}

static void tsp_write_complete(euv_pipe_t *handle, uint8_t *buf, int status)
{
    (void)handle;
    if (status != 0) {
        TSW_LOGW("write complete status:%d", status);
    }
    free(buf);   /* buf 即发送时传入的 payload 指针, 完成后释放 */
}

/* 发送消息(在 SPP 事件循环线程中执行) */
typedef struct {
    uint8_t *payload;
    int      len;
} tsp_send_msg_t;

static void tsp_send_proc(void *data)
{
    tsp_send_msg_t *msg = (tsp_send_msg_t *)data;

    if (!g_spp_dev.pipe) {
        TSW_LOGW("tsp_send_proc: not connected, drop %d bytes", msg->len);
        free(msg->payload);
        free(msg);
        return;
    }
    /* euv_pipe_write 完成回调收到的是 payload 指针, 在那里 free */
    euv_pipe_write(g_spp_dev.pipe, msg->payload, msg->len, tsp_write_complete);
    free(msg);
}

/* ---------------- 管道连接 ---------------- */
static void tsp_connect_cb(euv_pipe_t *handle, int status, void *user_data)
{
    bt_spp_dev_t *dev = user_data;
    char str[18];

    (void)dev;
    tsp_addr_str(&g_spp_dev.addr, str, sizeof(str));
    if (status != 0) {
        TSW_LOGE("pipe connect fail, status:%d peer=%s", status, str);
        g_spp_dev.pipe = NULL;
        return;
    }
    g_spp_dev.pipe = handle;
    euv_pipe_read_start(handle, BT_SPP_READ_SIZE, tsp_read_cb, NULL);
    TSW_LOGI("SPP pipe connected, peer=%s port=%d -> data channel READY",
             str, g_spp_dev.port);

    /* 此时 SPP 数据管道真正就绪（is_connected() 已为真），通知已注册的"连接状态回调"，
     * 驱动离线录音回传等依赖数据通道的逻辑。此前不应在 profile 连接回调里触发，
     * 否则管道未就绪即回传会导致发送失败/丢帧。 */
    if (g_conn_cb)
        g_conn_cb(&g_spp_dev.addr);

    /* 配对码页兜底收起: 配对真正完成、手机已连上 SPP 的可靠信号就是本回调。
     * 某些手机(如 Xiaomi)会发起两轮配对, 蓝牙栈对重复 bond 状态变更去重,
     * 导致第二轮 on_bond_state_changed(BONDED/NONE) 不回调, 页面只能等 30s 超时。
     * 这里在 SPP 连上时强制收起, 保证"手机确认后页面立即消失"。
     * bt_pair_ui_hide_passkey 内部用 lv_async_call, 从 libuv 线程调用也线程安全。 */
    bt_pair_ui_hide_passkey();
}

/* 在 SPP 事件循环线程中执行管道连接 */
static void tsp_open_proc(void *data)
{
    char *name = (char *)data;

    g_spp_dev.pipe = euv_pipe_connect(&g_spp_loop, name, tsp_connect_cb, &g_spp_dev);
    if (!g_spp_dev.pipe) {
        TSW_LOGE("euv_pipe_connect fail, name=%s", name);
    }
    free(name);
}

/* ---------------- SPP 回调 ---------------- */
static void tsp_proxy_state_cb(void *handle, bt_address_t *addr,
                              spp_proxy_state_t state, uint16_t scn,
                              uint16_t port, char *name)
{
    char str[18];

    (void)handle;
    tsp_addr_str(addr, str, sizeof(str));
    TSW_LOGI("proxy_state_cb peer=%s scn=%d port=%d state=%d name=%s",
             str, scn, port, state, name ? name : "(null)");

    if (state == SPP_PROXY_STATE_CONNECTED && name && name[0]) {
        /* 保存对端信息, 并把 "连接管道" 投递到 SPP 事件循环线程执行 */
        memcpy(&g_spp_dev.addr, addr, sizeof(bt_address_t));
        g_spp_dev.scn  = scn;
        g_spp_dev.port = port;
        strncpy(g_spp_dev.name, name, sizeof(g_spp_dev.name) - 1);
        g_spp_dev.name[sizeof(g_spp_dev.name) - 1] = '\0';

        char *nm = strdup(name);
        if (nm) {
            do_in_thread_loop(&g_spp_loop, tsp_open_proc, nm);
        }
    } else if (state == SPP_PROXY_STATE_DISCONNECTED) {
        TSW_LOGI("SPP proxy disconnected, peer=%s", str);
        if (g_spp_dev.pipe) {
            euv_pipe_disconnect(g_spp_dev.pipe);
            g_spp_dev.pipe = NULL;
        }
        memset(&g_spp_dev.addr, 0, sizeof(g_spp_dev.addr));
    }
}

static void tsp_connection_state_cb(void *handle, bt_address_t *addr,
                                    uint16_t scn, uint16_t port,
                                    profile_connection_state_t state)
{
    char str[18];

    (void)handle; (void)scn; (void)port;
    tsp_addr_str(addr, str, sizeof(str));
    TSW_LOGI("spp connection_state peer=%s state=%d",
             str, state);

    /* 注意：此处仅 profile 连接建立，SPP 数据管道尚未就绪（见 tsp_connect_cb）。
     * 真正的"已连接可用"信号在 tsp_connect_cb（管道 READY、is_connected() 为真）触发，
     * 故此处不调用 g_conn_cb，避免离线回传在管道就绪前抢跑导致丢帧/不发送。 */
}

/* ---------------- 对外 API ---------------- */
void bt_transport_init(void *ins)
{
    static const spp_callbacks_t cbs = {
        .size = sizeof(spp_callbacks_t),
        .proxy_state_cb      = tsp_proxy_state_cb,
        .connection_state_cb = tsp_connection_state_cb,
    };
    bt_uuid_t uuid;
    bt_status_t ret;

    if (g_spp_inited) {
        TSW_LOGW("bt_transport already inited");
        return;
    }

    /* 初始化并启动 SPP 专属事件循环线程(所有 euv_pipe 操作都在此线程) */
    thread_loop_init(&g_spp_loop);
    thread_loop_run(&g_spp_loop, true, "tsw_spp");

    /* 注册 SPP 应用并开始监听(手表作为 Server) */
    g_spp_handle = bt_spp_register_app_with_name((bt_instance_t *)ins,
                                                 BT_SPP_APP_NAME, &cbs);
    if (!g_spp_handle) {
        TSW_LOGE("bt_spp_register_app fail");
        return;
    }

    bt_uuid16_create(&uuid, BT_UUID_SERVCLASS_SERIAL_PORT);  /* 0x1101 */
    ret = bt_spp_server_start((bt_instance_t *)ins, g_spp_handle,
                              BT_SPP_SCN, &uuid, BT_SPP_MAX_CONN);
    if (ret != BT_STATUS_SUCCESS) {
        TSW_LOGE("bt_spp_server_start fail ret=%d", ret);
        return;
    }

    TSW_LOGI("SPP server started (scn=%d, name=%s), waiting for phone connect",
             BT_SPP_SCN, BT_SPP_APP_NAME);
    g_spp_inited = true;
}

int bt_transport_send(const uint8_t *data, uint16_t len)
{
    tsp_send_msg_t *msg;

    if (!g_spp_inited || !g_spp_dev.pipe) {
        TSW_LOGW("bt_transport_send: not connected");
        return -1;
    }
    if (!data || len == 0) {
        return -1;
    }

    msg = malloc(sizeof(*msg));
    if (!msg) {
        TSW_LOGE("bt_transport_send: malloc fail");
        return -1;
    }
    msg->payload = malloc((size_t)len);
    if (!msg->payload) {
        TSW_LOGE("bt_transport_send: payload malloc fail");
        free(msg);
        return -1;
    }
    memcpy(msg->payload, data, len);
    msg->len = (int)len;

    /* 投递到 SPP 事件循环线程执行实际写入 */
    do_in_thread_loop(&g_spp_loop, tsp_send_proc, msg);
    return (int)len;
}

int bt_transport_send_str(const char *str)
{
    if (!str) {
        return -1;
    }
    return bt_transport_send((const uint8_t *)str, (uint16_t)strlen(str));
}

void bt_transport_set_rx_callback(bt_transport_rx_cb_t cb)
{
    g_rx_cb = cb;
}

void bt_transport_set_conn_callback(bt_transport_conn_cb_t cb)
{
    g_conn_cb = cb;
}

bool bt_transport_is_connected(void)
{
    return g_spp_inited && (g_spp_dev.pipe != NULL);
}

int bt_transport_get_peer_addr(bt_address_t *out)
{
    if (!out || !g_spp_inited || !g_spp_dev.pipe) {
        return -1;
    }
    memcpy(out, &g_spp_dev.addr, sizeof(bt_address_t));
    return 0;
}
