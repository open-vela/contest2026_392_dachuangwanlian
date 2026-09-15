/*********************
 *      INCLUDES
 *********************/
#include <lvgl/lvgl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "../../exercise/include/exercise.h"
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
#define BRAND_GREEN     lv_color_hex(0x26DE81)
#define BRAND_RED       lv_color_hex(0xFF6B6B)

/* 心电图参数 */
#define ECG_W           310
#define ECG_H           40
#define ECG_STEP        6      /* 每帧平移像素 */
#define ECG_PERIOD_MS   40     /* 定时器间隔 ms */

/**********************
 *      TYPEDEFS
 **********************/
/* 静态运动数据（演示用） */
typedef struct {
    const char *goal_text;
    const char *last_km;
    const char *last_pace;
    const char *last_kcal;
    const char *distance;
    const char *duration;
    const char *pace;
    int hr;
} exercise_data_t;

static const exercise_data_t s_data = {
    .goal_text = "目标 5 公里  自由跑",
    .last_km   = "5.0",
    .last_pace = "28'12\"",
    .last_kcal = "318",
    .distance  = "0.00",
    .duration  = "06:48",
    .pace      = "5'28\"",
    .hr        = 142,
};

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_obj_t *s_container      = NULL;
static lv_obj_t *s_ready_view     = NULL;
static lv_obj_t *s_countdown_view = NULL;
static lv_obj_t *s_running_view   = NULL;

/* 倒计时 */
static lv_obj_t *s_cd_num_label   = NULL;
static lv_obj_t *s_cd_arc         = NULL;  /* 圆弧对象 */
static lv_timer_t *s_cd_timer     = NULL;
static int s_cd_count              = 0;

/* 运动状态标记：退出时不清除，重新进入时恢复 */
static bool s_is_running          = false;

/* 计时秒表 */
static lv_obj_t *s_dur_label      = NULL;
static lv_timer_t *s_dur_timer    = NULL;
static int s_elapsed_sec           = 0;

/* 心率动态值 */
static lv_obj_t *s_hr_label       = NULL;
static lv_timer_t *s_hr_timer     = NULL;
static int s_current_hr           = 72;

/* 心电图 */
static lv_obj_t *s_ecg_line       = NULL;
static lv_timer_t *s_ecg_timer    = NULL;
static int s_ecg_offset           = 0;
static lv_point_t s_ecg_pts[(ECG_W + ECG_STEP - 1) / ECG_STEP];

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void gesture_cb(lv_event_t *e);
static void start_btn_cb(lv_event_t *e);
static void pause_btn_cb(lv_event_t *e);
static void show_ready(void);
static void show_countdown(void);
static void show_running(void);
static void countdown_tick(lv_timer_t *timer);
static void ecg_tick(lv_timer_t *timer);
static void ecg_update_line(int offset);
static void dur_tick(lv_timer_t *timer);
static void hr_tick(lv_timer_t *timer);

/**********************
 *   ECG PULSE DATA
 **********************/

/*
 * 心电图波形采样点（归一化到 0~ECG_H）
 * 一段完整心跳周期，约 60 个采样点
 * 形状：基线→小P波→基线→QRS尖峰→基线→T波→基线
 */
static const uint8_t ecg_pattern[] = {
    /* 基线 */
    20,20,20,20,20,20,20,20,
    /* P 波（小幅上升） */
    20,18,16,15,16,18,20,
    /* 基线 */
    20,20,20,20,
    /* QRS 复合波（尖峰） */
    20,22,28,35,8,38,20,
    /* 基线 */
    20,20,20,20,20,
    /* T 波（中幅圆拱） */
    20,17,14,13,14,17,20,
    /* 基线填充 */
    20,20,20,20,20,20,20,20,20,20,
};

#define ECG_PATTERN_LEN  (sizeof(ecg_pattern))

/**********************
 *   STATIC FUNCTIONS
 **********************/

