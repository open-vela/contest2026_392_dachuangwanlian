/*********************
 *      INCLUDES
 *********************/
#include <lvgl/lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "../../timer/include/timer_main.h"
// #include "../../launcher/include/app_list.h"
#include "../../utils/include/circle_screen.h"
#include "../../utils/include/font_manager.h"
#include "../../main_page/include/main_page.h"
#include "../../main_page/include/watch_start.h"

/*********************
 *      INCLUDES
 *********************/

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/


/*Collect the unicode lists and glyph_id offsets*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static lv_font_fmt_txt_glyph_cache_t cache;
#endif



static char *muse_str = "00\n"
                        "01\n"
                        "02\n"
                        "03\n"
                        "04\n"
                        "05\n"
                        "06\n"
                        "07\n"
                        "08\n"
                        "09\n"
                        "10\n"
                        "11\n"
                        "12\n"
                        "13\n"
                        "14\n"
                        "15\n"
                        "16\n"
                        "17\n"
                        "18\n"
                        "19\n"
                        "20\n"
                        "21\n"
                        "22\n"
                        "23\n"
                        "24\n"
                        "25\n"
                        "26\n"
                        "27\n"
                        "28\n"
                        "29\n"
                        "30\n"
                        "31\n"
                        "32\n"
                        "33\n"
                        "34\n"
                        "35\n"
                        "36\n"
                        "37\n"
                        "38\n"
                        "39\n"
                        "40\n"
                        "41\n"
                        "42\n"
                        "43\n"
                        "44\n"
                        "45\n"
                        "46\n"
                        "47\n"
                        "48\n"
                        "49\n"
                        "50\n"
                        "51\n"
                        "52\n"
                        "53\n"
                        "54\n"
                        "55\n"
                        "56\n"
                        "57\n"
                        "58\n"
                        "59\n";

static char *roller_options = "00\n"
                              "01\n"
                              "02\n"
                              "03\n"
                              "04\n"
                              "05\n"
                              "06\n"
                              "07\n"
                              "08\n"
                              "09\n"
                              "10\n"
                              "11\n"
                              "12\n"
                              "13\n"
                              "14\n"
                              "15\n"
                              "16\n"
                              "17\n"
                              "18\n"
                              "19\n"
                              "20\n"
                              "21\n"
                              "22\n"
                              "23\n";



const int32_t testNum = 99;
lv_obj_t *timerScreen = NULL;
struct TIMER_CENTER *timerCenterObj = NULL;

#define DEG_TO_RAD(deg) ((deg) * 3.14 / 180.0);
struct TIMER_CONTROL *timerControlObj = NULL;
void run_distry(void);
void set_value(int cur);
void run_reset(void);
/**********************
 *  STATIC VARIABLES
 **********************/

/* 页面切换辅助函数：隐藏当前页，显示目标页 */
static void timer_switch_to_page(lv_obj_t *target)
{
    if (!timerCenterObj || !target) return;
    if (timerCenterObj->main_page)
        lv_obj_add_flag(timerCenterObj->main_page, LV_OBJ_FLAG_HIDDEN);
    if (timerCenterObj->select_page)
        lv_obj_add_flag(timerCenterObj->select_page, LV_OBJ_FLAG_HIDDEN);
    if (timerCenterObj->run_page)
        lv_obj_add_flag(timerCenterObj->run_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(target, LV_OBJ_FLAG_HIDDEN);
}

/* 判断当前显示的是哪个页面 */
static lv_obj_t *timer_get_current_page(void)
{
    if (!timerCenterObj) return NULL;
    if (timerCenterObj->main_page && !lv_obj_has_flag(timerCenterObj->main_page, LV_OBJ_FLAG_HIDDEN))
        return timerCenterObj->main_page;
    if (timerCenterObj->select_page && !lv_obj_has_flag(timerCenterObj->select_page, LV_OBJ_FLAG_HIDDEN))
        return timerCenterObj->select_page;
    if (timerCenterObj->run_page && !lv_obj_has_flag(timerCenterObj->run_page, LV_OBJ_FLAG_HIDDEN))
        return timerCenterObj->run_page;
    return NULL;
}

/* run_page 进入来源：true=从预设按钮, false=从select_page */
static bool from_preset = false;

void timer_gesture_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    switch (code)
    {
    case LV_EVENT_GESTURE:
    {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        if (dir == LV_DIR_RIGHT)
        {
            if (!timerCenterObj) break;
            lv_obj_t *cur_page = timer_get_current_page();

            if (cur_page == timerCenterObj->select_page)
            {
                timer_switch_to_page(timerCenterObj->main_page);
            }
            else if (cur_page == timerCenterObj->run_page)
            {
                run_reset();
                if (from_preset)
                {
                    timer_switch_to_page(timerCenterObj->main_page);
                }
                else
                {
                    timer_switch_to_page(timerCenterObj->select_page);
                }
            }
            else if (cur_page == timerCenterObj->main_page)
            {
                lv_obj_t *timer_container = lv_event_get_current_target(e);
                if (timer_container)
                    lv_obj_delete(timer_container);
                free(timerCenterObj);
                free(timerControlObj);
                timerCenterObj = NULL;
                timerControlObj = NULL;
                nav_return_from_app();
            }
        }
        break;
    }
    default:
        break;
    }
}

