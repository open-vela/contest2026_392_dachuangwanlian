/****************************************************************************
 * bt_pair_ui.c - 配对码屏幕显示
 *
 * 显示方式参考 tswatch 现有 UI 写法:
 *   - 覆盖层创建在 lv_layer_top() 之上(置顶, 盖住当前页面);
 *   - 用 lv_obj_create + lv_obj_center 做居中半透明面板;
 *   - 用 lv_label_create + lv_label_set_text 显示文字;
 *   - 跨线程(蓝牙线程 -> LVGL 线程)一律经 lv_async_call() 投递, 与
 *     xiaozhi_voice 的 UI 调度方式一致, 避免并发访问 LVGL 对象树导致崩溃。
 *
 * 配对通常在数秒内完成, 这里额外加 30s 自动隐藏兜底。
 ****************************************************************************/
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "font_manager.h"   /* font_manager_get_font / FONT_* */

#include "bt_pair.h"
#include "bt_pair_ui.h"

#define TSW_LOG_TAG "bt_pair_ui"
#include "tsw_log.h"

/* 覆盖层与自动隐藏定时器(仅由 LVGL 主线程读写, 蓝牙线程不直接访问) */
static lv_obj_t   *g_pair_cont  = NULL;
static lv_timer_t *g_pair_timer = NULL;

/* 配对码显示时长(ms): 兜底自动消失 */
#define BT_PAIR_UI_TIMEOUT_MS 30000

/* ---------------- 在 LVGL 主线程删除覆盖层 ---------------- */
static void bt_pair_ui_hide(void)
{
    if (g_pair_timer) {
        lv_timer_delete(g_pair_timer);
        g_pair_timer = NULL;
    }
    if (g_pair_cont) {
        lv_obj_del(g_pair_cont);
        g_pair_cont = NULL;
    }
}

/* ---------------- 超时自动隐藏(运行于 LVGL 主线程) ---------------- */
static void bt_pair_ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    bt_pair_ui_hide();
}

/* ---------------- 真正删除覆盖层(运行于 LVGL 主线程, lv_async_call 投递) ---------------- */
static void bt_pair_ui_hide_async(void *arg)
{
    (void)arg;
    bt_pair_ui_hide();
}

/* ---------------- 真正在屏幕上绘制配对码(运行于 LVGL 主线程) ---------------- */
static void bt_pair_ui_show_async(void *arg)
{
    uint32_t passkey = (uint32_t)(uintptr_t)arg;
    lv_obj_t *layer;
    lv_obj_t *label;
    char buf[16];

    /* 若已有覆盖层, 先删掉再重建(应对重新配对) */
    if (g_pair_cont) {
        lv_obj_del(g_pair_cont);
        g_pair_cont = NULL;
    }
    if (g_pair_timer) {
        lv_timer_delete(g_pair_timer);
        g_pair_timer = NULL;
    }

    layer = lv_layer_top();

    /* 半透明居中面板 */
    g_pair_cont = lv_obj_create(layer);
    lv_obj_set_size(g_pair_cont, 360, 200);
    lv_obj_center(g_pair_cont);
    lv_obj_set_style_bg_opa(g_pair_cont, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(g_pair_cont, lv_color_black(), 0);
    lv_obj_set_style_radius(g_pair_cont, 16, 0);
    lv_obj_set_style_border_width(g_pair_cont, 2, 0);
    lv_obj_set_style_border_color(g_pair_cont,
                                 lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_clear_flag(g_pair_cont, LV_OBJ_FLAG_SCROLLABLE);

    /* 标题: 配对码 PAIRING CODE */
    label = lv_label_create(g_pair_cont);
    lv_label_set_text(label, "配对码  PAIRING CODE");
    lv_obj_set_style_text_font(label, font_manager_get_font(FONT_MEDIUM), 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 18);

    /* 6 位配对码(大字号, 加字距更易辨识) */
    snprintf(buf, sizeof(buf), "%06u", passkey);
    label = lv_label_create(g_pair_cont);
    lv_label_set_text(label, buf);
    lv_obj_set_style_text_font(label, font_manager_get_font(FONT_LARGE), 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(label, 8, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -8);

    /* 提示: 请在手机上确认 */
    label = lv_label_create(g_pair_cont);
    lv_label_set_text(label, "请在手机上确认配对");
    lv_obj_set_style_text_font(label, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(label,
                                lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -18);

    /* 启动自动隐藏定时器 */
    g_pair_timer = lv_timer_create(bt_pair_ui_timer_cb,
                                  BT_PAIR_UI_TIMEOUT_MS, NULL);
}

/* ---------------- 供 bt_pair 调用的显示回调(运行于蓝牙线程) ---------------- */
void bt_pair_ui_show_passkey(uint32_t passkey, const bt_address_t *peer)
{
    (void)peer;
    TSW_LOGI("show pairing passkey %06u on screen", passkey);
    /* 投递到 LVGL 主线程执行, 避免跨线程操作 LVGL 对象树 */
    lv_async_call(bt_pair_ui_show_async, (void *)(uintptr_t)passkey);
}

/* ---------------- 供 bt_pair 调用的隐藏接口(运行于蓝牙线程) ---------------- */
void bt_pair_ui_hide_passkey(void)
{
    TSW_LOGI("hide pairing passkey UI (bond done/cancelled)");
    /* 同样经 lv_async_call 投到 LVGL 主线程, 与显示路径保持一致, 避免跨线程删对象 */
    lv_async_call(bt_pair_ui_hide_async, NULL);
}

/* ---------------- 注册显示回调(幂等, 仅设置静态指针) ---------------- */
void bt_pair_ui_init(void)
{
    bt_pair_set_passkey_display_cb(bt_pair_ui_show_passkey);
    TSW_LOGI("pairing-code UI registered");
}