/* 手势回调：右滑退出 */
static void gesture_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir != LV_DIR_RIGHT) return;

    LV_LOG_USER("exercise: gesture RIGHT -> exit");

    /* 清理定时器，但运动中保留 s_dur_timer 和 s_hr_timer 以持续计时和变化 */
    if (s_cd_timer) { lv_timer_del(s_cd_timer); s_cd_timer = NULL; }
    if (s_ecg_timer) { lv_timer_del(s_ecg_timer); s_ecg_timer = NULL; }
    if (!s_is_running && s_hr_timer)  { lv_timer_del(s_hr_timer);  s_hr_timer  = NULL; }
    if (!s_is_running && s_dur_timer) { lv_timer_del(s_dur_timer); s_dur_timer = NULL; }

    if (s_container) {
        lv_obj_delete(s_container);
        s_container      = NULL;
        s_ready_view     = NULL;
        s_countdown_view = NULL;
        s_running_view   = NULL;
        s_ecg_line       = NULL;
        s_cd_num_label   = NULL;
        s_cd_arc         = NULL;
        s_dur_label      = NULL;
        s_hr_label       = NULL;
    }
    nav_return_from_app();
}

/* ──────── 页面切换 ──────── */

static void show_ready(void)
{
    if (s_ready_view)
        lv_obj_clear_flag(s_ready_view, LV_OBJ_FLAG_HIDDEN);
    if (s_countdown_view)
        lv_obj_add_flag(s_countdown_view, LV_OBJ_FLAG_HIDDEN);
    if (s_running_view)
        lv_obj_add_flag(s_running_view, LV_OBJ_FLAG_HIDDEN);

    /* 停止心电图 */
    if (s_ecg_timer) { lv_timer_del(s_ecg_timer); s_ecg_timer = NULL; }

    /* 停止计时秒表并归零 */
    if (s_dur_timer) { lv_timer_del(s_dur_timer); s_dur_timer = NULL; }
    s_elapsed_sec = 0;
    if (s_dur_label) lv_label_set_text(s_dur_label, "00:00");

    /* 停止动态心率 */
    if (s_hr_timer)  { lv_timer_del(s_hr_timer);  s_hr_timer  = NULL; }
    s_current_hr = 72;

    /* 强制重绘容器，清除残留阴影脏像素 */
    if (s_container) lv_obj_invalidate(s_container);
}

static void show_countdown(void)
{
    if (s_ready_view)
        lv_obj_add_flag(s_ready_view, LV_OBJ_FLAG_HIDDEN);
    if (s_countdown_view)
        lv_obj_clear_flag(s_countdown_view, LV_OBJ_FLAG_HIDDEN);
    if (s_running_view)
        lv_obj_add_flag(s_running_view, LV_OBJ_FLAG_HIDDEN);

    /* 动态心率：倒计时即开始变化 */
    srand(time(NULL));
    s_current_hr = 72;
    if (!s_hr_timer) {
        s_hr_timer = lv_timer_create(hr_tick, 2000, NULL);
    }
}

static void show_running(void)
{
    if (s_ready_view)
        lv_obj_add_flag(s_ready_view, LV_OBJ_FLAG_HIDDEN);
    if (s_countdown_view)
        lv_obj_add_flag(s_countdown_view, LV_OBJ_FLAG_HIDDEN);
    if (s_running_view)
        lv_obj_clear_flag(s_running_view, LV_OBJ_FLAG_HIDDEN);

    /* 启动心电动效 */
    s_ecg_offset = 0;
    if (!s_ecg_timer) {
        s_ecg_timer = lv_timer_create(ecg_tick, ECG_PERIOD_MS, NULL);
    }

    /* 计时秒表 */
    if (!s_is_running) {
        /* 新运动：归零并启动定时器 */
        s_elapsed_sec = 0;
        if (s_dur_label) lv_label_set_text(s_dur_label, "00:00");
        if (!s_dur_timer)
            s_dur_timer = lv_timer_create(dur_tick, 1000, NULL);
    } else {
        /* 恢复页面：定时器在后台仍在运行，只需刷新显示 */
        if (s_dur_label) {
            int mm = s_elapsed_sec / 60;
            int ss = s_elapsed_sec % 60;
            lv_label_set_text_fmt(s_dur_label, "%02d:%02d", mm, ss);
        }
        /* 心率定时器可能被 UI 清理掉了，恢复重建 */
        srand(time(NULL));
        if (!s_hr_timer) {
            s_hr_timer = lv_timer_create(hr_tick, 2000, NULL);
        }
    }

    s_is_running = true;
}

/* ──────── 倒计时逻辑 ──────── */

/*
 * 设计稿：圆弧在 3、2、1 时始终满弧，GO 时清零
 * 3 → 100%    2 → 100%    1 → 100%    GO → 0%
 */
