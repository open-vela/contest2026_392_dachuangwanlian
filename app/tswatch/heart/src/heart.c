/*********************
 *      INCLUDES
 *********************/
#include <lvgl/lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../heart/include/heart.h"
#include "../../utils/include/circle_screen.h"
#include "../../utils/include/font_manager.h"
#include "../../main_page/include/watch_start.h"

/*********************
 *      DEFINES
 *********************/
#define SCR_W   LV_CIRCLE_WATCH   /* 455 */
#define SCR_H   LV_CIRCLE_WATCH

/* 配色 */
#define BG_BASE         lv_color_hex(0x06070D)
#define BG_CARD         lv_color_hex(0x131520)
#define TEXT_PRIMARY    lv_color_hex(0xF8F9FF)
#define TEXT_SECONDARY  lv_color_hex(0xA6ACCD)
#define TEXT_MUTED      lv_color_hex(0x5A5F80)
#define STROKE_SOFT     lv_color_hex(0x1F2133)
#define BRAND_RED       lv_color_hex(0xFF6B6B)

/* 心率区间色（与 HTML 设计稿一致） */
#define HR_ZONE1_COLOR  lv_color_hex(0xFFD32A)   /* 热身 */
#define HR_ZONE2_COLOR  lv_color_hex(0x26DE81)   /* 燃脂 */
#define HR_ZONE3_COLOR  lv_color_hex(0x00CEC9)   /* 有氧 */
#define HR_ZONE4_COLOR  lv_color_hex(0xFF6B6B)   /* 极限 */

/* 心电图参数 */
#define ECG_W           310
#define ECG_H           60
#define ECG_STEP        6
#define ECG_PERIOD_MS   40

/**********************
 *      TYPEDEFS
 **********************/
/* 静态心率数据（演示用） */
typedef struct {
    int current_hr;       /* 当前心率 */
    int resting_hr;       /* 静息心率 */
    int max_hr;           /* 最大心率 */
    int min_hr;           /* 最小心率 */
    const char *zone_name;/* 当前区间名 */
    int zone_pct;         /* 当前区间占比 % */
    const char *measure_time; /* 测量时间 */
    /* 4个区间占比（热身/燃脂/有氧/极限） */
    int zones[4];
    const char *zone_dur[4]; /* 各区间时长 */
} heart_data_t;

static const heart_data_t s_data = {
    .current_hr   = 72,
    .resting_hr   = 62,
    .max_hr       = 98,
    .min_hr       = 58,
    .zone_name    = "燃脂",
    .zone_pct     = 45,
    .measure_time = "09:35",
    .zones        = { 15, 45, 25, 15 },  /* 热身/燃脂/有氧/极限 */
    .zone_dur     = { "12分", "35分", "18分", "5分" },
};

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_obj_t *s_container = NULL;
static lv_obj_t *s_ecg_line  = NULL;
static lv_timer_t *s_ecg_timer = NULL;
static int s_ecg_offset = 0;

static lv_obj_t *s_hr_label  = NULL;
static lv_timer_t *s_hr_timer = NULL;
static int s_current_hr = 72;
static lv_point_t s_ecg_pts[(ECG_W + ECG_STEP - 1) / ECG_STEP];

/**********************
 *   ECG PULSE DATA
 **********************/
static const uint8_t ecg_pattern[] = {
    30,30,30,30,30,30,30,30,
    30,28,26,25,26,28,30,
    30,30,30,30,
    30,32,38,45,8,48,30,
    30,30,30,30,30,
    30,27,24,23,24,27,30,
    30,30,30,30,30,30,30,30,30,30,
};
#define ECG_PATTERN_LEN  (sizeof(ecg_pattern))

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void gesture_cb(lv_event_t *e);
static void ecg_update_line(int offset);
static void ecg_tick(lv_timer_t *timer);

/**********************
 *   STATIC FUNCTIONS
 **********************/

/* 心电图滚动更新 */
static void ecg_update_line(int offset)
{
    int pat_len = (int)ECG_PATTERN_LEN;
    int n_pts = (ECG_W + ECG_STEP - 1) / ECG_STEP;
    for (int i = 0; i < n_pts; i++) {
        int idx = (i + offset) % pat_len;
        if (idx < 0) idx += pat_len;
        int y = ECG_H - 1 - ecg_pattern[idx];
        if (y < 0) y = 0;
        if (y >= ECG_H) y = ECG_H - 1;
        s_ecg_pts[i].x = i * ECG_STEP;
        s_ecg_pts[i].y = y;
    }
    lv_line_set_points(s_ecg_line, s_ecg_pts, n_pts);
}

