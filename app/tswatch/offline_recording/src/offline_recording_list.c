/****************************************************************************
 * apps/examples/tswatch/offline_recording/offline_recording_list.c
 *
 * 端侧（手表）离线录音列表（覆盖层，LVGL，设计文档 §11）：
 *   - 以覆盖层叠加在调用方页面（录音 App）之上，不切换 App、不调用 nav；
 *   - 扫描 /emmc/rec_*.bin（offline_store_scan，按开始时间倒序），逐文件生成行；
 *   - 行内容：开始时间(YYYY-MM-DD HH:MM) + 估算时长(mm:ss) + 状态文字；
 *   - 刷新：lv_timer 每 1s 重绘（兜底）+ lv_async_call 事件驱动（同步状态变化）；
 *   - 「已同步」：手机 ACK(success) 经 offline_recordings_on_synced → 行置绿 +
 *     轻量 toast → 1.5s 后移除（本地文件已在 ACK 时删除）；
 *   - 手动清理：PENDING 行长按 → 确认框 → 删本地文件；
 *   - 右滑仅关闭覆盖层，回到下层（录音 App）。
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <lvgl/lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "../../utils/include/circle_screen.h"    /* LV_CIRCLE_WATCH */
#include "../../utils/include/font_manager.h"     /* 中文显示字体 */
#include "../../main_page/include/watch_start.h"      /* get_watch_scr / nav_return_from_app */
#include "../../spp_audio/include/offline_recording_store.h"

/****************************************************************************
 * 颜色（与 recorder / applist 同套深色主题）
 ****************************************************************************/
#define BG_BASE         lv_color_hex(0x06070D)
#define CARD_BG         lv_color_hex(0x151827)
#define CARD_BG_ACT     lv_color_hex(0x1C2033)
#define STROKE_SOFT     lv_color_hex(0x1F2133)
#define TEXT_PRIMARY    lv_color_hex(0xF8F9FF)
#define TEXT_SECONDARY  lv_color_hex(0x9AA4B2)
#define TEXT_DIM        lv_color_hex(0x5C6675)
#define COLOR_SYNCING   lv_color_hex(0x3B82F6)   /* 蓝：同步中 */
#define COLOR_SYNCED    lv_color_hex(0x22C55E)   /* 绿：已同步 */
#define COLOR_DIM       lv_color_hex(0x3A4452)   /* 灰：待同步 */
#define AMBER           lv_color_hex(0xF59E0B)

/* 版面：455圆屏，卡片宽 400（左右各留 27），行高 78，间距 8 */
#define MAX_ROWS        OFFLINE_MAX_FILES
#define LIST_X          27
#define CARD_W          (LV_CIRCLE_WATCH - LIST_X * 2)
#define ROW_H           78
#define ROW_GAP         8
#define Y_TITLE         34
#define Y_SUBTITLE      74
#define Y_LIST          100
#define LIST_H          (LV_CIRCLE_WATCH - Y_LIST - 30)
#define SYNCED_FLASH_MS 1500

/****************************************************************************
 * 行 / 状态对象
 ****************************************************************************/
typedef struct {
    lv_obj_t      *row;
    char           name[64];
    offline_state_t state;
    uint64_t       start_ts_ms;
    uint32_t       size_bytes;
} row_t;

typedef struct {
    char     name[64];
    uint32_t expire_ms;     /* lv_tick_get() + SYNCED_FLASH_MS */
    bool     used;
} flash_t;

