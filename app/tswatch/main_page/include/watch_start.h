#ifndef WATCH_START_H
#define WATCH_START_H

#include "../../utils/include/circle_screen.h"

/* 圆形表盘尺寸：与 watchScr (LV_CIRCLE_WATCH=455) 对齐，避免溢出导致可滑动 */
#define WATCH_SCREEN_WIDTH   LV_CIRCLE_WATCH
#define WATCH_SCREEN_HEIGHT  LV_CIRCLE_WATCH
#define WATCH_RADIUS         (LV_CIRCLE_WATCH / 2)

/* ============ 配色 ============ */
#define COLOR_BG_DARK        lv_color_hex(0x101418)
#define COLOR_BG_MID         lv_color_hex(0x1b2330)
#define COLOR_TEXT_WHITE     lv_color_hex(0xffffff)
#define COLOR_TEXT_GRAY      lv_color_hex(0x9aa4b2)
#define COLOR_TEXT_DIM       lv_color_hex(0x3a4452)

#define COLOR_ACCENT_CYAN    lv_color_hex(0x22d3ee)
#define COLOR_ACCENT_BLUE    lv_color_hex(0x3b82f6)
#define COLOR_ACCENT_PURPLE  lv_color_hex(0x8b5cf6)
#define COLOR_ACCENT_PINK    lv_color_hex(0xec4899)
#define COLOR_ACCENT_ORANGE  lv_color_hex(0xf59e0b)
#define COLOR_ACCENT_GREEN   lv_color_hex(0x22c55e)
#define COLOR_ACCENT_RED     lv_color_hex(0xef4444)

/* ============ 字体别名 ============
 * watch_start.c 原本引用了 montserrat_12/14/16/18/22/36。
 * 真机只启用 20/22/24/28/32/36/38/40/48，模拟器只启用 12/16/20/32，
 * 12/14/16/18 在两边都未必启用，会链接失败。
 * 这里统一映射到两边均已启用的安全字体：20（小）、32（大）。
 * 若要更精细字号，可在 defconfig 里开启更多 CONFIG_LV_FONT_MONTSERRAT_xx 后改这里。
 */
#define WATCH_FONT_BIG        (&lv_font_montserrat_32)   /* 原 36：时间、数值大字 */
#define WATCH_FONT_MID        (&lv_font_montserrat_20)   /* 原 22：图标、数值中字 */
#define WATCH_FONT_SMALL      (&lv_font_montserrat_20)   /* 原 14/18：标签、单位 */
#define WATCH_FONT_TINY       (&lv_font_montserrat_20)   /* 原 12：最小单位 */

void watch_start(void);

/* 二级应用页退出后回调：按页面栈重建上层（列表或表盘）。
 * 各二级页(stopwatch/calendar/settings/timer/music)退出函数末尾调用。 */
void nav_return_from_app(void);

/* 进入二级应用页前回调：删除列表释放 GUI 堆，记栈上层=列表。
 * list.c 的 app_click_event_cb 在调用 xxx_start() 前调用。 */
void nav_enter_app(void);

/* 页面类型常量（用于 nav_enter_app_from 的 prev 参数） */
enum {
    PAGE_HOME = 0,     /* 表盘 */
    PAGE_APPLIST,      /* 应用列表 */
    PAGE_APP,          /* 二级应用页 */
    PAGE_VOICE,        /* xiaozhi_voice（从语音返回时用） */
};

/* 从指定页面进入应用（prev = PAGE_HOME / PAGE_APPLIST / PAGE_VOICE）。
 * 用于电源键→xiaozhi_voice（prev=PAGE_HOME）或 语音→闹钟（prev=PAGE_VOICE）等场景。 */
void nav_enter_app_from(int prev);

/* 获取当前入口页面（用于快照 g_prev_page，MCP 工具会修改它） */
int nav_get_prev_page(void);

#endif /* WATCH_START_H */