static void ecg_tick(lv_timer_t *timer)
{
    (void)timer;
    if (!s_ecg_line) return;
    s_ecg_offset++;
    ecg_update_line(s_ecg_offset);
}

/* 心率随机变化：每2秒，±5，范围72-142 */
static void hr_tick(lv_timer_t *timer)
{
    (void)timer;
    if (!s_hr_label) return;

    int delta = (rand() % 11) - 5;  /* -5 ~ +5 */
    s_current_hr += delta;
    if (s_current_hr < 72)  s_current_hr = 72;
    if (s_current_hr > 142) s_current_hr = 142;

    lv_label_set_text_fmt(s_hr_label, "%d", s_current_hr);
}

/* 手势回调：右滑退出 */
static void gesture_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir != LV_DIR_RIGHT) return;

    LV_LOG_USER("heart: gesture RIGHT -> exit");
    if (s_ecg_timer) { lv_timer_del(s_ecg_timer); s_ecg_timer = NULL; }
    if (s_hr_timer) { lv_timer_del(s_hr_timer); s_hr_timer = NULL; }
    if (s_container) {
        lv_obj_delete(s_container);
        s_container = NULL;
        s_ecg_line = NULL;
        s_hr_label = NULL;
    }
    nav_return_from_app();
}

/* 创建一个指标卡片行 */
static lv_obj_t *create_metric_row(lv_obj_t *parent, const char *label,
                                    const char *value, lv_color_t val_color)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 310, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_bg_color(row, BG_CARD, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, STROKE_SOFT, 0);
    lv_obj_set_style_pad_all(row, 14, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(lbl, TEXT_SECONDARY, 0);

    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, value);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(val, val_color, 0);

    return row;
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void heart_start(void)
{
    lv_obj_t *scr = get_watch_scr();
    if (!scr) return;

    /* 根容器 */
    s_container = lv_obj_create(scr);
    lv_obj_set_size(s_container, SCR_W, SCR_H);
    lv_obj_align(s_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_container, BG_BASE, 0);
    lv_obj_set_style_bg_opa(s_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_container, 0, 0);
    lv_obj_set_style_pad_all(s_container, 0, 0);
    lv_obj_set_scrollbar_mode(s_container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_container, LV_RADIUS_CIRCLE, 0);

    /* 手势 */
    lv_obj_add_event_cb(s_container, gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_remove_flag(s_container, LV_OBJ_FLAG_GESTURE_BUBBLE);

    /* ---- 内容容器（垂直 Flex 居中）---- */
    lv_obj_t *content = lv_obj_create(s_container);
    lv_obj_set_size(content, SCR_W, SCR_H);
    lv_obj_align(content, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_layout(content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    /* ============ 顶部系统时间 ============ */
    lv_obj_t *sys_time = lv_label_create(s_container);
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    tm_info->tm_hour = (tm_info->tm_hour + 8) % 24;
    char time_buf[8];
    strftime(time_buf, sizeof(time_buf), "%H:%M", tm_info);
    lv_label_set_text(sys_time, time_buf);
    lv_obj_set_style_text_font(sys_time, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(sys_time, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(sys_time, 1, 0);
    lv_obj_align(sys_time, LV_ALIGN_TOP_MID, 0, 10);

    /* ============ 标题区域 ============ */
    lv_obj_t *title = lv_label_create(content);
    lv_label_set_text(title, "Heartrate");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(title, 1, 0);
    lv_obj_set_style_pad_top(title, 30, 0);

    /* ============ 心率大数字 ============ */
    s_current_hr = s_data.current_hr;
    s_hr_label = lv_label_create(content);
    lv_label_set_text_fmt(s_hr_label, "%d", s_current_hr);
    lv_obj_set_style_text_font(s_hr_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_hr_label, BRAND_RED, 0);
    lv_obj_set_style_pad_top(s_hr_label, 10, 0);

    /* bpm 单位 */
    lv_obj_t *hr_unit = lv_label_create(content);
    lv_label_set_text(hr_unit, "bpm");
    lv_obj_set_style_text_font(hr_unit, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(hr_unit, TEXT_SECONDARY, 0);
    lv_obj_set_style_pad_top(hr_unit, 2, 0);

    /* ============ 心电图动效 ============ */
    lv_obj_t *ecg_box = lv_obj_create(content);
    lv_obj_set_size(ecg_box, ECG_W, 80);
    lv_obj_set_style_radius(ecg_box, 16, 0);
    lv_obj_set_style_bg_color(ecg_box, lv_color_hex(0x1A0A0A), 0);
    lv_obj_set_style_bg_opa(ecg_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ecg_box, 1, 0);
    lv_obj_set_style_border_color(ecg_box, lv_color_hex(0x331515), 0);
    lv_obj_set_style_pad_all(ecg_box, 0, 0);
    lv_obj_clear_flag(ecg_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_top(ecg_box, 28, 0);

    s_ecg_line = lv_line_create(ecg_box);
    lv_obj_set_size(s_ecg_line, ECG_W, ECG_H);
    lv_obj_set_style_line_color(s_ecg_line, BRAND_RED, 0);
    lv_obj_set_style_line_width(s_ecg_line, 2, 0);
    lv_obj_set_style_line_opa(s_ecg_line, LV_OPA_COVER, 0);
    lv_obj_set_style_line_rounded(s_ecg_line, 1, 0);
    lv_obj_set_style_pad_top(s_ecg_line, 5, 0);
    ecg_update_line(0);

    if (s_ecg_timer) lv_timer_del(s_ecg_timer);
    s_ecg_offset = 0;
    s_ecg_timer = lv_timer_create(ecg_tick, ECG_PERIOD_MS, NULL);

    /* 心率随机变化定时器 */
    srand(time(NULL));
    if (s_hr_timer) lv_timer_del(s_hr_timer);
    s_hr_timer = lv_timer_create(hr_tick, 2000, NULL);

    /* ============ 心率区间列表（纵向） ============ */
    const char *zone_names[] = {"热身", "燃脂", "有氧", "极限"};
    lv_color_t zone_colors[] = {
        HR_ZONE1_COLOR, HR_ZONE2_COLOR, HR_ZONE3_COLOR, HR_ZONE4_COLOR
    };

    lv_obj_t *zone_list = lv_obj_create(content);
    lv_obj_set_size(zone_list, 310, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(zone_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(zone_list, 0, 0);
    lv_obj_set_style_pad_all(zone_list, 0, 0);
    lv_obj_set_style_pad_row(zone_list, 8, 0);
    lv_obj_set_layout(zone_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(zone_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(zone_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_top(zone_list, 18, 0);

    for (int i = 0; i < 4; i++) {
        lv_obj_t *row = lv_obj_create(zone_list);
        lv_obj_set_size(row, 310, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 10, 0);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        /* 左侧标签 */
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, zone_names[i]);
        lv_obj_set_style_text_font(lbl, font_manager_get_font(FONT_SMALL), 0);
        lv_obj_set_style_text_color(lbl, TEXT_MUTED, 0);
        lv_obj_set_style_width(lbl, 50, 0);

        /* 区间进度条 */
        lv_obj_t *bar_bg = lv_obj_create(row);
        lv_obj_set_size(bar_bg, 180, 10);
        lv_obj_set_style_radius(bar_bg, 5, 0);
        lv_obj_set_style_bg_color(bar_bg, BG_CARD, 0);
        lv_obj_set_style_bg_opa(bar_bg, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar_bg, 0, 0);
        lv_obj_set_style_pad_all(bar_bg, 0, 0);
        lv_obj_clear_flag(bar_bg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_clip_corner(bar_bg, true, 0);

        int fill_w = 180 * s_data.zones[i] / 100;
        if (fill_w < 2) fill_w = 2;
        lv_obj_t *bar_fill = lv_obj_create(bar_bg);
        lv_obj_set_size(bar_fill, fill_w, 10);
        lv_obj_set_style_radius(bar_fill, 5, 0);
        lv_obj_set_style_bg_color(bar_fill, zone_colors[i], 0);
        lv_obj_set_style_bg_opa(bar_fill, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar_fill, 0, 0);
        lv_obj_clear_flag(bar_fill, LV_OBJ_FLAG_SCROLLABLE);

        /* 右侧时长 */
        lv_obj_t *dur = lv_label_create(row);
        lv_label_set_text(dur, s_data.zone_dur[i]);
        lv_obj_set_style_text_font(dur, font_manager_get_font(FONT_SMALL), 0);
        lv_obj_set_style_text_color(dur, TEXT_SECONDARY, 0);
    }

    /* ============ 测量时间 ============ */
    lv_obj_t *measure_row = create_metric_row(content, "最近测量", s_data.measure_time, BRAND_RED);
    lv_obj_set_style_pad_top(measure_row, 10, 0);
}