static lv_obj_t *s_container   = NULL;   /* 根容器 */
static lv_obj_t *s_title       = NULL;   /* 标题 */
static lv_obj_t *s_summary     = NULL;   /* 副标题：N 个文件 · X */
static lv_obj_t *s_back_btn    = NULL;   /* 左上返回箭头 */
static lv_obj_t *s_list       = NULL;   /* 可滚动列表容器 */
static lv_obj_t *s_empty      = NULL;   /* 空态 */
static lv_obj_t *s_toast      = NULL;   /* 轻量 toast */
static lv_obj_t *s_overlay    = NULL;   /* 删除确认框 */
static lv_timer_t *s_timer    = NULL;   /* 1s 兜底轮询 */
static row_t     s_rows[MAX_ROWS];
static int       s_row_count  = 0;
static flash_t   s_flash[MAX_ROWS];
static char      s_pending_delete[64];   /* 待确认删除的文件名 */
static void     (*s_close_hook)(void) = NULL;   /* 列表关闭时通知下层（如录音页立即刷新离线入口） */

/* 前向声明 */
static void row_event_cb(lv_event_t *e);
static void show_delete_confirm(const char *name);
static void confirm_event_cb(lv_event_t *e);
static void rebuild(void);

/****************************************************************************
 * 时间 / 时长格式化
 ****************************************************************************/

static void fmt_duration(uint32_t bytes, char *buf, size_t n)
{
    /* 16kbps Opus ≈ 2KB/s（设计文档 §11.3） */
    uint32_t sec = bytes / 2000;
    uint32_t mm  = sec / 60;
    uint32_t ss  = sec % 60;
    snprintf(buf, n, "%02u:%02u", mm, ss);
}

/** 字节数 → "1.2 MB" / "860 KB" */
static void fmt_size(uint32_t bytes, char *buf, size_t n)
{
    if (bytes >= 1024 * 1024)
        snprintf(buf, n, "%.1fM", (float)bytes / (1024.0f * 1024.0f));
    else if (bytes >= 1024)
        snprintf(buf, n, "%uK", (unsigned)(bytes / 1024));
    else
        snprintf(buf, n, "%uB", (unsigned)bytes);
}

