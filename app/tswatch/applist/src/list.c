#include "lvgl.h"
#include "../include/list.h"
#include "../../clock/include/alarm_main.h"     /* 闹钟 clock_main_start() */
#include "../../stopwatch/include/stopwatch.h"  /* 秒表 stopwatch_start() */
#include "../../xiaozhi_voice/xiaozhi_voice.h"  /* AI语音 xiaozhi_voice_start() */
#include "../../timer/include/timer_main.h"     /* 倒计时 timer_start() */
#include "../../sleep/include/sleep.h"          /* 睡眠 sleep_start() */
#include "../../heart/include/heart.h"          /* 心率 heart_start() */
#include "../../exercise/include/exercise.h"    /* 运动 exercise_start() */
#include "../../settings/include/setting.h"    /* 设置 settings_app_start() */
#include "../../main_page/include/watch_start.h" /* nav_enter_app() */
#include "../../utils/include/font_manager.h"  /* 中文显示字体 */
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/* ========== 应用列表定义 ========== */
typedef struct {
    const char *name;
    const char *icon;       /* 外置图标资源路径（/emmc/xxx.png，运行时从 emmc 加载） */
    uint32_t    icon_bg;    /* 图标容器背景色（hex，运行时转 lv_color_t） */
    uint32_t    icon_color; /* 图标容器/按下态边框前景色（hex） */
    void      (*launch_cb)(void);
} app_item_t;

static void launch_alarm(void)      { nav_enter_app(); clock_main_start(); }
static void launch_stopwatch(void)  { nav_enter_app(); stopwatch_start(); }
static void launch_timer(void)      { printf("[DBG] launch_timer ENTER\n"); nav_enter_app(); printf("[DBG] launch_timer nav_enter_app done\n"); timer_start(); printf("[DBG] launch_timer timer_start done\n"); }
static void launch_ai(void)         { nav_enter_app(); xiaozhi_voice_start(); }
static void launch_sleep(void)      { nav_enter_app(); sleep_start(); }
static void launch_heart(void)      { nav_enter_app(); heart_start(); }
static void launch_exercise(void)   { nav_enter_app(); exercise_start(); }
static void launch_settings(void)   { nav_enter_app(); settings_app_start(); }

/* "语音助手"置于列表第一位 */
#define APP_COUNT   8
static app_item_t apps[APP_COUNT] = {
    /*  name      icon                            icon_bg     icon_color   launch           */
    { "语音助手", "/emmc/icon_ai.png",       0x6C5CE7, 0x6C5CE7,   launch_ai        },
    { "闹钟",   "/emmc/icon_alarm.png",          0xFFA502, 0xFFA502,   launch_alarm     },
    { "秒表",   "/emmc/icon_stopwatch.png",      0x00CEC9, 0x00CEC9,   launch_stopwatch },
    { "倒计时", "/emmc/icon_timer.png",          0x4A90E2, 0x4A90E2,   launch_timer     },
    { "心率",   "/emmc/icon_heart.png",      0xFF6B6B, 0xFF6B6B,   launch_heart      },
    { "睡眠",   "/emmc/icon_sleep.png",          0x8B7CF6, 0x8B7CF6,   launch_sleep        },
    { "运动",   "/emmc/icon_exercise.png",          0x00B894, 0x00B894,   launch_exercise         },
    { "设置",   "/emmc/icon_settings.png",      0x636E72, 0x636E72,   launch_settings  },
};

/* ========== 设计规范 (来自 watch-design) ========== */
#define SCREEN_W        455
#define SCREEN_H        455
#define BG_BASE         lv_color_hex(0x06070D)     /* --bg-base: 近黑深蓝 */
#define BG_CARD         lv_color_hex(0x131520)     /* --bg-card 实色近似 */
#define TEXT_PRIMARY     lv_color_hex(0xF8F9FF)
#define TEXT_SECONDARY   lv_color_hex(0xA6ACCD)
#define STROKE_SOFT      lv_color_hex(0x1F2133)    /* --stroke-soft 实色近似 */

