/*********************
 *      INCLUDES
 *********************/
#include <lvgl/lvgl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../../sleep/include/sleep.h"
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

/* 睡眠阶段色 */
#define COLOR_DEEP      lv_color_hex(0x6C5CE7)
#define COLOR_LIGHT     lv_color_hex(0xA29BFE)
#define COLOR_REM       lv_color_hex(0x00CEC9)
#define COLOR_AWAKE     lv_color_hex(0xFFD32A)
#define BRAND_PRIMARY   lv_color_hex(0x6C5CE7)

/**********************
 *      TYPEDEFS
 **********************/
/* 静态睡眠数据（演示用） */
typedef struct {
    const char *duration;     /* 总时长 */
    const char *sub_text;     /* 副标题 */
    int deep_pct;             /* 深睡占比 % */
    int light_pct;
    int rem_pct;
    int awake_pct;
    const char *deep_time;
    const char *light_time;
    const char *rem_time;
    const char *awake_time;
    const char *sleep_time;   /* 入睡时间 */
    const char *wake_time;    /* 醒来时间 */
    int score;                /* 睡眠评分 */
} sleep_data_t;

static const sleep_data_t s_data = {
    .duration   = "7:32",
    .sub_text   = "昨晚睡眠时长",
    .deep_pct   = 23,
    .light_pct  = 42,
    .rem_pct    = 27,
    .awake_pct  = 8,
    .deep_time  = "1:44",
    .light_time = "3:10",
    .rem_time   = "2:02",
    .awake_time = "0:36",
    .sleep_time = "23:48",
    .wake_time  = "07:20",
    .score      = 86,
};

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_obj_t *s_container = NULL;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void gesture_cb(lv_event_t *e);

/**********************
 *   STATIC FUNCTIONS
 **********************/

/* 手势回调：右滑退出 */
static void gesture_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir != LV_DIR_RIGHT) return;

    LV_LOG_USER("sleep: gesture RIGHT -> exit");
    if (s_container) {
        lv_obj_delete(s_container);
        s_container = NULL;
    }
    nav_return_from_app();
}

/* 创建一行图例行（色块 + 名称 + 时间） */
static lv_obj_t *create_legend_row(lv_obj_t *parent, lv_color_t color,
                                    const char *name, const char *time)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* 色块 */
    lv_obj_t *sw = lv_obj_create(row);
    lv_obj_set_size(sw, 10, 10);
    lv_obj_set_style_radius(sw, 3, 0);
    lv_obj_set_style_bg_color(sw, color, 0);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sw, 0, 0);
    lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE);

    /* 名称 */
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, name);
    lv_obj_set_style_text_font(lbl, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(lbl, TEXT_SECONDARY, 0);

    /* 时间 */
    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, time);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(val, TEXT_PRIMARY, 0);

    return row;
}

/* 创建时间线行 */
static lv_obj_t *create_timeline_row(lv_obj_t *parent,
                                      const char *label, const char *time)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 310, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_bg_color(row, BG_CARD, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, STROKE_SOFT, 0);
    lv_obj_set_style_pad_all(row, 12, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* 左侧：文字 */
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(lbl, TEXT_SECONDARY, 0);

    /* 右侧：时间 */
    lv_obj_t *t = lv_label_create(row);
    lv_label_set_text(t, time);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(t, TEXT_PRIMARY, 0);

    return row;
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void sleep_start(void)
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
    lv_label_set_text(title, "Sleep");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(title, 1, 0);
    lv_obj_set_style_pad_top(title, 30, 0);

    /* ============ 时长数字 ============ */
    lv_obj_t *dur_num = lv_label_create(content);
    lv_label_set_text(dur_num, s_data.duration);
    lv_obj_set_style_text_font(dur_num, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(dur_num, TEXT_PRIMARY, 0);
    lv_obj_set_style_pad_top(dur_num, 10, 0);

    /* 副标题 */
    lv_obj_t *dur_sub = lv_label_create(content);
    lv_label_set_text(dur_sub, s_data.sub_text);
    lv_obj_set_style_text_font(dur_sub, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(dur_sub, TEXT_SECONDARY, 0);
    lv_obj_set_style_pad_top(dur_sub, 4, 0);

    /* ============ 阶段条 ============ */
    lv_obj_t *stages = lv_obj_create(content);
    lv_obj_set_size(stages, 310, 18);
    lv_obj_set_style_bg_opa(stages, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stages, 0, 0);
    lv_obj_set_style_radius(stages, 0, 0);
    lv_obj_set_style_pad_all(stages, 0, 0);
    lv_obj_clear_flag(stages, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_top(stages, 78, 0);

    /* 各阶段色块（绝对定位，避免 flex 布局覆盖尺寸） */
    struct { int pct; lv_color_t color; } stage_parts[] = {
        { s_data.deep_pct,  COLOR_DEEP  },
        { s_data.light_pct, COLOR_LIGHT },
        { s_data.rem_pct,   COLOR_REM   },
        { s_data.awake_pct, COLOR_AWAKE },
    };
    int x_pos = 0;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *bar = lv_obj_create(stages);
        int w = 310 * stage_parts[i].pct / 100;
        if (w < 1) w = 1;
        lv_obj_set_size(bar, w, 18);
        lv_obj_set_pos(bar, x_pos, 0);
        lv_obj_set_style_bg_color(bar, stage_parts[i].color, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        x_pos += w;
    }

    /* ============ 图例（2×2 网格） ============ */
    lv_obj_t *legend = lv_obj_create(content);
    lv_obj_set_size(legend, 310, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(legend, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(legend, 0, 0);
    lv_obj_set_style_pad_all(legend, 0, 0);
    lv_obj_set_layout(legend, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(legend, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(legend, 8, 0);
    lv_obj_clear_flag(legend, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_top(legend, 12, 0);

    create_legend_row(legend, COLOR_DEEP,  "深睡",     s_data.deep_time);
    create_legend_row(legend, COLOR_LIGHT, "浅睡",     s_data.light_time);
    create_legend_row(legend, COLOR_REM,   "快速眼动", s_data.rem_time);
    create_legend_row(legend, COLOR_AWAKE, "清醒",     s_data.awake_time);

    /* ============ 时间线 ============ */
    lv_obj_t *timeline = lv_obj_create(content);
    lv_obj_set_size(timeline, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(timeline, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(timeline, 0, 0);
    lv_obj_set_style_pad_all(timeline, 0, 0);
    lv_obj_set_style_pad_row(timeline, 10, 0);
    lv_obj_set_layout(timeline, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(timeline, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(timeline, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(timeline, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_top(timeline, 16, 0);

    create_timeline_row(timeline, "入睡", s_data.sleep_time);
    create_timeline_row(timeline, "醒来", s_data.wake_time);

    /* ============ 睡眠评分 ============ */
    lv_obj_t *score_row = lv_obj_create(content);
    lv_obj_set_size(score_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(score_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(score_row, 0, 0);
    lv_obj_set_style_pad_all(score_row, 0, 0);
    lv_obj_set_style_pad_column(score_row, 8, 0);
    lv_obj_set_layout(score_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(score_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(score_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(score_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_top(score_row, 16, 0);

    lv_obj_t *score_lbl = lv_label_create(score_row);
    lv_label_set_text(score_lbl, "睡眠评分");
    lv_obj_set_style_text_font(score_lbl, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(score_lbl, TEXT_SECONDARY, 0);

    lv_obj_t *score_val = lv_label_create(score_row);
    lv_label_set_text_fmt(score_val, "%d", s_data.score);
    lv_obj_set_style_text_font(score_val, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(score_val, BRAND_PRIMARY, 0);
}