/* 更新 run_page 上所有时间标签的显示 */
static void timer_update_display(int total_ms)
{
    if (!timerControlObj) return;
    int total_sec = total_ms / 1000;
    int h = total_sec / 3600;
    int m = (total_sec % 3600) / 60;
    int s = total_sec % 60;

    /* 运行中面板时间 */
    if (timerControlObj->run_op_time_label)
        lv_label_set_text_fmt(timerControlObj->run_op_time_label, "%02d:%02d:%02d", h, m, s);
    /* 运行中大字时间 */
    if (timerControlObj->run_oc_label)
        lv_label_set_text_fmt(timerControlObj->run_oc_label, "%02d:%02d:%02d", h, m, s);
    /* 结束面板时间 */
    if (timerControlObj->run_of_time_label)
        lv_label_set_text_fmt(timerControlObj->run_of_time_label, "%02d:%02d:%02d", h, m, s);
}

/* 预设按钮跳转到 run_page：只更新显示，不启动倒计时 */
static void timer_preset_goto_run(int ms)
{
    if (!timerControlObj || !timerCenterObj) return;
    run_reset();
    timerControlObj->ms = ms;
    timerControlObj->curms = 0;
    timer_update_display(ms);
    /* 确保按钮显示 ▶，运行面板可见 */
    lv_label_set_text(timerControlObj->run_op_btn2_label, LV_SYMBOL_PLAY);
    lv_obj_remove_flag(timerControlObj->run_op, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(timerControlObj->run_of, LV_OBJ_FLAG_HIDDEN);
    from_preset = true;
    timer_switch_to_page(timerCenterObj->run_page);
}

/* 保留供其他场景使用（如语音控制） */
static void timer_start_countdown(int ms)
{
    if (!timerControlObj || !timerCenterObj) return;
    run_reset();
    run_create_timer(ms);
    timer_update_display(ms);
    from_preset = false;
    timer_switch_to_page(timerCenterObj->run_page);
}

/* 预设按钮1回调 (1/2/3/5/10/30 分钟) */
static const int preset_minutes_ms[] = {60000, 120000, 180000, 300000, 600000, 1800000};

static void time_clu_btns_event(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        int idx = (int)(intptr_t)lv_event_get_user_data(e);
        if (idx >= 0 && idx < 6)
        {
            timer_preset_goto_run(preset_minutes_ms[idx]);
        }
    }
}

static void time_clu_add_evet(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        timer_switch_to_page(timerCenterObj->select_page);
    }
}


static void timer_cul_cb(lv_timer_t timer)
{
    if (timerControlObj->ms > timerControlObj->curms)
    {
        timerControlObj->curms += 1000;
        int remaining_ms = timerControlObj->ms - timerControlObj->curms;
        timer_update_display(remaining_ms);
        set_value(timerControlObj->curms * 90 / timerControlObj->ms);
    }
    else
    {
        timerControlObj->paused = true;
        timer_update_display(0);
        lv_label_set_text(timerControlObj->run_op_btn2_label, LV_SYMBOL_PAUSE);
        lv_obj_add_flag(timerControlObj->run_op, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(timerControlObj->run_of, LV_OBJ_FLAG_HIDDEN);
        lv_async_call(run_distry, NULL);
    }
}

void run_create_timer(int32_t ms)
{
    if (timerControlObj->tiemr)
    {
        run_distry();
    }
    timerControlObj->ms = ms;
    timerControlObj->curms = 0;
    timerControlObj->tiemr = lv_timer_create(timer_cul_cb, 1000, NULL);
}

static void bg_timain(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        timer_switch_to_page(timerCenterObj->main_page);
    }
}
static void time_confirm_evet(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        from_preset = false;
        timer_switch_to_page(timerCenterObj->run_page);
    }
}

static void roller_select_evet(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_obj_t *parent = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_VALUE_CHANGED)
    {
        int8_t i = 0;
        for (; i < 5; i++)
        {
            lv_obj_t *child = lv_obj_get_child(parent, i);
            if (!child)
            {
                continue;
            }
            else if (child == obj)
            {
                continue;
            }
            else if (i % 2 == 0)
            {
                lv_obj_set_style_text_color(child, lv_color_white(), 0);
                lv_obj_set_style_text_color(child, lv_color_white(), LV_PART_SELECTED);
            }
        }
        lv_obj_set_style_text_color(obj, lv_color_hex(0x297cf5), 0);
        lv_obj_set_style_text_color(obj, lv_color_hex(0x297cf5), LV_PART_SELECTED);
    }
}
// as
void set_value(int cur)
{
    int c = timerControlObj->index;
    if (c < cur)
    {
        int i = c;
        for (; i < cur; i++)
        {
            lv_obj_set_style_line_color(timerControlObj->ticks[i], lv_color_hex(0x00CEC9), 0);
        }
        timerControlObj->index = cur;
    }
    if (cur == 0)
    {
        int i = 0;
        for (; i < 90; i++)
        {
            lv_obj_set_style_line_color(timerControlObj->ticks[i], lv_color_hex(0x22222E), 0);
        }
        timerControlObj->index = 0;
    }
    lv_obj_invalidate(timerControlObj->time_bg);
}

void run_distry()
{
    if (timerControlObj->tiemr)
    {
        lv_timer_delete(timerControlObj->tiemr);
        timerControlObj->tiemr = NULL;
    }
}

