/*********************
 *      INCLUDES
 *********************/
#include <lvgl/lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "../../utils/include/circle_screen.h"   /* get_watch_scr() */
#include "../../utils/include/font_manager.h"    /* 中文显示字体 */
#include "../../main_page/include/watch_start.h"  /* nav_return_from_app / 配色 / LV_CIRCLE_WATCH */
#include "bt_transport.h"                        /* SPP 数据通道 API（-Ibt/include 已全局包含） */

/*********************
 *      DEFINES
 *********************/
#define SPP_TEST_SEND_INTERVAL_MS   500     /* 连续发送间隔 */

/* 本机发出的回显 / 手机发来的数据，都汇到这个日志里（只由 LVGL 线程/异步回调访问） */
#define SPP_LOG_BUF_LEN    (768)

/*********************
 *      STATIC VARS
 *********************/
static lv_obj_t *g_cont       = NULL;   /* 根容器 */
static lv_obj_t *g_status_lbl = NULL;   /* 连接状态 */
static lv_obj_t *g_log_lbl    = NULL;   /* 收发日志 */
static lv_timer_t *g_status_timer = NULL;
static lv_timer_t *g_send_timer   = NULL;

static bool    g_sending   = false;     /* 连续发送开关 */
static uint32_t g_send_cnt = 0;         /* 连续发送计数 */
static uint32_t g_test_cnt = 0;         /* 单次发送计数 */

/* 日志环形缓冲（仅 LVGL 线程访问，包含异步回调） */
static char g_log_buf[SPP_LOG_BUF_LEN] = {0};

/*********************
 *      FORWARD
 *********************/
static void spp_exit(void);
static void spp_log_async(const char *fmt, ...);
static void spp_rx_cb(const uint8_t *data, uint16_t len, const bt_address_t *peer);

/*********************
 *       HELPERS
 *********************/

/* 创建一个圆形按钮（与 stopwatch 同风格） */
static lv_obj_t *make_btn(lv_obj_t *parent, const char *text, lv_color_t color,
                          lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 110, 64);
    lv_obj_set_style_radius(btn, 32, 0);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_text_color(btn, lv_color_white(), 0);
    lv_obj_set_style_text_font(btn, font_manager_get_font(FONT_LARGE), 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

/* 在 LVGL 线程里把一行文本追加到日志并刷新 label */
static void spp_append_log(const char *line)
{
    if (!g_log_lbl) return;

    size_t cur = strlen(g_log_buf);
    size_t remain = SPP_LOG_BUF_LEN - cur - 1;
    if (remain <= 1) {
        /* 满了就丢弃最旧的一半（简单环形） */
        size_t half = SPP_LOG_BUF_LEN / 2;
        memmove(g_log_buf, g_log_buf + half, SPP_LOG_BUF_LEN - half);
        cur = strlen(g_log_buf);
        remain = SPP_LOG_BUF_LEN - cur - 1;
    }
    strncat(g_log_buf, line, remain);
    lv_label_set_text(g_log_lbl, g_log_buf);
}

/* lv_async_call 触发的真正写日志（运行于 LVGL 主线程） */
static void spp_log_async_cb(void *p)
{
    const char *line = (const char *)p;
    if (line) {
        spp_append_log(line);
        free((void *)line);
    }
}

/* 任意线程（含蓝牙数据线程）调用：格式化一行日志，投到 LVGL 线程显示 */
static void spp_log_async(const char *fmt, ...)
{
    char *buf = malloc(160);
    if (!buf) return;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, 160, fmt, ap);
    va_end(ap);

    /* 确保以换行结尾 */
    size_t n = strlen(buf);
    if (n > 0 && buf[n - 1] != '\n') {
        if (n + 1 < 160) {
            buf[n] = '\n';
            buf[n + 1] = '\0';
        }
    }
    lv_async_call(spp_log_async_cb, buf);
}

/* 蓝牙数据线程中的接收回调：手机经 SPP 发来的数据 */
static void spp_rx_cb(const uint8_t *data, uint16_t len, const bt_address_t *peer)
{
    if (!data || len == 0) return;

    /* 拷贝出来再投到 LVGL 线程，避免在回调内做 UI 操作 */
    char *copy = malloc((size_t)len + 1);
    if (!copy) return;
    memcpy(copy, data, len);
    copy[len] = '\0';

    spp_log_async("RX: %s", copy);
    free(copy);
}

/* 状态刷新定时器：每 500ms 更新一次连接状态 */
static void spp_status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!g_status_lbl) return;
    bool connected = bt_transport_is_connected();
    lv_label_set_text(g_status_lbl,
                      connected ? "状态: 已连接" : "状态: 未连接");
    lv_obj_set_style_text_color(g_status_lbl,
                                connected ? COLOR_ACCENT_GREEN : COLOR_ACCENT_RED, 0);
}

/* 连续发送定时器 */
static void spp_send_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    char payload[64];
    snprintf(payload, sizeof(payload), "tswatch spp stress #%u\n", g_send_cnt++);
    int ret = bt_transport_send_str(payload);
    if (ret > 0) {
        spp_log_async("TX: %s", payload);
    } else {
        spp_log_async("TX 失败: 通道未连接(ret=%d)", ret);
    }
}

/*********************
 *   BUTTON HANDLERS
 *********************/