/** 「今天/昨天/ MM-DD」+ 时分，比完整日期更适合小屏 */
static void fmt_day(uint64_t ts_ms, char *buf, size_t n)
{
    time_t sec = (time_t)(ts_ms / 1000);
    struct tm t;
    memset(&t, 0, sizeof(t));
    gmtime_r(&sec, &t);

    time_t now = time(NULL);
    struct tm tn;
    memset(&tn, 0, sizeof(tn));
    gmtime_r(&now, &tn);

    if (t.tm_year == tn.tm_year && t.tm_yday == tn.tm_yday)
        snprintf(buf, n, "今天 %02d:%02d", t.tm_hour, t.tm_min);
    else if (t.tm_year == tn.tm_year && t.tm_yday == tn.tm_yday - 1)
        snprintf(buf, n, "昨天 %02d:%02d", t.tm_hour, t.tm_min);
    else
        snprintf(buf, n, "%d月%d日 %02d:%02d",
                 t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
}

/****************************************************************************
 * 行（卡片）：左侧状态色条 + 主时间 + 副信息(时长·大小) + 右上状态徽标 + 右下时长
 ****************************************************************************/
static void add_row(const char *name, offline_state_t state,
                    uint64_t ts, uint32_t size, int y)
{
    int idx = s_row_count;
    if (idx >= MAX_ROWS)
        return;
    row_t *r = &s_rows[idx];

    const char *stxt;
    lv_color_t  scol;
    if (state == OFFLINE_SYNCING)      { stxt = "同步中"; scol = COLOR_SYNCING; }
    else if (state == OFFLINE_SYNCED)  { stxt = "已同步"; scol = COLOR_SYNCED;  }
    else                               { stxt = "待同步"; scol = COLOR_DIM;     }

    /* 卡片容器 */
    r->row = lv_obj_create(s_list);
    lv_obj_set_size(r->row, CARD_W, ROW_H);
    lv_obj_set_pos(r->row, 0, y);
    lv_obj_set_style_bg_color(r->row, CARD_BG, 0);
    lv_obj_set_style_bg_opa(r->row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(r->row, 1, 0);
    lv_obj_set_style_border_color(r->row, STROKE_SOFT, 0);
    lv_obj_set_style_radius(r->row, 16, 0);
    lv_obj_set_style_pad_all(r->row, 0, 0);
    lv_obj_clear_flag(r->row, LV_OBJ_FLAG_SCROLLABLE);

    /* 左侧状态色条（圆角竖条，用状态色标示，一眼区分） */
    lv_obj_t *bar = lv_obj_create(r->row);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 4, ROW_H - 28);
    lv_obj_align(bar, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_bg_color(bar, scol, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

    /* 主标题：今天 14:32 */
    char day[32], dbuf[16], sbuf[16], sub[48];
    fmt_day(ts, day, sizeof(day));
    fmt_duration(size, dbuf, sizeof(dbuf));
    fmt_size(size, sbuf, sizeof(sbuf));

    lv_obj_t *main = lv_label_create(r->row);
    lv_label_set_text(main, day);
    lv_obj_set_style_text_font(main, font_manager_get_font(FONT_LARGE), 0);
    lv_obj_set_style_text_color(main, TEXT_PRIMARY, 0);
    lv_obj_align(main, LV_ALIGN_TOP_LEFT, 22, 12);

    /* 副信息：时长 · 大小（无大小信息时只显示时长） */
    if (sbuf[0])
        snprintf(sub, sizeof(sub), "%s  %s", dbuf, sbuf);
    else
        snprintf(sub, sizeof(sub), "%s", dbuf);
    lv_obj_t *subl = lv_label_create(r->row);
    lv_label_set_text(subl, sub);
    lv_obj_set_style_text_font(subl, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(subl, TEXT_DIM, 0);
    lv_obj_align(subl, LV_ALIGN_BOTTOM_LEFT, 22, -12);

    /* 右上状态徽标：胶囊底 + 状态色文字 */
    lv_obj_t *badge = lv_obj_create(r->row);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, 58, 22);
    lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -12, 12);
    lv_obj_set_style_radius(badge, 11, 0);
    lv_obj_set_style_bg_color(badge, CARD_BG_ACT, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_t *bl = lv_label_create(badge);
    lv_label_set_text(bl, stxt);
    lv_obj_set_style_text_font(bl, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(bl, scol, 0);
    lv_obj_center(bl);

    r->state       = state;
    r->start_ts_ms = ts;
    r->size_bytes  = size;
    strncpy(r->name, name, sizeof(r->name) - 1);
    r->name[sizeof(r->name) - 1] = '\0';

    /* 仅 PENDING 行可长按删除；SYNCING/SYNCED 不开放（防误删进行中/已确认文件） */
    lv_obj_add_event_cb(r->row, row_event_cb, LV_EVENT_LONG_PRESSED,
                        (void *)(intptr_t)idx);
    s_row_count++;
}

/****************************************************************************
 * 行事件：PENDING 长按 → 删除确认
 ****************************************************************************/
static void row_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_LONG_PRESSED)
        return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_row_count)
        return;
    if (s_rows[idx].state == OFFLINE_PENDING)
        show_delete_confirm(s_rows[idx].name);
}

/****************************************************************************
 * 删除确认框
 ****************************************************************************/
static void confirm_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    int which = (int)(intptr_t)lv_event_get_user_data(e);
    if (which == 1) {
        offline_store_remove(s_pending_delete);   /* 删本地文件 + 从清单移除 */

        /* 删完后已无残留文件 → 直接退出本页，返回下层录音 App，不再重建空态 */
        if (offline_recordings_count() == 0) {
            offline_recordings_close();
            return;
        }
    }

    if (s_overlay) { lv_obj_del(s_overlay); s_overlay = NULL; }
    rebuild();
}