void run_reset()
{
    timerControlObj->finished = false;
    timerControlObj->paused = true;
    timerControlObj->index = 0;
    timerControlObj->ms = 0;
    timerControlObj->curms = 0;
    lv_label_set_text(timerControlObj->run_op_btn2_label, LV_SYMBOL_PLAY);
    lv_obj_remove_flag(timerControlObj->run_op, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(timerControlObj->run_of, LV_OBJ_FLAG_HIDDEN);
    run_distry();
    set_value(0);
}

void run_resume()
{
    if (timerControlObj->tiemr)
    {
        lv_timer_resume(timerControlObj->tiemr);
    }
    else
    {
        /* 恢复时使用剩余时间重新创建定时器 */
        int remaining_ms = timerControlObj->ms - timerControlObj->curms;
        if (remaining_ms > 0)
        {
            int saved_curms = timerControlObj->curms;
            run_create_timer(remaining_ms);
            timerControlObj->curms = saved_curms;
            timer_update_display(remaining_ms);
        }
    }
}
void run_pause()
{
    if (timerControlObj->tiemr)
    {
        lv_timer_pause(timerControlObj->tiemr);
    }
}

static void option_btn1_cb(lv_event_t *e)
{
    run_reset();
    lv_obj_add_flag(timerControlObj->run_op, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(timerControlObj->run_of, LV_OBJ_FLAG_HIDDEN);
}

static void option_btn2_cb(lv_event_t *e)
{
    if (timerControlObj->paused)
    {
        lv_label_set_text(timerControlObj->run_op_btn2_label, LV_SYMBOL_PAUSE);
        timerControlObj->paused = false;
        run_resume();
    }
    else
    {
        lv_label_set_text(timerControlObj->run_op_btn2_label, LV_SYMBOL_PLAY);
        timerControlObj->paused = true;
        run_pause();
    }
}

static void option_finish_btn1_cb(lv_event_t *e)
{
    run_reset();
    timer_switch_to_page(timerCenterObj->main_page);
}

static void option_finish_btn2_cb(lv_event_t *e)
{
    run_reset();
}

void initCb(void)
{
    lv_obj_add_event_cb(timerControlObj->run_op_btn1, option_btn1_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(timerControlObj->run_op_btn2, option_btn2_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(timerControlObj->run_of_btn1, option_finish_btn1_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(timerControlObj->run_of_btn2, option_finish_btn2_cb, LV_EVENT_CLICKED, NULL);
}

// ae

void timer_start()
{
    if (timerCenterObj == NULL)
    {
        timerCenterObj = malloc(sizeof(struct TIMER_CENTER));
        if (timerCenterObj) memset(timerCenterObj, 0, sizeof(struct TIMER_CENTER));
    }
    if (timerControlObj == NULL)
    {
        timerControlObj = malloc(sizeof(struct TIMER_CONTROL));
        if (timerControlObj) memset(timerControlObj, 0, sizeof(struct TIMER_CONTROL));
    }

    lv_obj_t *obj;
    lv_color_t dar = lv_color_hex(0x121212);
    // 设计稿 --bg-base (近黑深蓝)
    lv_color_t bg_base = lv_color_hex(0x06070D);
    // 设计稿 --brand-accent (青色)
    lv_color_t brand_accent = lv_color_hex(0x00CEC9);
    // 设计稿 --bg-card (卡片色)
    lv_color_t bg_card = lv_color_hex(0x1A1A2E);
    // 设计稿 --text-secondary
    lv_color_t text_secondary = lv_color_hex(0xA6ACCD);
    // 设计稿 --text-muted
    lv_color_t text_muted = lv_color_hex(0x5A5F80);
    // 设计稿 --stroke-soft
    lv_color_t stroke_soft = lv_color_hex(0x22222E);

    /* 参照 stopwatch/xiaozhi：先在 watchScr 上创建独立容器，
     * 再在容器内创建 menu 和 UI，手势事件注册在容器上，
     * 避免直接操作 mpd->watchScr（已有表盘手势处理器）。 */
    lv_obj_t *scr = get_watch_scr();
    lv_obj_t *timer_container = lv_obj_create(scr);
    lv_obj_set_size(timer_container, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_align(timer_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(timer_container, dar, 0);
    lv_obj_set_style_bg_opa(timer_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(timer_container, 0, 0);
    lv_obj_set_style_pad_all(timer_container, 0, 0);
    lv_obj_set_scrollbar_mode(timer_container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(timer_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(timer_container, LV_RADIUS_CIRCLE, 0);

    /* 手势事件注册在容器上（不是 watchScr） */
    lv_obj_add_event_cb(timer_container, timer_gesture_event_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_remove_flag(timer_container, LV_OBJ_FLAG_GESTURE_BUBBLE);

    static int32_t zero = 0;

    static lv_style_t baseStyle;
    lv_style_init(&baseStyle);
    lv_style_set_bg_color(&baseStyle, dar);

    lv_style_set_radius(&baseStyle, lv_pct(50));
    lv_style_set_pad_all(&baseStyle, zero);
    lv_style_set_border_width(&baseStyle, 0);
    lv_style_set_outline_width(&baseStyle, zero);
    lv_style_set_border_color(&baseStyle, lv_color_hex(0xe1e6ed));

    /* 用 plain container 替代 lv_menu，解决圆形容器兼容问题 */

    /* 首页容器 */
    lv_obj_t *view = lv_obj_create(timer_container);
    lv_obj_set_size(view, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_align(view, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_style(view, &baseStyle, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(view, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *cont = lv_obj_create(view);
    lv_obj_set_size(cont, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(cont, lv_pct(50), 0);
    lv_obj_set_style_bg_color(cont, dar, 0);
    lv_obj_set_style_pad_all(cont, zero, 0);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_clip_corner(cont, true, 0);
    timerCenterObj->main_page = view;

    /* 选择页容器 */
    lv_obj_t *select_page = lv_obj_create(timer_container);
    timerCenterObj->select_page = select_page;
    lv_obj_set_size(select_page, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_align(select_page, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_style(select_page, &baseStyle, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(select_page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(select_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *cont2 = lv_obj_create(select_page);
    lv_obj_set_size(cont2, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_align(cont2, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(cont2, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_border_width(cont2, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(cont2, lv_color_hex(0xff0000), 0);
    lv_obj_set_style_radius(cont2, lv_pct(50), 0);
    lv_obj_set_style_pad_all(cont2, zero, 0);
    lv_obj_set_scrollbar_mode(cont2, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_clip_corner(cont2, true, 0);
    lv_obj_set_scroll_dir(select_page, LV_DIR_NONE);

    /* 运行页容器 */
    lv_obj_t *run_page = lv_obj_create(timer_container);
    timerCenterObj->run_page = run_page;
    lv_obj_set_size(run_page, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_align(run_page, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_style(run_page, &baseStyle, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(run_page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(run_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *cont3 = lv_obj_create(run_page);
    lv_obj_set_size(cont3, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_align(cont3, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(cont3, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_border_width(cont3, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(cont3, lv_color_hex(0x0000ff), 0);
    lv_obj_set_style_radius(cont3, lv_pct(50), 0);
    lv_obj_set_style_pad_all(cont3, zero, 0);
    lv_obj_set_scrollbar_mode(cont3, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_clip_corner(cont3, true, 0);
    lv_obj_set_scroll_dir(run_page, LV_DIR_NONE);

    // app按钮区
    lv_obj_t *bg_main = lv_obj_create(cont);
    lv_obj_set_style_bg_color(bg_main, lv_color_hex(0x121212), LV_PART_MAIN);
    lv_obj_set_size(bg_main, LV_CIRCLE_WATCH, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(bg_main, 0, 0);
    lv_obj_set_style_outline_width(bg_main, 0, 0);
    lv_obj_set_style_outline_opa(bg_main, 0, 0);
    lv_obj_align(bg_main, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_set_style_radius(bg_main, lv_pct(50), 0);

    lv_obj_set_style_pad_top(bg_main, 50, 0);
    // lv_obj_remove_flag(bg_main,LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    // 上蒙版
    lv_obj_t *flower1;
    flower1 = lv_obj_create(cont);
    lv_obj_set_size(flower1, LV_CIRCLE_WATCH, 50);
    lv_obj_align(flower1, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(flower1, dar, 0);
    lv_obj_set_style_opa(flower1, 104, 0);
    lv_obj_add_flag(flower1, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_border_width(flower1, 0, 0);
    lv_obj_set_style_outline_width(flower1, 0, 0);

    // 下蒙版
    lv_obj_t *flower2;
    flower2 = lv_obj_create(cont);
    lv_obj_set_size(flower2, LV_CIRCLE_WATCH, 50);
    lv_obj_align(flower2, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(flower2, dar, 0);
    lv_obj_set_style_opa(flower2, 104, 0);
    lv_obj_add_flag(flower2, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_border_width(flower2, 0, 0);
    lv_obj_set_style_outline_width(flower2, 0, 0);

    /* 顶部系统时间（与闹钟/秒表一致：montserrat_22, y=20） */
    lv_obj_t *sys_time_label = lv_label_create(cont);
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    tm_info->tm_hour = (tm_info->tm_hour + 8) % 24;
    char time_buf[8];
    strftime(time_buf, sizeof(time_buf), "%H:%M", tm_info);
    lv_label_set_text(sys_time_label, time_buf);
    lv_obj_align(sys_time_label, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_font(sys_time_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(sys_time_label, lv_color_white(), 0);
    lv_obj_add_flag(sys_time_label, LV_OBJ_FLAG_FLOATING);

    /* 顶部标题（montserrat_32, y=20） */
    lv_obj_t *title_label = lv_label_create(cont);
    lv_label_set_text(title_label, "Timer");
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(title_label, 1, 0);
    lv_obj_add_flag(title_label, LV_OBJ_FLAG_FLOATING);
  

    // 先创建一个按钮矩阵的“模板”样式（可按需调整圆角、颜色等）
    static lv_style_t btn_style;
    lv_style_init(&btn_style);
    lv_style_set_radius(&btn_style, 10);
    lv_style_set_bg_color(&btn_style, lv_color_hex(0x333333));
    lv_style_set_text_color(&btn_style, lv_color_hex(0xFFFFFF));
    lv_style_set_pad_all(&btn_style, 8);

    /**************************
     * 6. 底部“+”按钮（添加自定义时间）
     *************************/
    lv_obj_t *add_btn = lv_btn_create(cont);
    lv_obj_set_size(add_btn, 60, 40);
    lv_obj_add_style(add_btn, &btn_style, 0);
    lv_obj_align(add_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_flag(add_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_radius(add_btn, 20, 0);
    lv_obj_set_style_bg_color(add_btn, lv_color_hex(0x297cf7), 0);
    lv_obj_t *add_btn_label = lv_label_create(add_btn);
    lv_label_set_text(add_btn_label, LV_SYMBOL_PLUS);
   
    lv_obj_center(add_btn_label);
    // “+”按钮点击事件（可弹出窗口让用户输入自定义时间等）
    lv_obj_add_event_cb(add_btn, time_clu_add_evet, LV_EVENT_ALL, NULL);

    // 按钮矩阵的文字内容，每行 2 个按钮
    lv_obj_t *btn_max_1 = lv_obj_create(bg_main);
    lv_obj_set_style_border_width(btn_max_1, 0, 0);
    lv_obj_set_style_outline_width(btn_max_1, 0, 0);
    lv_obj_set_size(btn_max_1, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_align_to(btn_max_1, bg_main, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_layout(btn_max_1, LV_LAYOUT_FLEX);
    lv_obj_set_style_pad_top(btn_max_1, 10, 0);
    lv_obj_set_style_pad_bottom(btn_max_1, 10, 0);
    lv_obj_set_style_pad_left(btn_max_1, 20, 0);
    lv_obj_set_style_pad_right(btn_max_1, 20, 0);
    lv_obj_set_style_radius(btn_max_1, 108, 0);
    lv_obj_set_flex_flow(btn_max_1, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_flex_main_place(btn_max_1, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
    lv_obj_set_style_bg_color(btn_max_1, dar, 0);

    static const char *btn_nums[] = {"1", "2", "3", "5", "10", "30"};
    static const char *btn_levs[] = {"minute", "minute", "minute", "minute", "minute", "minute"};
    static lv_style_t btn_max_style;
    lv_style_init(&btn_max_style);
    // lv_style_set_size(&btn_max_style, 90, 50);
    lv_style_set_size(&btn_max_style, 148, 82); // 修改
    lv_style_set_bg_color(&btn_max_style, lv_color_hex(0x252523));
    // lv_style_set_radius(&btn_max_style, 30);
    lv_style_set_radius(&btn_max_style, 50); // 修改
    lv_style_set_border_width(&btn_max_style, 0);
    lv_style_set_outline_width(&btn_max_style, 0);
    lv_style_set_layout(&btn_max_style, 1);
    lv_style_set_flex_flow(&btn_max_style, LV_FLEX_FLOW_COLUMN);
    lv_style_set_flex_main_place(&btn_max_style, LV_FLEX_ALIGN_CENTER);
    lv_style_set_flex_cross_place(&btn_max_style, 2);
    lv_style_set_flex_track_place(&btn_max_style, 2);
    lv_obj_set_scrollbar_mode(btn_max_1, LV_SCROLLBAR_MODE_OFF);
    uint32_t i;
    int len = sizeof(btn_nums) / sizeof(btn_nums[0]);
    for (i = 0; i < len; i++)
    {
        lv_obj_t *obj;
        lv_obj_t *label1;
        lv_obj_t *label2;

        /*Add items to the row*/
        obj = lv_button_create(btn_max_1);
        lv_obj_add_style(obj, &btn_max_style, 0);
        lv_obj_add_event_cb(obj, time_clu_btns_event, LV_EVENT_ALL, (void *)i);

        label1 = lv_label_create(obj);
        lv_label_set_text_fmt(label1, btn_nums[i]);
        lv_obj_center(label1);
  

        label2 = lv_label_create(obj);
        lv_label_set_text_fmt(label2, btn_levs[i]);
        lv_obj_center(label2);

    }

    /* 初始显示 main_page，隐藏其他页 */
    timer_switch_to_page(timerCenterObj->main_page);

    // main区
    lv_obj_set_style_pad_all(cont2, 0, 0);
    lv_obj_t *bg_main_2 = lv_obj_create(cont2);
    lv_obj_set_style_bg_color(bg_main_2, lv_color_hex(0x121212), LV_PART_MAIN);
    lv_obj_set_size(bg_main_2, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_set_style_border_width(bg_main_2, 0, 0);
    lv_obj_set_style_outline_width(bg_main_2, 0, 0);
    lv_obj_set_style_outline_opa(bg_main_2, 0, 0);
    lv_obj_align(bg_main_2, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_radius(bg_main_2, lv_pct(50), 0);
    // lv_obj_set_style_pad_top(bg_main_2,180,0);
    lv_obj_set_scrollbar_mode(bg_main_2, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(bg_main_2, LV_DIR_NONE);
    // lv_obj_remove_flag(bg_main,LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    // 主要选择区
    lv_obj_t *select_option = lv_obj_create(bg_main_2);
    lv_obj_set_size(select_option, 330, 248);
    lv_obj_align(select_option, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_color(select_option, dar, 0);
    lv_obj_set_style_border_width(select_option, 0, 0);
    lv_obj_set_style_outline_width(select_option, 0, 0);
    lv_obj_set_layout(select_option, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(select_option, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(select_option, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
    lv_obj_set_style_flex_cross_place(select_option, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(select_option, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_scroll_dir(select_option, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(select_option, LV_SCROLLBAR_MODE_OFF);

    static lv_style_t roller_style;
    lv_style_init(&roller_style);
    lv_style_set_bg_color(&roller_style, dar);
    lv_style_set_text_color(&roller_style, lv_color_white());
    lv_style_set_border_width(&roller_style, 0);
    lv_style_set_outline_width(&roller_style, 0);
    lv_style_set_radius(&roller_style, 0);
 
    static lv_style_t roller_speli_labe_style;
    lv_style_init(&roller_speli_labe_style);
    lv_style_set_text_color(&roller_speli_labe_style, lv_color_white());
    lv_style_set_text_align(&roller_speli_labe_style, LV_ALIGN_CENTER);
   

    lv_obj_t *roller_hour = lv_roller_create(select_option);
    lv_roller_set_options(roller_hour,
                          roller_options,
                          LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(roller_hour, 3);
    lv_obj_add_style(roller_hour, &roller_style, 0);
    lv_roller_set_selected(roller_hour, 9, LV_ANIM_OFF);
    lv_obj_set_size(roller_hour, 100, 330);
    lv_obj_set_style_bg_color(roller_hour, dar, LV_PART_SELECTED);
  
    lv_obj_add_event_cb(roller_hour, roller_select_evet, LV_EVENT_ALL, select_option);

    lv_obj_t *roll_lable1 = lv_label_create(select_option);
    lv_obj_add_style(roll_lable1, &roller_speli_labe_style, 0);
    lv_label_set_text_fmt(roll_lable1, ":");

    lv_obj_t *roller_minute = lv_roller_create(select_option);
    lv_roller_set_options(roller_minute, muse_str, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(roller_minute, 3);
    lv_obj_add_style(roller_minute, &roller_style, 0);
    lv_roller_set_selected(roller_minute, 30, LV_ANIM_OFF);
    lv_obj_set_size(roller_minute, 100, 330);
    lv_obj_set_style_bg_color(roller_minute, dar, LV_PART_SELECTED);

    lv_obj_add_event_cb(roller_minute, roller_select_evet, LV_EVENT_ALL, select_option);

    lv_obj_t *roll_lable2 = lv_label_create(select_option);
    lv_obj_add_style(roll_lable2, &roller_speli_labe_style, 0);
    lv_label_set_text(roll_lable2, ":");

    lv_obj_t *roller_second = lv_roller_create(select_option);
    lv_roller_set_options(roller_second, muse_str, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(roller_second, 3);
    lv_obj_add_style(roller_second, &roller_style, 0);
    lv_roller_set_selected(roller_second, 0, LV_ANIM_OFF);
    lv_obj_set_size(roller_second, 100, 330);
    lv_obj_set_style_bg_color(roller_second, dar, LV_PART_SELECTED);

    lv_obj_add_event_cb(roller_second, roller_select_evet, LV_EVENT_ALL, select_option);

    // 上蒙版
    lv_obj_t *flower1_2;
    flower1_2 = lv_obj_create(cont2);
    lv_obj_set_size(flower1_2, LV_CIRCLE_WATCH, 100);
    lv_obj_align(flower1_2, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(flower1_2, dar, 0);
    lv_obj_add_flag(flower1_2, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_border_width(flower1_2, 0, 0);
    lv_obj_set_style_outline_width(flower1_2, 0, 0);
    lv_obj_set_style_shadow_color(flower1_2, dar, 0);
    lv_obj_set_style_shadow_width(flower1_2, 40, 0);
    lv_obj_set_style_shadow_offset_y(flower1_2, 20, 0);

    // 下蒙版
    lv_obj_t *flower2_2;
    flower2_2 = lv_obj_create(cont2);
    lv_obj_set_size(flower2_2, LV_CIRCLE_WATCH, 100);
    lv_obj_align(flower2_2, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(flower2_2, dar, 0);
    lv_obj_add_flag(flower2_2, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_border_width(flower2_2, 0, 0);
    lv_obj_set_style_outline_width(flower2_2, 0, 0);
    lv_obj_set_style_shadow_color(flower2_2, dar, 0);
    lv_obj_set_style_shadow_width(flower2_2, 40, 0);
    lv_obj_set_style_shadow_offset_y(flower2_2, -20, 0);

    /* 顶部系统时间（与闹钟/秒表一致：montserrat_22, y=20） */
    lv_obj_t *sys_time_label_2 = lv_label_create(flower1_2);
    lv_obj_align(sys_time_label_2, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_font(sys_time_label_2, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(sys_time_label_2, lv_color_white(), 0);
    lv_obj_add_flag(sys_time_label_2, LV_OBJ_FLAG_FLOATING);

    /* 顶部标题（montserrat_32, y=20） */
    lv_obj_t *title_label1_2 = lv_label_create(flower1_2);
    lv_label_set_text(title_label1_2, "Timer");
    lv_obj_align(title_label1_2, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_text_font(title_label1_2, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title_label1_2, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(title_label1_2, 1, 0);
    lv_obj_add_flag(title_label1_2, LV_OBJ_FLAG_FLOATING);


    // 选中lable
    lv_obj_t *sele_labe = lv_label_create(flower1_2);
    lv_label_set_text(sele_labe, "hour");
    lv_obj_set_style_text_color(sele_labe, lv_color_hex(0x297cf5), 0); // 0x03040a

    lv_obj_align(sele_labe, LV_ALIGN_BOTTOM_MID, 0, 10);
    lv_obj_add_flag(sele_labe, LV_OBJ_FLAG_FLOATING);

    // 6. 底部“>”按钮（确认自定义时间）
    lv_obj_t *con_btn = lv_btn_create(flower2_2);
    lv_obj_set_size(con_btn, 60, 40);
    lv_obj_add_style(con_btn, &btn_style, 0);
    lv_obj_align(con_btn, LV_ALIGN_TOP_MID, 0, -4);
    lv_obj_add_flag(con_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_radius(con_btn, 20, 0);
    lv_obj_set_style_bg_color(con_btn, lv_color_hex(0x297cf7), 0);
    lv_obj_t *con_btn_label = lv_label_create(con_btn);
    lv_label_set_text(con_btn_label, LV_SYMBOL_PLAY);

    lv_obj_center(con_btn_label);
    // “+”按钮点击事件（可弹出窗口让用户输入自定义时间等）
    lv_obj_add_event_cb(con_btn, time_confirm_evet, LV_EVENT_ALL, NULL);

    // as
    // run main区
    lv_obj_set_style_pad_all(cont3, 0, 0);
    lv_obj_t *bg_main_3 = lv_obj_create(cont3);
    lv_obj_set_style_bg_color(bg_main_3, bg_base, LV_PART_MAIN);
    lv_obj_set_size(bg_main_3, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_set_style_border_width(bg_main_3, 0, 0);
    lv_obj_set_style_outline_width(bg_main_3, 0, 0);
    lv_obj_set_style_outline_opa(bg_main_3, 0, 0);
    lv_obj_align(bg_main_3, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(bg_main_3, lv_pct(50), 0);
    lv_obj_set_style_pad_all(bg_main_3, 0, 0);
    lv_obj_set_scrollbar_mode(bg_main_3, LV_SCROLLBAR_MODE_OFF);
    // lv_obj_set_scroll_dir(bg_main_3, LV_DIR_NONE);

    /* 创建背景环形 */
    lv_obj_t *bg_run = lv_obj_create(bg_main_3);
    lv_obj_set_size(bg_run, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_center(bg_run);
    lv_obj_set_style_radius(bg_run, lv_pct(50), 0);
    lv_obj_set_style_bg_color(bg_run, bg_base, 0);
    lv_obj_set_style_border_width(bg_run, 0, 0);
    lv_obj_set_style_outline_width(bg_run, 0, 0);
    lv_obj_set_style_pad_all(bg_run, 0, 0);
    timerControlObj->time_bg = bg_run;

    static lv_style_t style_line;
    lv_style_init(&style_line);
    lv_style_set_line_width(&style_line, 6);
    lv_style_set_line_color(&style_line, stroke_soft);

    static lv_point_precise_t points[90][2];

    /* 添加刻度线 */
    const int tick_count = 90;
    int center_x = 228;
    int center_y = 228;
    int outer_radius = 222;
    int inner_radius = 200;
    for (int i = 0; i < tick_count; i++)
    {
        double angle_deg = (i * 4.0) - 90.0;
        double angle_rad = DEG_TO_RAD(angle_deg); // 使用自定义转换宏

        // 创建刻度线
        timerControlObj->ticks[i] = lv_line_create(bg_run);

        // 设置刻度线样式（默认灰色）
        lv_obj_add_style(timerControlObj->ticks[i], &style_line, 0);
        // 注: 不再调用 lv_obj_set_style_transform_rotation(angle_rad)。
        // LVGL transform_rotation 单位是 0.1 度，传弧度(double→int32 截断为 -1..4)
        // 会使每根线进入 LV_LAYER_TYPE_TRANSFORM 软件旋转渲染路径，
        // 90 根线 × 455×455 区域单帧绘制爆炸 → 整个表盘卡死。
        // 端点已用 cos/sin 表达角度，无需再旋转对象。
        // 计算刻度线端点
        points[i][0].x = center_x + cos(angle_rad) * outer_radius;
        points[i][0].y = center_y + sin(angle_rad) * outer_radius;

        int current_inner_radius = inner_radius;
        points[i][1].x = center_x + cos(angle_rad) * current_inner_radius;
        points[i][1].y = center_y + sin(angle_rad) * current_inner_radius;

        lv_line_set_points(timerControlObj->ticks[i], points[i], 2);
    }

    lv_obj_t *run_center = lv_obj_create(bg_run);
    lv_obj_center(run_center);
    lv_obj_set_size(run_center, 380, 380);
    lv_obj_set_style_radius(run_center, lv_pct(50), 0);
    lv_obj_add_style(run_center, &baseStyle, 0);
    // 设计稿背景环 (--stroke-soft 描边)
    lv_obj_set_style_border_color(run_center, stroke_soft, 0);
    lv_obj_set_style_border_width(run_center, 8, 0);
    lv_obj_set_style_border_opa(run_center, LV_OPA_COVER, 0);

    lv_obj_t *run_time_lable = lv_label_create(run_center);
    lv_obj_align(run_time_lable, LV_ALIGN_TOP_MID, 0, 120);
    lv_label_set_text(run_time_lable, "09:00");

    lv_obj_set_style_text_color(run_time_lable, lv_color_white(), 0);
    timerControlObj->run_oc_label = run_time_lable;

    // 计时结束
    lv_obj_t *run_option_finish = lv_obj_create(run_center);
    lv_obj_set_size(run_option_finish, lv_pct(100), 240);
    lv_obj_add_style(run_option_finish, &baseStyle, 0);
    lv_obj_align(run_option_finish, LV_ALIGN_CENTER, 0, 0);
    timerControlObj->run_of = run_option_finish;

    lv_obj_set_layout(run_option_finish, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(run_option_finish, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(run_option_finish, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
    lv_obj_set_style_flex_cross_place(run_option_finish, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(run_option_finish, LV_FLEX_ALIGN_CENTER, 0);

    lv_obj_t *run_option_finish_lable = lv_label_create(run_option_finish);
    lv_label_set_text(run_option_finish_lable, "Countdown\nended");

    lv_obj_set_style_text_color(run_option_finish_lable, lv_color_white(), 0);
    lv_obj_set_style_text_align(run_option_finish_lable, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *run_option_finish_time = lv_label_create(run_option_finish);
    lv_label_set_text(run_option_finish_time, "00:09.00");
    lv_obj_set_style_text_font(run_option_finish_time, &lv_font_montserrat_24, 0);

    lv_obj_set_style_text_color(run_option_finish_time, text_muted, 0);
    timerControlObj->run_of_time_label = run_option_finish_time;

    lv_obj_t *run_option_finish_btn_box = lv_obj_create(run_option_finish);
    lv_obj_set_size(run_option_finish_btn_box, lv_pct(100), 100);
    lv_obj_add_style(run_option_finish_btn_box, &baseStyle, 0);
    lv_obj_set_layout(run_option_finish_btn_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(run_option_finish_btn_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(run_option_finish_btn_box, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(run_option_finish_btn_box, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(run_option_finish_btn_box, LV_FLEX_ALIGN_CENTER, 0);
    // 设计稿 gap — 按钮列间距
    lv_obj_set_style_pad_column(run_option_finish_btn_box, 20, 0);

    lv_obj_t *run_option_finish_btn1 = lv_btn_create(run_option_finish_btn_box);
    lv_obj_set_size(run_option_finish_btn1, 60, 60);
    lv_obj_add_style(run_option_finish_btn1, &btn_style, 0);
    lv_obj_set_style_radius(run_option_finish_btn1, 30, 0);
    lv_obj_set_style_bg_color(run_option_finish_btn1, bg_card, 0);
    timerControlObj->run_of_btn1 = run_option_finish_btn1;

    lv_obj_t *run_option_finish_label = lv_label_create(run_option_finish_btn1);
    lv_label_set_text(run_option_finish_label, LV_SYMBOL_CLOSE);

    lv_obj_set_style_text_color(run_option_finish_label, lv_color_white(), 0);
    lv_obj_center(run_option_finish_label);

    lv_obj_t *run_option_finish_btn2 = lv_btn_create(run_option_finish_btn_box);
    lv_obj_set_size(run_option_finish_btn2, 60, 60);
    lv_obj_add_style(run_option_finish_btn2, &btn_style, 0);
    lv_obj_set_style_radius(run_option_finish_btn2, 30, 0);
    lv_obj_set_style_bg_color(run_option_finish_btn2, brand_accent, 0);
    // 设计稿 --glow-accent 发光
    lv_obj_set_style_shadow_color(run_option_finish_btn2, brand_accent, 0);
    lv_obj_set_style_shadow_width(run_option_finish_btn2, 30, 0);
    lv_obj_set_style_shadow_spread(run_option_finish_btn2, 4, 0);
    timerControlObj->run_of_btn2 = run_option_finish_btn2;

    lv_obj_t *run_option_finish_btn2_label = lv_label_create(run_option_finish_btn2);
    lv_label_set_text(run_option_finish_btn2_label, LV_SYMBOL_REFRESH);

    lv_obj_set_style_text_color(run_option_finish_btn2_label, lv_color_white(), 0);
    lv_obj_center(run_option_finish_btn2_label);

    lv_obj_t *run_option = lv_obj_create(run_center);
    lv_obj_set_size(run_option, lv_pct(100), 240);
    lv_obj_add_style(run_option, &baseStyle, 0);
    lv_obj_align(run_option, LV_ALIGN_CENTER, 0, 0);
    timerControlObj->run_op = run_option;

    lv_obj_set_layout(run_option, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(run_option, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(run_option, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
    lv_obj_set_style_flex_cross_place(run_option, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(run_option, LV_FLEX_ALIGN_CENTER, 0);

    lv_obj_t *run_option_time = lv_label_create(run_option);
    lv_label_set_text(run_option_time, "09:00.00");
    lv_obj_set_style_text_font(run_option_time, &lv_font_montserrat_24, 0);

    lv_obj_set_style_text_color(run_option_time, lv_color_white(), 0);
    timerControlObj->run_op_time_label = run_option_time;

    // 设计稿"剩余时间"副标签 (secondary 色)
    lv_obj_t *run_op_sub_lable = lv_label_create(run_option);
    lv_obj_align_to(run_op_sub_lable, run_option_time, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
    lv_label_set_text(run_op_sub_lable, "剩余时间");
    lv_obj_set_style_text_color(run_op_sub_lable, text_secondary, 0);
    lv_obj_set_style_text_font(run_op_sub_lable, font_manager_get_font(FONT_SMALL), 0);

    lv_obj_t *run_option_btn_box = lv_obj_create(run_option);
    lv_obj_set_size(run_option_btn_box, lv_pct(100), 100);
    lv_obj_add_style(run_option_btn_box, &baseStyle, 0);
    lv_obj_set_layout(run_option_btn_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(run_option_btn_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(run_option_btn_box, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(run_option_btn_box, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(run_option_btn_box, LV_FLEX_ALIGN_CENTER, 0);
    // 设计稿 gap:18px — 按钮列间距
    lv_obj_set_style_pad_column(run_option_btn_box, 20, 0);

    lv_obj_t *run_option_btn1 = lv_btn_create(run_option_btn_box);
    lv_obj_set_size(run_option_btn1, 60, 60);
    lv_obj_add_style(run_option_btn1, &btn_style, 0);
    lv_obj_set_style_radius(run_option_btn1, 30, 0);
    lv_obj_set_style_bg_color(run_option_btn1, bg_card, 0);
    // lv_obj_set_user_data(run_option_btn1,false);
    timerControlObj->run_op_btn1 = run_option_btn1;

    lv_obj_t *run_option_btn1_label = lv_label_create(run_option_btn1);
    lv_label_set_text(run_option_btn1_label, LV_SYMBOL_STOP);

    lv_obj_set_style_text_color(run_option_btn1_label, lv_color_white(), 0);
    lv_obj_center(run_option_btn1_label);

    lv_obj_t *run_option_btn2 = lv_btn_create(run_option_btn_box);
    lv_obj_set_size(run_option_btn2, 60, 60);
    lv_obj_add_style(run_option_btn2, &btn_style, 0);
    lv_obj_set_style_radius(run_option_btn2, 30, 0);
    lv_obj_set_style_bg_color(run_option_btn2, brand_accent, 0);
    // 设计稿 --glow-accent 发光
    lv_obj_set_style_shadow_color(run_option_btn2, brand_accent, 0);
    lv_obj_set_style_shadow_width(run_option_btn2, 30, 0);
    lv_obj_set_style_shadow_spread(run_option_btn2, 4, 0);
    timerControlObj->run_op_btn2 = run_option_btn2;

    lv_obj_t *run_option_btn2_label = lv_label_create(run_option_btn2);
    lv_label_set_text(run_option_btn2_label, LV_SYMBOL_PLAY);
  
    lv_obj_set_style_text_color(run_option_btn2_label, lv_color_white(), 0);
    lv_obj_center(run_option_btn2_label);
    timerControlObj->run_op_btn2_label = run_option_btn2_label;

    // 隐藏切换
    lv_obj_add_flag(run_option_finish, LV_OBJ_FLAG_HIDDEN);
    timerControlObj->tiemr = NULL;
    timerControlObj->finished = false;
    timerControlObj->paused = true;
    timerControlObj->index = 0;
    timerControlObj->ms = 0;
    timerControlObj->curms = 0;
    initCb();
}

void timer_app_start()
{
    timer_start();
}
// new code