/* 「发送测试」：发一条带序号的字串 */
static void send_test_handler(lv_event_t *e)
{
    (void)e;
    char payload[64];
    snprintf(payload, sizeof(payload), "Hello from tswatch #%u\n", g_test_cnt++);
    int ret = bt_transport_send_str(payload);
    if (ret > 0) {
        spp_log_async("TX: %s", payload);
    } else {
        spp_log_async("TX 失败: 通道未连接(ret=%d)，请先在手机 SPP 连接本机", ret);
    }
}

/* 「连续发送」：切换 500ms 定时器 */
static void toggle_send_handler(lv_event_t *e)
{
    (void)e;
    g_sending = !g_sending;
    if (g_sending) {
        if (!g_send_timer) {
            g_send_timer = lv_timer_create(spp_send_timer_cb,
                                           SPP_TEST_SEND_INTERVAL_MS, NULL);
        }
        spp_log_async("连续发送: 开");
    } else {
        if (g_send_timer) {
            lv_timer_del(g_send_timer);
            g_send_timer = NULL;
        }
        spp_log_async("连续发送: 关");
    }
}

/* 「清除」：清空日志 */
static void clear_handler(lv_event_t *e)
{
    (void)e;
    g_log_buf[0] = '\0';
    if (g_log_lbl) lv_label_set_text(g_log_lbl, "");
}

/*********************
 *   GESTURE / EXIT
 *********************/

static void spp_gesture_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_GESTURE) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir == LV_DIR_RIGHT) {
        spp_exit();
    }
}

static void spp_exit(void)
{
    if (!g_cont) return;

    /* 先复位接收回调，避免残留指向本模块 */
    bt_transport_set_rx_callback(NULL);

    if (g_status_timer) { lv_timer_del(g_status_timer); g_status_timer = NULL; }
    if (g_send_timer)   { lv_timer_del(g_send_timer);   g_send_timer   = NULL; }
    g_sending = false;
    g_send_cnt = 0;
    g_test_cnt = 0;
    g_log_buf[0] = '\0';

    /* 先恢复上层页面（应用列表/表盘），再删容器，避免黑屏 */
    nav_return_from_app();

    lv_obj_del(g_cont);
    g_cont = NULL;
    g_status_lbl = NULL;
    g_log_lbl = NULL;

    LV_LOG_USER("spp_test exited");
}

/*********************
 *   PUBLIC ENTRY
 *********************/

void spp_test_start(void)
{
    if (g_cont != NULL) {
        lv_obj_clear_flag(g_cont, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_t *scr = get_watch_scr();

    g_cont = lv_obj_create(scr);
    lv_obj_set_size(g_cont, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_set_style_bg_color(g_cont, COLOR_BG_DARK, 0);
    lv_obj_set_style_radius(g_cont, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(g_cont, 0, 0);
    lv_obj_set_style_pad_all(g_cont, 0, 0);
    lv_obj_clear_flag(g_cont, LV_OBJ_FLAG_SCROLLABLE);

    /* 手势：右滑退出 */
    lv_obj_add_event_cb(g_cont, spp_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_remove_flag(g_cont, LV_OBJ_FLAG_GESTURE_BUBBLE);

    /* 标题 */
    lv_obj_t *title = lv_label_create(g_cont);
    lv_label_set_text(title, "SPP 蓝牙测试");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_set_style_text_font(title, font_manager_get_font(FONT_LARGE), 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT_WHITE, 0);

    /* 连接状态 */
    g_status_lbl = lv_label_create(g_cont);
    lv_label_set_text(g_status_lbl, "状态: 未连接");
    lv_obj_align(g_status_lbl, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_text_font(g_status_lbl, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(g_status_lbl, COLOR_ACCENT_RED, 0);

    /* 日志区（可滚动） */
    lv_obj_t *log_box = lv_obj_create(g_cont);
    lv_obj_set_size(log_box, 410, 210);
    lv_obj_align(log_box, LV_ALIGN_TOP_MID, 0, 88);
    lv_obj_set_style_bg_color(log_box, COLOR_BG_MID, 0);
    lv_obj_set_style_bg_opa(log_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(log_box, 14, 0);
    lv_obj_set_style_border_width(log_box, 0, 0);
    lv_obj_set_style_pad_all(log_box, 8, 0);
    lv_obj_set_scroll_dir(log_box, LV_DIR_VER);

    g_log_lbl = lv_label_create(log_box);
    lv_label_set_long_mode(g_log_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_log_lbl, 394);
    lv_label_set_text(g_log_lbl, "等待手机经 SPP 连接...\n");
    lv_obj_set_style_text_font(g_log_lbl, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(g_log_lbl, COLOR_TEXT_GRAY, 0);

    /* 按钮行 */
    lv_obj_t *btn_row = lv_obj_create(g_cont);
    lv_obj_set_size(btn_row, 420, 80);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(btn_row, LV_OPA_TRANSP, 0);

    make_btn(btn_row, "发送", COLOR_ACCENT_GREEN,  send_test_handler);
    make_btn(btn_row, "连续", COLOR_ACCENT_ORANGE, toggle_send_handler);
    make_btn(btn_row, "清除", COLOR_ACCENT_BLUE,    clear_handler);

    /* 周期刷新连接状态 */
    g_status_timer = lv_timer_create(spp_status_timer_cb, 500, NULL);
    /* 立即刷新一次 */
    spp_status_timer_cb(NULL);

    /* 接管 SPP 接收回调，把手机回传数据显示到日志 */
    bt_transport_set_rx_callback(spp_rx_cb);

    LV_LOG_USER("spp_test started");
}