/* 磁贴尺寸（3×3 布局，7 个应用，整体居中） */
#define TILE_W          100
#define TILE_H          100
#define TILE_GAP        12
#define TILE_RADIUS     22      /* --r-tile: 22px */
#define ICON_SIZE       46
#define ICON_RADIUS     14      /* --ic-wrap: 14px */
#define GRID_COLS       3       /* 7 个应用 -> 3×3 */
#define GRID_ROWS       3

/* 网格整体居中偏移 */
#define GRID_W          (GRID_COLS * TILE_W + (GRID_COLS - 1) * TILE_GAP)
#define GRID_H          (GRID_ROWS * TILE_H + (GRID_ROWS - 1) * TILE_GAP)
#define GRID_X0         ((SCREEN_W - GRID_W) / 2)
#define GRID_Y0         ((SCREEN_H - GRID_H) / 2 + 10)  /* 略偏下，给标题留空 */

/* ========== UI 对象 ========== */
static lv_obj_t *ui_bg;
static lv_obj_t *ui_title;
static lv_obj_t *ui_tiles[APP_COUNT];
static lv_obj_t *ui_icon_wraps[APP_COUNT];
static lv_obj_t *ui_icon_imgs[APP_COUNT];   /* 外置 PNG 图标（/emmc/xxx.png） */
static lv_obj_t *ui_names[APP_COUNT];
static lv_obj_t *ui_dots[2];
static int8_t    selected_idx = 0;
static lv_group_t *ui_group;

static void set_tile_default(int8_t i);
static void set_tile_pressed(int8_t i);
static void tile_event_cb(lv_event_t *e);

