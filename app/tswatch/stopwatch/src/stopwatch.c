#include <nuttx/config.h>
#include <unistd.h>
#include <sys/boardctl.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <nuttx/compiler.h>
#include <lvgl/src/font/lv_font.h>

#include <lvgl/lvgl.h>
#include <lvgl/demos/lv_demos.h>
#include <stdio.h>
#include "../include/stopwatch.h"
#include "../include/systemtime.h"
#include "../../utils/include/circle_screen.h"
#include "../../utils/include/font_manager.h"
#include "../../nav_page/include/nav_page_manager.h"
#include "../../main_page/include/watch_start.h"

static uint32_t stopwatch_start_time = 0;
static uint32_t stopwatch_elapsed_time = 0;
static bool stopwatch_running = false;

static lv_obj_t *stopwatch_container = NULL;
static lv_obj_t *title_label;//秒表标题
static lv_obj_t *time_label;//系统时间
static lv_timer_t *time_timer;//时间定时器
static lv_obj_t *stopwatch_label;//秒表显示
static lv_timer_t *stopwatch_timer;//秒表定时器
static lv_obj_t *start_btn;
static lv_obj_t *pause_btn;
static lv_obj_t *reset_btn;

//手势相关变量
static int gesture_start_x = 0;
static int gesture_start_y = 0;
static bool gesture_in_progress = false;
static const int gesture_threshold = 50;//滑动阈值

//系统时间
static void update_system_time(void)
{
    char time_str[10];
    system_time_get_string(time_str, sizeof(time_str));
    lv_label_set_text(time_label, time_str);
}

static void time_timer_cb(lv_timer_t *timer)
{
    update_system_time();
}

//秒表时间
static void update_stopwatch_time(void)
{
    uint32_t current_time;
    uint32_t total_elapsed;
    char time_str[16];

    if (stopwatch_running) {
        current_time = lv_tick_get();
        total_elapsed = stopwatch_elapsed_time + (current_time - stopwatch_start_time);
    } else {
        total_elapsed = stopwatch_elapsed_time;
    }

    uint32_t minutes = total_elapsed / 60000;
    uint32_t seconds = (total_elapsed % 60000) / 1000;
    uint32_t milliseconds = total_elapsed % 1000;

    snprintf(time_str, sizeof(time_str), "%02lu:%02lu.%03lu", minutes, seconds, milliseconds);

    /* UI 可能已被删除（后台运行），仅在 UI 存在时更新显示 */
    if (stopwatch_label) {
        lv_label_set_text(stopwatch_label, time_str);
    }
}

static void stopwatch_timer_cb(lv_timer_t *timer)
{
    update_stopwatch_time();
}