static void countdown_tick(lv_timer_t *timer)
{
    (void)timer;
    s_cd_count--;

    if (s_cd_count > 0) {
        /* 更新数字，圆弧保持满弧 */
        lv_label_set_text_fmt(s_cd_num_label, "%d", s_cd_count);
        lv_arc_set_value(s_cd_arc, 100);
    } else if (s_cd_count == 0) {
        /* 显示 GO，圆弧保持满弧 */
        lv_label_set_text(s_cd_num_label, "GO");
        lv_arc_set_value(s_cd_arc, 100);
    } else {
        /* 倒计时结束，进入跑步 */
        if (s_cd_timer) { lv_timer_del(s_cd_timer); s_cd_timer = NULL; }
        show_running();
    }
}

/* 计时秒表：每秒更新（UI 不在时也继续计时） */
static void dur_tick(lv_timer_t *timer)
{
    (void)timer;
    s_elapsed_sec++;
    if (s_dur_label) {
        int mm = s_elapsed_sec / 60;
        int ss = s_elapsed_sec % 60;
        lv_label_set_text_fmt(s_dur_label, "%02d:%02d", mm, ss);
    }
}

/* 心率随机变化：每2秒，±5，范围72-142（与心率应用一致） */
static void hr_tick(lv_timer_t *timer)
{
    (void)timer;

    int delta = (rand() % 11) - 5;
    s_current_hr += delta;
    if (s_current_hr < 72)  s_current_hr = 72;
    if (s_current_hr > 142) s_current_hr = 142;

    if (s_hr_label) {
        lv_label_set_text_fmt(s_hr_label, "%d", s_current_hr);
    }
}

static void start_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    LV_LOG_USER("exercise: start -> countdown");

    /* 初始化倒计时状态：圆弧从满开始 */
    s_cd_count = 3;
    lv_label_set_text(s_cd_num_label, "3");
    lv_arc_set_value(s_cd_arc, 100);

    show_countdown();

    /* 启动倒计时定时器：850ms 间隔，3→2→1→GO→running */
    if (s_cd_timer) lv_timer_del(s_cd_timer);
    s_cd_timer = lv_timer_create(countdown_tick, 850, NULL);
}

/* ──────── 心电动效 ──────── */

/*
 * 更新 lv_line 的点数组，模拟心电图滚动
 * offset 控制波形水平滚动
 */
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

/* ──────── 辅助创建函数 ──────── */