static void show_delete_confirm(const char *name)
{
    strncpy(s_pending_delete, name, sizeof(s_pending_delete) - 1);
    s_pending_delete[sizeof(s_pending_delete) - 1] = '\0';

    s_overlay = lv_obj_create(s_container);
    lv_obj_set_size(s_overlay, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_align(s_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *box = lv_obj_create(s_overlay);
    lv_obj_set_size(box, 320, 200);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, CARD_BG, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, STROKE_SOFT, 0);
    lv_obj_set_style_radius(box, 18, 0);
    lv_obj_set_style_pad_all(box, 0, 0);   /* 取消默认内边距，避免对齐错位/重叠 */
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    /* 标题：限制宽度、顶部留 24px，两行文字不会贴边或压到按钮 */
    lv_obj_t *lab = lv_label_create(box);
    lv_label_set_text(lab, "删除本地离线文件?\n(不会发送到手机)");
    lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lab, 284);
    lv_obj_set_style_text_align(lab, LV_TEXT_ALIGN_CENTER, 0);   /* 固定宽度下文字需显式居中 */
    lv_obj_set_style_text_font(lab, font_manager_get_font(FONT_LARGE), 0);
    lv_obj_set_style_text_color(lab, TEXT_PRIMARY, 0);
    lv_obj_align(lab, LV_ALIGN_TOP_MID, 0, 24);

    /* 两个按钮：各 128×46，距底边 24px、距左右各 18px → 中间留约 28px 间隙，互不重叠 */
    lv_obj_t *btn_cancel = lv_obj_create(box);
    lv_obj_set_size(btn_cancel, 128, 46);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 18, -24);
    lv_obj_set_style_bg_color(btn_cancel, STROKE_SOFT, 0);
    lv_obj_set_style_bg_opa(btn_cancel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_cancel, 23, 0);
    lv_obj_set_style_border_width(btn_cancel, 0, 0);   /* 去除默认主题边框（右侧圆角处的竖线） */
    lv_obj_set_style_outline_width(btn_cancel, 0, 0);
    lv_obj_clear_flag(btn_cancel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lc = lv_label_create(btn_cancel);
    lv_label_set_text(lc, "取消");
    lv_obj_set_style_text_font(lc, font_manager_get_font(FONT_MEDIUM), 0);
    lv_obj_set_style_text_color(lc, TEXT_PRIMARY, 0);
    lv_obj_center(lc);
    lv_obj_add_event_cb(btn_cancel, confirm_event_cb, LV_EVENT_CLICKED, (void *)0);

    lv_obj_t *btn_ok = lv_obj_create(box);
    lv_obj_set_size(btn_ok, 128, 46);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_RIGHT, -18, -24);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0xEF4444), 0);
    lv_obj_set_style_bg_opa(btn_ok, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_ok, 23, 0);
    lv_obj_set_style_border_width(btn_ok, 0, 0);   /* 去除默认主题边框（右侧圆角处的竖线） */
    lv_obj_set_style_outline_width(btn_ok, 0, 0);
    lv_obj_clear_flag(btn_ok, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lo = lv_label_create(btn_ok);
    lv_label_set_text(lo, "删除");
    lv_obj_set_style_text_font(lo, font_manager_get_font(FONT_MEDIUM), 0);
    lv_obj_set_style_text_color(lo, lv_color_white(), 0);
    lv_obj_center(lo);
    lv_obj_add_event_cb(btn_ok, confirm_event_cb, LV_EVENT_CLICKED, (void *)1);
}

/****************************************************************************
 * 轻量 toast（已同步提示）
 ****************************************************************************/
static void toast_timer_cb(lv_timer_t *t)
{
    if (s_toast) { lv_obj_del(s_toast); s_toast = NULL; }
    lv_timer_del(t);   /* 一次性 */
}

static void show_toast(const char *txt)
{
    if (!s_container)
        return;   /* 页面未打开则不弹 */
    if (s_toast) { lv_obj_del(s_toast); s_toast = NULL; }
    s_toast = lv_obj_create(s_container);
    lv_obj_set_size(s_toast, 160, 48);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_bg_color(s_toast, COLOR_SYNCED, 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_toast, 24, 0);
    lv_obj_set_style_border_width(s_toast, 0, 0);
    lv_obj_t *lab = lv_label_create(s_toast);
    lv_label_set_text(lab, txt);
    lv_obj_set_style_text_color(lab, lv_color_white(), 0);
    lv_obj_center(lab);
    lv_timer_create(toast_timer_cb, SYNCED_FLASH_MS, NULL);
}