//开始
void start_stopwatch(void)
{
    if (!stopwatch_running) {
        stopwatch_start_time = lv_tick_get();
        stopwatch_running = true;

        /* UI 可能已被删除（后台运行），仅在 UI 存在时更新按钮 */
        if (pause_btn && start_btn) {
            lv_obj_clear_flag(pause_btn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(start_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

//暂停
void pause_stopwatch(void)
{
    if (stopwatch_running) {
        uint32_t current_time = lv_tick_get();
        stopwatch_elapsed_time += (current_time - stopwatch_start_time);
        stopwatch_running = false;

        /* UI 可能已被删除（后台运行），仅在 UI 存在时更新按钮 */
        if (pause_btn && start_btn) {
            lv_obj_add_flag(pause_btn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(start_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

//重置
void reset_stopwatch(void)
{
    stopwatch_running = false;
    stopwatch_elapsed_time = 0;
    stopwatch_start_time = 0;

    /* UI 可能已被删除（后台运行），仅在 UI 存在时更新 */
    if (stopwatch_label) {
        update_stopwatch_time();
    }
    if (pause_btn && start_btn) {
        lv_obj_add_flag(pause_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(start_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Public wrappers for MCP voice control. */

void stopwatch_pause(void)
{
    pause_stopwatch();
}

void stopwatch_resume(void)
{
    start_stopwatch();
}

void stopwatch_reset(void)
{
    reset_stopwatch();
}

static void start_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    LV_LOG_USER("Start button event: %d", code);

    if (code == LV_EVENT_CLICKED) {
        start_stopwatch();
    }
}

static void pause_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        pause_stopwatch();
    }
}

static void reset_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        reset_stopwatch();
    }
}

//隐藏秒表界面（不重置秒表，保持后台运行状态）
static void exit_stopwatch(void)
{
    if (stopwatch_container) {
        lv_timer_delete(time_timer);
        lv_timer_delete(stopwatch_timer);

        /* 先恢复上层页面（appList 或表盘），再删除秒表容器，
         * 避免删除后无内容显示导致黑屏。 */
        nav_return_from_app();

        lv_obj_delete(stopwatch_container);
        stopwatch_container = NULL;
        time_label = NULL;
        stopwatch_label = NULL;
        start_btn = NULL;
        pause_btn = NULL;
        reset_btn = NULL;

        LV_LOG_USER("Stopwatch exited (running=%d)\n", stopwatch_running);
    }
}

//手势事件处理函数
static void gesture_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_RELEASED) return;
    lv_obj_t *obj = lv_event_get_target(e);
    lv_indev_t *indev = lv_event_get_indev(e);
    
    if (!indev) return;
    
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    
    switch (code) {
        case LV_EVENT_PRESSED:
            gesture_start_x = point.x;
            gesture_start_y = point.y;
            gesture_in_progress = true;
            break;
        case LV_EVENT_RELEASED:
            if (gesture_in_progress) {
                int delta_x = point.x - gesture_start_x;
                int delta_y = point.y - gesture_start_y;
                
                // 检查是否是右滑手势（水平滑动距离大于阈值，且垂直滑动较小）
                if (delta_x > gesture_threshold && abs(delta_y) < gesture_threshold) {
                    // 修改：右滑隐藏秒表界面
                    exit_stopwatch();
                }
                
                gesture_in_progress = false;
            }
            break;
        default:
            break;
    }
}

static void stopwatch_gesture_event_cb(lv_event_t *e)
{
    lv_obj_t * obj = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    switch (code)
    {
    case LV_EVENT_GESTURE:
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        if (dir == LV_DIR_BOTTOM)
        {
            LV_LOG_USER(" LOG_DIR_BOTTOM---But do nothing\n");
        }
        if (dir == LV_DIR_TOP)
        {
            LV_LOG_USER(" LV_DIR_TOP---But do nothing\n");
        }
        if (dir == LV_DIR_LEFT)
        {
            LV_LOG_USER(" LV_DIR_LEFT---But do nothing\n");
        }
        if (dir == LV_DIR_RIGHT)
        {
            LV_LOG_USER(" LV_DIR_RIGHT---Exit\n");
            exit_stopwatch();
            break;
        }

    default:
        break;
    }
}

// 创建圆形按钮
static lv_obj_t *create_circle_button(lv_obj_t *parent, const char *text, lv_color_t color, lv_event_cb_t event_cb)
{
    LV_LOG_USER("-------sunbin--------In %s:enter %s:%d\n", __FILE__, __func__,__LINE__);
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 70, 70);
    lv_obj_set_style_radius(btn, 35, 0);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_text_color(btn, lv_color_white(), 0);
    lv_obj_set_style_text_font(btn, font_manager_get_font(FONT_LARGE), 0);
    
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, NULL);
    
    return btn;
}

void stopwatch_start(void) {
    /* If the stopwatch UI already exists, just show it. */
    if (stopwatch_container != NULL) {
        lv_obj_clear_flag(stopwatch_container, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    //创建圆形遮罩
    stopwatch_container = lv_obj_create(get_watch_scr());
    lv_obj_set_size(stopwatch_container, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_set_style_bg_color(stopwatch_container, lv_color_black(), 0);
    lv_obj_set_style_radius(stopwatch_container, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(stopwatch_container, 0, 0);

    lv_obj_set_scrollbar_mode(stopwatch_container, LV_SCROLLBAR_MODE_OFF);
    // 添加手势事件处理
    lv_obj_add_event_cb(stopwatch_container, stopwatch_gesture_event_cb, LV_EVENT_GESTURE, stopwatch_container);
    lv_obj_remove_flag(stopwatch_container, LV_OBJ_FLAG_GESTURE_BUBBLE);

    //系统时间（顶部居中，与闹钟应用一致：montserrat_22, y=20）
    time_label = lv_label_create(stopwatch_container);
    update_system_time();
    lv_obj_align(time_label, LV_ALIGN_TOP_MID, 0, -10);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(time_label, lv_color_white(), 0);

    //秒表标题（montserrat_32, y=20）
    title_label = lv_label_create(stopwatch_container);
    lv_label_set_text(title_label, "StopWatch");
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(title_label, 1, 0);

    //中间计时（放大字体：montserrat_32）
    stopwatch_label = lv_label_create(stopwatch_container);
    lv_obj_align(stopwatch_label, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_text_font(stopwatch_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(stopwatch_label, lv_color_hex(0xFFFFFF), 0);

    // 创建控制按钮
    lv_obj_t *btn_container = lv_obj_create(stopwatch_container);
    lv_obj_set_size(btn_container, 300, 80);
    lv_obj_align(btn_container, LV_ALIGN_CENTER, 0, 120);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(btn_container, LV_OPA_TRANSP, 0);

    //开始
    start_btn = create_circle_button(btn_container, "start", lv_color_hex(0x10B981), start_btn_event_handler);

    //暂停
    pause_btn = create_circle_button(btn_container, "stop", lv_color_hex(0xF59E0B), pause_btn_event_handler);

    //重置
    reset_btn = create_circle_button(btn_container, "reset", lv_color_hex(0x3498db), reset_btn_event_handler);

    time_timer = lv_timer_create(time_timer_cb, 1000, NULL);
    stopwatch_timer = lv_timer_create(stopwatch_timer_cb, 50, NULL);

    /* 根据秒表状态恢复 UI：
     * - 如果秒表正在运行：显示暂停按钮，隐藏开始按钮
     * - 如果秒表已暂停或未开始：显示开始按钮，隐藏暂停按钮
     */
    if (stopwatch_running) {
        lv_obj_clear_flag(pause_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(start_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(pause_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(start_btn, LV_OBJ_FLAG_HIDDEN);
    }

    /* 立即更新一次秒表显示 */
    update_stopwatch_time();
}