static lv_obj_t *create_stat_mini(lv_obj_t *parent, const char *value,
                                   const char *unit)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_layout(col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *v = lv_label_create(col);
    lv_label_set_text(v, value);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(v, TEXT_PRIMARY, 0);

    lv_obj_t *u = lv_label_create(col);
    lv_label_set_text(u, unit);
    lv_obj_set_style_text_font(u, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(u, TEXT_MUTED, 0);

    return col;
}

static lv_obj_t *create_run_stat(lv_obj_t *parent, const char *label,
                                  const char *value, const char *unit,
                                  bool accent)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_set_size(col, 140, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_layout(col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(col);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(lbl, TEXT_MUTED, 0);

    lv_obj_t *big = lv_label_create(col);
    lv_label_set_text(big, value);
    lv_obj_set_style_text_font(big, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(big, accent ? BRAND_GREEN : TEXT_PRIMARY, 0);

    lv_obj_t *u = lv_label_create(col);
    lv_label_set_text(u, unit);
    lv_obj_set_style_text_font(u, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(u, TEXT_SECONDARY, 0);

    return col;
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void exercise_start(void)
{
    /* 清理已存在的 UI（防止重复创建） */
    if (s_container) {
        if (s_cd_timer) { lv_timer_del(s_cd_timer); s_cd_timer = NULL; }
        if (s_ecg_timer) { lv_timer_del(s_ecg_timer); s_ecg_timer = NULL; }
        if (s_hr_timer)  { lv_timer_del(s_hr_timer);  s_hr_timer  = NULL; }
        if (!s_is_running && s_dur_timer) { lv_timer_del(s_dur_timer); s_dur_timer = NULL; }
        lv_obj_delete(s_container);
        s_container      = NULL;
        s_ready_view     = NULL;
        s_countdown_view = NULL;
        s_running_view   = NULL;
        s_ecg_line       = NULL;
        s_cd_num_label   = NULL;
        s_cd_arc         = NULL;
        s_dur_label      = NULL;
        s_hr_label       = NULL;
    }

    lv_obj_t *scr = get_watch_scr();
    if (!scr) return;

    /* ── 根容器 ── */
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

    /* ── 顶部系统时间 ── */
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

    /* ── 标题（固定在所有页面之上） ── */
    lv_obj_t *title = lv_label_create(s_container);
    lv_label_set_text(title, "Sport");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(title, 1, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_add_flag(title, LV_OBJ_FLAG_FLOATING);

    /* ================================================================
     *  页面 1：准备页面 (Ready)
     * ================================================================ */
    s_ready_view = lv_obj_create(s_container);
    lv_obj_set_size(s_ready_view, SCR_W, SCR_H);
    lv_obj_align(s_ready_view, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(s_ready_view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ready_view, 0, 0);
    lv_obj_set_style_pad_all(s_ready_view, 0, 0);
    lv_obj_set_layout(s_ready_view, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_ready_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_ready_view, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_ready_view, LV_OBJ_FLAG_SCROLLABLE);

    /* "准备开始" */
    lv_obj_t *ready_tip = lv_label_create(s_ready_view);
    lv_label_set_text(ready_tip, "准备开始");
    lv_obj_set_style_text_font(ready_tip, font_manager_get_font(FONT_MEDIUM), 0);
    lv_obj_set_style_text_color(ready_tip, TEXT_PRIMARY, 0);
    lv_obj_set_style_pad_top(ready_tip, 80, 0);

    /* 目标标签 */
    lv_obj_t *goal = lv_obj_create(s_ready_view);
    lv_obj_set_size(goal, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(goal, 20, 0);
    lv_obj_set_style_bg_color(goal, BG_CARD, 0);
    lv_obj_set_style_bg_opa(goal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(goal, 1, 0);
    lv_obj_set_style_border_color(goal, STROKE_SOFT, 0);
    lv_obj_set_style_pad_hor(goal, 16, 0);
    lv_obj_set_style_pad_ver(goal, 8, 0);
    lv_obj_set_style_pad_column(goal, 8, 0);
    lv_obj_set_layout(goal, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(goal, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(goal, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(goal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_top(goal, 18, 0);

    lv_obj_t *dot = lv_obj_create(goal);
    lv_obj_set_size(dot, 7, 7);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, BRAND_GREEN, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *goal_lbl = lv_label_create(goal);
    lv_label_set_text(goal_lbl, s_data.goal_text);
    lv_obj_set_style_text_font(goal_lbl, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(goal_lbl, TEXT_SECONDARY, 0);

    /* 开始按钮 */
    lv_obj_t *start_btn = lv_btn_create(s_ready_view);
    lv_obj_set_size(start_btn, 140, 140);
    lv_obj_set_style_radius(start_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(start_btn, BRAND_GREEN, 0);
    lv_obj_set_style_bg_opa(start_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(start_btn, BRAND_GREEN, 0);
    lv_obj_set_style_shadow_width(start_btn, 30, 0);
    lv_obj_set_style_shadow_spread(start_btn, 4, 0);
    lv_obj_set_style_pad_all(start_btn, 0, 0);
    lv_obj_set_style_border_width(start_btn, 0, 0);
    lv_obj_add_event_cb(start_btn, start_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_col = lv_obj_create(start_btn);
    lv_obj_set_size(btn_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(btn_col, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(btn_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_col, 0, 0);
    lv_obj_set_style_pad_all(btn_col, 0, 0);
    lv_obj_set_layout(btn_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btn_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn_col, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(btn_col, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *play_ic = lv_label_create(btn_col);
    lv_label_set_text(play_ic, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(play_ic, lv_color_white(), 0);
    lv_obj_set_style_text_font(play_ic, &lv_font_montserrat_32, 0);

    /* 底部统计行 */
    lv_obj_t *stats_row = lv_obj_create(s_ready_view);
    lv_obj_set_size(stats_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(stats_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stats_row, 0, 0);
    lv_obj_set_style_pad_all(stats_row, 0, 0);
    lv_obj_set_style_pad_column(stats_row, 36, 0);
    lv_obj_set_layout(stats_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(stats_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(stats_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_top(stats_row, 20, 0);

    create_stat_mini(stats_row, s_data.last_km,   "上次公里");
    create_stat_mini(stats_row, s_data.last_pace,  "上次配速");
    create_stat_mini(stats_row, s_data.last_kcal,  "千卡");

    /* ================================================================
     *  页面 2：倒计时页面 (Countdown) — 初始隐藏
     * ================================================================ */
    s_countdown_view = lv_obj_create(s_container);
    lv_obj_set_size(s_countdown_view, SCR_W, SCR_H);
    lv_obj_align(s_countdown_view, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(s_countdown_view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_countdown_view, 0, 0);
    lv_obj_set_style_pad_all(s_countdown_view, 0, 0);
    lv_obj_set_scrollbar_mode(s_countdown_view, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_countdown_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_countdown_view, LV_OBJ_FLAG_HIDDEN);

    /* 圆弧 (用 lv_arc 控件) */
    s_cd_arc = lv_arc_create(s_countdown_view);
    lv_obj_set_size(s_cd_arc, 180, 180);
    lv_obj_align(s_cd_arc, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_rotation(s_cd_arc, 270);            /* 从顶部开始 */
    lv_arc_set_bg_angles(s_cd_arc, 0, 360);        /* 完整圆 */
    lv_arc_set_range(s_cd_arc, 0, 100);
    lv_arc_set_value(s_cd_arc, 0);
    lv_obj_remove_style(s_cd_arc, NULL, LV_PART_KNOB);  /* 隐藏旋钮 */
    lv_obj_clear_flag(s_cd_arc, LV_OBJ_FLAG_CLICKABLE);  /* 不可交互 */

    /* 弧线样式：灰色底弧 */
    lv_obj_set_style_arc_color(s_cd_arc, STROKE_SOFT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_cd_arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_cd_arc, STROKE_SOFT, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_cd_arc, 8, LV_PART_MAIN);

    /* 绿色前景弧 */
    lv_obj_set_style_arc_color(s_cd_arc, BRAND_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_cd_arc, LV_OPA_COVER, LV_PART_INDICATOR);

    /* 中间数字 */
    s_cd_num_label = lv_label_create(s_countdown_view);
    lv_label_set_text(s_cd_num_label, "3");
    lv_obj_set_style_text_font(s_cd_num_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_letter_space(s_cd_num_label, 0, 0);
    lv_obj_set_style_text_color(s_cd_num_label, lv_color_white(), 0);
    lv_obj_align(s_cd_num_label, LV_ALIGN_CENTER, 0, 0);

    /* "Starting" 提示 — 在圆弧下方水平居中 */
    lv_obj_t *cd_hint = lv_label_create(s_countdown_view);
    lv_label_set_text(cd_hint, "Starting...");
    lv_obj_set_style_text_font(cd_hint, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(cd_hint, TEXT_SECONDARY, 0);
    lv_obj_align(cd_hint, LV_ALIGN_CENTER, 0, 150);

    /* ================================================================
     *  页面 3：运行页面 (Running) — 初始隐藏
     * ================================================================ */
    s_running_view = lv_obj_create(s_container);
    lv_obj_set_size(s_running_view, SCR_W, SCR_H);
    lv_obj_align(s_running_view, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(s_running_view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_running_view, 0, 0);
    lv_obj_set_style_pad_all(s_running_view, 0, 0);
    lv_obj_set_layout(s_running_view, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_running_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_running_view, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_running_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_running_view, LV_OBJ_FLAG_HIDDEN);

    /* 2×2 数据网格 */
    lv_obj_t *grid = lv_obj_create(s_running_view);
    lv_obj_set_size(grid, 310, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, 14, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_top(grid, 70, 0);

    create_run_stat(grid, "距离", s_data.distance, "公里",  true);

    /* 用时：单独创建以便动态更新 */
    {
        lv_obj_t *col = lv_obj_create(grid);
        lv_obj_set_size(col, 140, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_set_layout(col, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(col);
        lv_label_set_text(lbl, "用时");
        lv_obj_set_style_text_font(lbl, font_manager_get_font(FONT_SMALL), 0);
        lv_obj_set_style_text_color(lbl, TEXT_MUTED, 0);

        s_dur_label = lv_label_create(col);
        lv_label_set_text(s_dur_label, "00:00");
        lv_obj_set_style_text_font(s_dur_label, &lv_font_montserrat_36, 0);
        lv_obj_set_style_text_color(s_dur_label, TEXT_PRIMARY, 0);

        lv_obj_t *u = lv_label_create(col);
        lv_label_set_text(u, "分:秒");
        lv_obj_set_style_text_font(u, font_manager_get_font(FONT_SMALL), 0);
        lv_obj_set_style_text_color(u, TEXT_SECONDARY, 0);
    }

    create_run_stat(grid, "配速", s_data.pace,      "分/公里", false);

    /* 心率：单独创建以便动态更新 */
    {
        lv_obj_t *col = lv_obj_create(grid);
        lv_obj_set_size(col, 140, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_set_layout(col, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(col);
        lv_label_set_text(lbl, "心率");
        lv_obj_set_style_text_font(lbl, font_manager_get_font(FONT_SMALL), 0);
        lv_obj_set_style_text_color(lbl, TEXT_MUTED, 0);

        s_hr_label = lv_label_create(col);
        lv_label_set_text_fmt(s_hr_label, "%d", s_current_hr);
        lv_obj_set_style_text_font(s_hr_label, &lv_font_montserrat_36, 0);
        lv_obj_set_style_text_color(s_hr_label, TEXT_PRIMARY, 0);

        lv_obj_t *u = lv_label_create(col);
        lv_label_set_text(u, "BPM");
        lv_obj_set_style_text_font(u, font_manager_get_font(FONT_SMALL), 0);
        lv_obj_set_style_text_color(u, TEXT_SECONDARY, 0);
    }

    /* 配速进度条 */
    lv_obj_t *pace_bar = lv_obj_create(s_running_view);
    lv_obj_set_size(pace_bar, 310, 8);
    lv_obj_set_style_radius(pace_bar, 4, 0);
    lv_obj_set_style_bg_color(pace_bar, BG_CARD, 0);
    lv_obj_set_style_bg_opa(pace_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pace_bar, 0, 0);
    lv_obj_set_style_pad_all(pace_bar, 0, 0);
    lv_obj_clear_flag(pace_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_clip_corner(pace_bar, true, 0);
    lv_obj_set_style_pad_top(pace_bar, 10, 0);

    lv_obj_t *pace_fill = lv_obj_create(pace_bar);
    lv_obj_set_size(pace_fill, 310 * 62 / 100, 8);
    lv_obj_set_style_radius(pace_fill, 4, 0);
    lv_obj_set_style_bg_color(pace_fill, BRAND_GREEN, 0);
    lv_obj_set_style_bg_opa(pace_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pace_fill, 0, 0);
    lv_obj_clear_flag(pace_fill, LV_OBJ_FLAG_SCROLLABLE);

    /* ── 心电图 lv_line ── */
    s_ecg_line = lv_line_create(s_running_view);
    lv_obj_set_size(s_ecg_line, ECG_W, ECG_H);
    lv_obj_set_style_line_color(s_ecg_line, BRAND_RED, 0);
    lv_obj_set_style_line_width(s_ecg_line, 2, 0);
    lv_obj_set_style_line_opa(s_ecg_line, LV_OPA_COVER, 0);
    lv_obj_set_style_line_rounded(s_ecg_line, 1, 0);
    lv_obj_set_style_pad_top(s_ecg_line, 6, 0);

    /* 初始绘制一帧 */
    ecg_update_line(0);

    /* 暂停按钮 */
    lv_obj_t *pause_btn = lv_btn_create(s_running_view);
    lv_obj_set_size(pause_btn, 84, 84);
    lv_obj_set_style_radius(pause_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pause_btn, BG_CARD, 0);
    lv_obj_set_style_bg_opa(pause_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pause_btn, 1, 0);
    lv_obj_set_style_border_color(pause_btn, STROKE_SOFT, 0);
    lv_obj_set_style_pad_all(pause_btn, 0, 0);
    lv_obj_add_event_cb(pause_btn, pause_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *pause_ic = lv_label_create(pause_btn);
    lv_label_set_text(pause_ic, LV_SYMBOL_PAUSE);
    lv_obj_set_style_text_color(pause_ic, lv_color_white(), 0);
    lv_obj_set_style_text_font(pause_ic, &lv_font_montserrat_24, 0);
    lv_obj_center(pause_ic);

    if (s_is_running) {
        show_running();
    }
}

/* 暂停按钮回调：停止运动，回到准备页 */
static void pause_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    s_is_running = false;
    show_ready();
}

/**********************
 *   MCP 接口（供语音调用）
 **********************/

/* 跳转到运动应用，显示倒计时后自动进入跑步页 */
void exercise_start_running(void)
{
    exercise_start();
    /* 模拟点击开始按钮：启动倒计时 */
    s_cd_count = 3;
    lv_label_set_text(s_cd_num_label, "3");
    lv_arc_set_value(s_cd_arc, 100);
    show_countdown();
    if (s_cd_timer) lv_timer_del(s_cd_timer);
    s_cd_timer = lv_timer_create(countdown_tick, 850, NULL);
}

/* 停止当前运动，回到准备页 */
void exercise_stop(void)
{
    s_is_running = false;
    show_ready();
}