/****************************************************************************
 * 重建列表（全量重绘）+ 顶部汇总（文件数 / 占用空间）
 ****************************************************************************/
static void rebuild(void)
{
    if (!s_list)
        return;

    for (int i = 0; i < s_row_count; i++)
        if (s_rows[i].row) lv_obj_del(s_rows[i].row);
    s_row_count = 0;

    if (s_empty) { lv_obj_del(s_empty); s_empty = NULL; }

    offline_file_entry_t entries[MAX_ROWS];
    int n = offline_store_scan(entries, MAX_ROWS);

    int y = 0;
    bool any = false;
    uint32_t total_bytes = 0;
    int shown = 0;

    for (int i = 0; i < n; i++) {
        if (offline_store_is_writing(entries[i].name))
            continue;   /* 跳过正在写入的当前录音文件 */
        add_row(entries[i].name, entries[i].state,
                entries[i].start_ts_ms, entries[i].size_bytes, y);
        y += ROW_H + ROW_GAP;
        total_bytes += entries[i].size_bytes;
        shown++;
        any = true;
    }

    /* 已同步闪光：ACK 后文件已在落盘模块删除，此处按 flash 暂存保留 1.5s 绿显 */
    uint32_t now = lv_tick_get();
    for (int i = 0; i < MAX_ROWS; i++) {
        if (!s_flash[i].used)
            continue;
        if ((int32_t)(now - s_flash[i].expire_ms) >= 0) {
            s_flash[i].used = false;
            continue;
        }
        /* flash 项无磁盘信息：用文件名时间戳推导展示 */
        uint64_t ts = offline_store_parse_ts(s_flash[i].name);
        add_row(s_flash[i].name, OFFLINE_SYNCED, ts, 0, y);
        y += ROW_H + ROW_GAP;
        any = true;
    }

    /* 顶部汇总：N 个文件 · 占用 X（给用户一个整体感知） */
    if (s_summary) {
        char sum[64], sz[16];
        fmt_size(total_bytes, sz, sizeof(sz));
        if (shown > 0)
            snprintf(sum, sizeof(sum), "%d 个文件   %s", shown, sz);
        else
            snprintf(sum, sizeof(sum), "无待同步文件");
        lv_label_set_text(s_summary, sum);
    }

    if (!any) {
        /* 空态：图形占位 + 主副文案，比单行文字更清晰 */
        s_empty = lv_obj_create(s_list);
        lv_obj_remove_style_all(s_empty);
        lv_obj_set_size(s_empty, 120, 120);
        lv_obj_align(s_empty, LV_ALIGN_CENTER, 0, -20);

        /* 简易麦克风图标（emoji 在本工程字体不可见，改用矢量绘制，保证显示） */
        lv_obj_t *mic = lv_obj_create(s_empty);
        lv_obj_remove_style_all(mic);
        lv_obj_set_size(mic, 16, 26);
        lv_obj_align(mic, LV_ALIGN_TOP_MID, 0, 16);
        lv_obj_set_style_bg_color(mic, COLOR_DIM, 0);
        lv_obj_set_style_bg_opa(mic, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(mic, 8, 0);   /* 胶囊 = 麦克风头 */

        lv_obj_t *mic_stand = lv_obj_create(s_empty);
        lv_obj_remove_style_all(mic_stand);
        lv_obj_set_size(mic_stand, 22, 3);
        lv_obj_align(mic_stand, LV_ALIGN_TOP_MID, 0, 45);
        lv_obj_set_style_bg_color(mic_stand, COLOR_DIM, 0);
        lv_obj_set_style_bg_opa(mic_stand, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(mic_stand, 1, 0);  /* 支架横线 */

        lv_obj_t *t1 = lv_label_create(s_empty);
        lv_label_set_text(t1, "暂无离线录音");
        lv_obj_set_style_text_font(t1, font_manager_get_font(FONT_LARGE), 0);
        lv_obj_set_style_text_color(t1, TEXT_SECONDARY, 0);
        lv_obj_align(t1, LV_ALIGN_BOTTOM_MID, 0, -34);

        lv_obj_t *t2 = lv_label_create(s_empty);
        lv_label_set_text(t2, "未连蓝牙时的录音\n会保存在这里");
        lv_obj_set_style_text_font(t2, font_manager_get_font(FONT_SMALL), 0);
        lv_obj_set_style_text_color(t2, TEXT_DIM, 0);
        lv_obj_set_style_text_align(t2, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(t2, LV_ALIGN_BOTTOM_MID, 0, -8);
    }
}

static void timer_cb(lv_timer_t *t)
{
    (void)t;
    rebuild();
}

/****************************************************************************
 * 同步成功（手机 ACK）→ 绿显 + toast + 1.5s 移除（跨线程安全）
 ****************************************************************************/
static void synced_async_cb(void *param)
{
    char *name = (char *)param;
    if (name) {
        for (int i = 0; i < MAX_ROWS; i++) {
            if (!s_flash[i].used) {
                strncpy(s_flash[i].name, name, sizeof(s_flash[i].name) - 1);
                s_flash[i].name[sizeof(s_flash[i].name) - 1] = '\0';
                s_flash[i].expire_ms = lv_tick_get() + SYNCED_FLASH_MS;
                s_flash[i].used = true;
                break;
            }
        }
        free(name);
        rebuild();
        show_toast("已同步");
    }
}

void offline_recordings_on_synced(const char *name)
{
    if (!name)
        return;
    char *copy = (char *)malloc(strlen(name) + 1);
    if (!copy)
        return;
    strcpy(copy, name);
    lv_async_call(synced_async_cb, copy);   /* 投到 LVGL 主线程 */
}

/****************************************************************************
 * 手势：右滑退出
 ****************************************************************************/
static void gesture_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE)
        return;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir != LV_DIR_RIGHT)
        return;

    /* 右滑退出：统一走 close（清理 + 通知下层刷新离线入口） */
    offline_recordings_close();
}