/* ========== 圆形背景 ========== */
static void create_background(lv_obj_t *parent)
{
    ui_bg = lv_obj_create(parent);
    lv_obj_set_size(ui_bg, SCREEN_W, SCREEN_H);
    lv_obj_align(ui_bg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(ui_bg, 0, 0);
    lv_obj_set_style_bg_color(ui_bg, BG_BASE, 0);
    lv_obj_set_style_bg_opa(ui_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_bg, 0, 0);
    lv_obj_set_style_pad_all(ui_bg, 0, 0);
    lv_obj_clear_flag(ui_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ui_bg, LV_OBJ_FLAG_CLICKABLE);

}

/* ========== 顶部标题 "应用" ========== */
static void create_title(void)
{
    ui_title = lv_label_create(ui_bg);
    lv_label_set_text(ui_title, "APP");
    lv_obj_set_style_text_font(ui_title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(ui_title, TEXT_SECONDARY, 0);
    lv_obj_set_style_text_letter_space(ui_title, 1, 0);
    lv_obj_align(ui_title, LV_ALIGN_TOP_MID, 0, 30);
}

/* ========== 单个应用磁贴 ==========
 * 设计稿：圆角矩形卡片，内含彩色圆角图标容器 + 下方文字。
 * 7 个应用排成 3×3 网格，整体居中。图标从 /emmc/ 外置 PNG 加载。 */
static void create_app_tile(int8_t idx)
{
    int col = idx % GRID_COLS;
    int row = idx / GRID_COLS;
    int16_t x = GRID_X0 + col * (TILE_W + TILE_GAP) + TILE_W / 2;
    int16_t y = GRID_Y0 + row * (TILE_H + TILE_GAP) + TILE_H / 2;

    /* --- 磁贴容器 --- */
    ui_tiles[idx] = lv_obj_create(ui_bg);
    lv_obj_set_size(ui_tiles[idx], TILE_W, TILE_H);
    lv_obj_align(ui_tiles[idx], LV_ALIGN_CENTER, x - SCREEN_W / 2, y - SCREEN_H / 2);
    lv_obj_set_style_radius(ui_tiles[idx], TILE_RADIUS, 0);
    lv_obj_set_style_bg_color(ui_tiles[idx], BG_CARD, 0);
    lv_obj_set_style_bg_opa(ui_tiles[idx], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_tiles[idx], 1, 0);
    lv_obj_set_style_border_color(ui_tiles[idx], STROKE_SOFT, 0);
    lv_obj_set_style_pad_all(ui_tiles[idx], 0, 0);
    lv_obj_clear_flag(ui_tiles[idx], LV_OBJ_FLAG_SCROLLABLE);

    /* --- 图标容器（彩色圆角方块）--- */
    ui_icon_wraps[idx] = lv_obj_create(ui_tiles[idx]);
    lv_obj_set_size(ui_icon_wraps[idx], ICON_SIZE, ICON_SIZE);
    lv_obj_align(ui_icon_wraps[idx], LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_radius(ui_icon_wraps[idx], ICON_RADIUS, 0);

    /* 半透明背景色：取 icon_bg 的 RGB，alpha 固定 ~18% (LV_OPA_18=46) */
    lv_obj_set_style_bg_color(ui_icon_wraps[idx], lv_color_hex(apps[idx].icon_bg), 0);
    lv_obj_set_style_bg_opa(ui_icon_wraps[idx], 46, 0);  /* ~18% opacity */
    lv_obj_set_style_border_width(ui_icon_wraps[idx], 0, 0);
    lv_obj_set_style_pad_all(ui_icon_wraps[idx], 0, 0);
    lv_obj_clear_flag(ui_icon_wraps[idx], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ui_icon_wraps[idx], LV_OBJ_FLAG_CLICKABLE);

    /* --- 图标（外置 PNG，从 /emmc/ 加载）--- */
    ui_icon_imgs[idx] = lv_img_create(ui_icon_wraps[idx]);
    lv_img_set_src(ui_icon_imgs[idx], apps[idx].icon);
    lv_obj_center(ui_icon_imgs[idx]);
    lv_obj_clear_flag(ui_icon_imgs[idx], LV_OBJ_FLAG_CLICKABLE);

    /* --- 应用名称 --- */
    ui_names[idx] = lv_label_create(ui_tiles[idx]);
    lv_label_set_text(ui_names[idx], apps[idx].name);
    lv_obj_set_style_text_font(ui_names[idx], font_manager_get_font(FONT_LARGE), 0);
    lv_obj_set_style_text_color(ui_names[idx], TEXT_SECONDARY, 0);
    lv_obj_align(ui_names[idx], LV_ALIGN_BOTTOM_MID, 0, -8);

    lv_obj_add_event_cb(ui_tiles[idx], tile_event_cb, LV_EVENT_ALL, (void*)(intptr_t)idx);
}

/* ========== 磁贴样式：默认态 / 按下态 ========== */
static void set_tile_default(int8_t i)
{
    lv_obj_set_style_bg_opa(ui_tiles[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ui_tiles[i], STROKE_SOFT, 0);
    lv_obj_set_style_border_width(ui_tiles[i], 1, 0);
    lv_obj_set_style_text_color(ui_names[i], TEXT_SECONDARY, 0);
}

static void set_tile_pressed(int8_t i)
{
    lv_obj_set_style_bg_opa(ui_tiles[i], LV_OPA_30, 0);
    lv_obj_set_style_bg_color(ui_tiles[i], lv_color_hex(apps[i].icon_color), 0);
    lv_obj_set_style_border_color(ui_tiles[i], lv_color_hex(apps[i].icon_color), 0);
    lv_obj_set_style_border_width(ui_tiles[i], 2, 0);
    lv_obj_set_style_text_color(ui_names[i], TEXT_PRIMARY, 0);
}

static void tile_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int8_t idx = (int8_t)(intptr_t)lv_event_get_user_data(e);
    if (code == LV_EVENT_PRESSED) {
        set_tile_pressed(idx);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        set_tile_default(idx);
    } else if (code == LV_EVENT_CLICKED) {
        if (apps[idx].launch_cb) apps[idx].launch_cb();
    }
}

/* ========== 底部分页圆点 ========== */
static void create_page_dots(void)
{
    int16_t dots_y = SCREEN_H / 2 + GRID_H / 2 + 30;
    int16_t dot_w = 6, dot_active_w = 16;
    int16_t total_w = dot_active_w + 6 + dot_w;
    int16_t x0 = (SCREEN_W - total_w) / 2;

    /* 第一个点（active） */
    ui_dots[0] = lv_obj_create(ui_bg);
    lv_obj_set_size(ui_dots[0], dot_active_w, dot_w);
    lv_obj_align(ui_dots[0], LV_ALIGN_CENTER, x0 + dot_active_w / 2 - SCREEN_W / 2,
                 dots_y - SCREEN_H / 2);
    lv_obj_set_style_radius(ui_dots[0], 3, 0);
    lv_obj_set_style_bg_color(ui_dots[0], lv_color_hex(0x6C5CE7), 0);  /* brand-primary */
    lv_obj_set_style_bg_opa(ui_dots[0], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_dots[0], 0, 0);
    lv_obj_clear_flag(ui_dots[0], LV_OBJ_FLAG_CLICKABLE);

    /* 第二个点（inactive） */
    ui_dots[1] = lv_obj_create(ui_bg);
    lv_obj_set_size(ui_dots[1], dot_w, dot_w);
    lv_obj_align(ui_dots[1], LV_ALIGN_CENTER,
                 x0 + dot_active_w + 6 + dot_w / 2 - SCREEN_W / 2,
                 dots_y - SCREEN_H / 2);
    lv_obj_set_style_radius(ui_dots[1], 3, 0);
    lv_obj_set_style_bg_color(ui_dots[1], STROKE_SOFT, 0);
    lv_obj_set_style_bg_opa(ui_dots[1], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_dots[1], 0, 0);
    lv_obj_clear_flag(ui_dots[1], LV_OBJ_FLAG_CLICKABLE);
}

/* ========== 公开 API ========== */
void app_launcher_create(lv_obj_t *parent)
{
    lv_obj_set_size(parent, SCREEN_W, SCREEN_H);
    lv_obj_align(parent, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(parent, 0, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    create_background(parent);
    create_title();
    for (int8_t i = 0; i < APP_COUNT; i++) create_app_tile(i);
    for (int8_t i = 0; i < APP_COUNT; i++) set_tile_default(i);
    create_page_dots();

    selected_idx = 0;
    ui_group = lv_group_create();
    for (int8_t i = 0; i < APP_COUNT; i++) lv_group_add_obj(ui_group, ui_tiles[i]);
}

void app_launcher_next(void)  { selected_idx = (selected_idx+1) % APP_COUNT; for(int8_t i=0;i<APP_COUNT;i++) set_tile_default(i); set_tile_pressed(selected_idx); }
void app_launcher_prev(void)  { selected_idx = (selected_idx-1+APP_COUNT) % APP_COUNT; for(int8_t i=0;i<APP_COUNT;i++) set_tile_default(i); set_tile_pressed(selected_idx); }
void app_launcher_enter(void) { if (apps[selected_idx].launch_cb) apps[selected_idx].launch_cb(); }
int8_t app_launcher_get_selected(void) { return selected_idx; }
lv_group_t *app_launcher_get_group(void) { return ui_group; }

void app_launcher_delete(void)
{
    if (ui_group) { lv_group_del(ui_group); ui_group = NULL; }
    if (ui_bg) { lv_obj_del(ui_bg); ui_bg = NULL; }
    for (int8_t i = 0; i < APP_COUNT; i++) {
        ui_tiles[i] = NULL; ui_icon_wraps[i] = NULL;
        ui_icon_imgs[i] = NULL; ui_names[i] = NULL;
    }
    ui_dots[0] = NULL; ui_dots[1] = NULL;
    ui_title = NULL;
    selected_idx = 0;
}

/* ========== 旧 API 适配：供 watch_start.c 页面栈调用 ========== */
lv_obj_t *applist_create_list(lv_obj_t *scr)
{
    app_launcher_create(scr);
    return scr;
}

void applist_hide_page(void)
{
    app_launcher_delete();
    LV_LOG_USER("applist_hide_page: launcher cleaned");
}