/****************************************************************************
 * 公开 API
 ****************************************************************************/

/* 返回按钮：关闭覆盖层（与右滑手势等价） */
static void back_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    offline_recordings_close();
}

void offline_recordings_open(void)
{
    lv_obj_t *scr = get_watch_scr();
    if (!scr)
        return;

    s_row_count = 0;
    for (int i = 0; i < MAX_ROWS; i++) s_flash[i].used = false;
    s_empty  = NULL;
    s_toast  = NULL;
    s_overlay = NULL;

    /* 根容器（圆形屏幕） */
    s_container = lv_obj_create(scr);
    lv_obj_set_size(s_container, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_align(s_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_container, BG_BASE, 0);
    lv_obj_set_style_bg_opa(s_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_container, 0, 0);
    lv_obj_set_style_pad_all(s_container, 0, 0);
    lv_obj_set_style_radius(s_container, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(s_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(s_container, gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_remove_flag(s_container, LV_OBJ_FLAG_GESTURE_BUBBLE);

    /* 左上返回箭头（圆形屏内安全坐标） */
    s_back_btn = lv_obj_create(s_container);
    lv_obj_remove_style_all(s_back_btn);
    lv_obj_set_size(s_back_btn, 34, 34);
    lv_obj_align(s_back_btn, LV_ALIGN_TOP_LEFT, 16, Y_TITLE - 4);
    lv_obj_set_style_radius(s_back_btn, 17, 0);
    lv_obj_set_style_bg_color(s_back_btn, CARD_BG, 0);
    lv_obj_set_style_bg_opa(s_back_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_back_btn, 1, 0);
    lv_obj_set_style_border_color(s_back_btn, STROKE_SOFT, 0);
    lv_obj_add_event_cb(s_back_btn, back_event_cb, LV_EVENT_CLICKED, NULL);
    {
        lv_obj_t *ar = lv_label_create(s_back_btn);
        lv_label_set_text(ar, "<");
        lv_obj_set_style_text_font(ar, font_manager_get_font(FONT_LARGE), 0);
        lv_obj_set_style_text_color(ar, TEXT_PRIMARY, 0);
        lv_obj_center(ar);
    }

    /* 标题（避开返回按钮，左移让位） */
    s_title = lv_label_create(s_container);
    lv_label_set_text(s_title, "离线录音");
    lv_obj_set_style_text_font(s_title, font_manager_get_font(FONT_LARGE), 0);
    lv_obj_set_style_text_color(s_title, TEXT_PRIMARY, 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 14, Y_TITLE);

    /* 副标题：文件数 / 占用空间（rebuild 时更新） */
    s_summary = lv_label_create(s_container);
    lv_label_set_text(s_summary, "");
    lv_obj_set_style_text_font(s_summary, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(s_summary, TEXT_DIM, 0);
    lv_obj_align(s_summary, LV_ALIGN_TOP_MID, 0, Y_SUBTITLE);

    /* 可滚动列表容器 */
    s_list = lv_obj_create(s_container);
    lv_obj_set_size(s_list, CARD_W, LIST_H);
    lv_obj_set_pos(s_list, LIST_X, Y_LIST);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_clear_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

    /* 底部提示：长按删除 */
    lv_obj_t *hint = lv_label_create(s_container);
    lv_label_set_text(hint, "长按可删除本地文件");
    lv_obj_set_style_text_font(hint, font_manager_get_font(FONT_SMALL), 0);
    lv_obj_set_style_text_color(hint, TEXT_DIM, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -22);

    rebuild();

    /* 兜底轮询：每 1s 重新扫描，捕获新建/删除/状态变化 */
    s_timer = lv_timer_create(timer_cb, 1000, NULL);
}

/****************************************************************************
 * 关闭覆盖层（供调用方页面退出时兜底清理）
 ****************************************************************************/
void offline_recordings_close(void)
{
    if (s_timer)   { lv_timer_del(s_timer);   s_timer   = NULL; }
    if (s_overlay) { lv_obj_del(s_overlay);    s_overlay = NULL; }
    if (s_toast)   { lv_obj_del(s_toast);      s_toast   = NULL; }
    if (s_container) { lv_obj_del(s_container); s_container = NULL; }
    s_list  = NULL;
    s_empty = NULL;
    s_title = NULL; s_summary = NULL; s_back_btn = NULL;
    for (int i = 0; i < s_row_count; i++) s_rows[i].row = NULL;
    s_row_count = 0;

    /* 通知下层（录音 App）：列表已关闭，立即刷新离线入口可见性，
       避免回到录音页时「离线」按钮残留一秒才消失 */
    if (s_close_hook)
        s_close_hook();
}

/** 注册/注销「列表关闭」回调（调用方在自身创建时注册、销毁时传 NULL 注销）。 */
void offline_recordings_set_close_hook(void (*cb)(void))
{
    s_close_hook = cb;
}

/****************************************************************************
 * 离线文件计数 / 可用性（排除正在写入的当前录音文件）
 ****************************************************************************/
int offline_recordings_count(void)
{
    offline_file_entry_t entries[MAX_ROWS];
    int n = offline_store_scan(entries, MAX_ROWS);
    int c = 0;
    for (int i = 0; i < n; i++)
        if (!offline_store_is_writing(entries[i].name))
            c++;
    return c;
}

bool offline_recordings_available(void)
{
    return offline_recordings_count() > 0;
}
