#include <nuttx/config.h>
#include <unistd.h>
#include <sys/boardctl.h>

#include <lvgl/lvgl.h>
#include <lvgl/demos/lv_demos.h>
#include <uv.h>
#include "../include/setting.h"
#include "../include/wifi.h"
#include "../../utils/include/font_manager.h"
#include "../../utils/include/circle_screen.h"
#include "../../main_page/include/watch_start.h"

#include <stdio.h>
#include <time.h>

// 全局变量
static lv_obj_t *main_screen;
static lv_obj_t *wifi_screen;
static lv_obj_t *volume_screen;
static lv_obj_t *brightness_screen;
static lv_obj_t *about_screen;

// 手势相关变量
static int gesture_start_x = 0;
static int gesture_start_y = 0;
static bool gesture_in_progress = false;
static const int gesture_threshold = 50; // 滑动阈值（像素）

// 当前音量值
static int ring_volume = 70;
static int media_volume = 80;

// 当前亮度值
static int brightness_level = 75;

// WiFi信息
static const char *wifi_name = "MyHome_WiFi_5G";
static const char *ip_address = "192.168.1.105";
static const char *mac_address = "A4:BB:6D:CC:77:2A";

static void ring_volume_slider_event(lv_event_t *e);
static void media_volume_slider_event(lv_event_t *e);
static void brightness_slider_event(lv_event_t *e);

// 右滑退出应用
static void exit_page(lv_obj_t* parent)
{
    if (parent) {
        lv_obj_add_flag(parent, LV_OBJ_FLAG_HIDDEN);
        // lv_obj_del(parent);
        // parent = NULL;
    }
}

// 子页面右滑回调：只删除子页面，不退出设置应用
void sub_page_gesture_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        if (dir == LV_DIR_RIGHT) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_delete(obj);
        }
    }
}

// 手势事件处理函数
static void gesture_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
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
                    exit_page(obj);
                }

                gesture_in_progress = false;
            }
            break;

        default:
            break;
    }
}

// 点击事件
static void item_click_event(lv_event_t *e) {
    uint32_t flag= lv_event_get_user_data(e);
    switch (flag)
    {
    case 0:
        wifi_connect_app();
        break;
    case 1:
        create_volume_screen();
        break;
    case 2:
        create_brightness_screen();
        break;
    case 3:
        create_about_screen();
        break;
    default:
        break;
    }
}

void setting_gesture_event_cb(lv_event_t *e)
{
    lv_obj_t * obj = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    switch (code)
    {
    case LV_EVENT_GESTURE:
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        if (dir == LV_DIR_RIGHT)
        {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_delete(obj);
            obj = NULL;
            nav_return_from_app();
            break;
        }

    default:
        break;
    }
}

// 创建返回按钮
void create_back_button(lv_obj_t* parent) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 80, 40);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 100, 22);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn,LV_OPA_TRANSP,0);
    // 移除边框
    lv_obj_set_style_border_width(btn, LV_OPA_TRANSP, 0);
    
    // 移除阴影
    lv_obj_set_style_shadow_width(btn, LV_OPA_TRANSP, 0);
   // lv_obj_set_style_radius(btn, 20, LV_PART_MAIN);
    
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, LV_SYMBOL_LEFT);

    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(label);

    lv_obj_add_flag(btn, LV_OBJ_FLAG_EVENT_BUBBLE);
    
    lv_obj_add_event_cb(parent, back_to_main_event, LV_EVENT_CLICKED, 0);
}

// 返回主菜单事件
void back_to_main_event(lv_event_t *e) {
   lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED){
      lv_obj_t * obj = lv_event_get_current_target(e);
       lv_obj_del(obj);
       obj = NULL;
    }
}

// 创建主设置界面
void create_main_settings_screen(void) {

    main_screen = lv_obj_create(get_watch_scr());
    lv_obj_set_size(main_screen, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_set_style_bg_color(main_screen, lv_color_black(), 0);
    lv_obj_set_style_radius(main_screen, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(main_screen, 0, 0);
    // lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x1a1a2e), 0);
    //new code
    lv_obj_set_scrollbar_mode(main_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(main_screen, LV_OBJ_FLAG_EVENT_BUBBLE); // 允许事件冒泡
    //new_code

    // 系统时间（与闹钟应用顶部样式一致）
    lv_obj_t *time_label = lv_label_create(main_screen);
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    tm_info->tm_hour = (tm_info->tm_hour + 8) % 24;
    char time_buf[8];
    strftime(time_buf, sizeof(time_buf), "%H:%M", tm_info);
    lv_label_set_text(time_label, time_buf);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
    lv_obj_align(time_label, LV_ALIGN_TOP_MID, 0, -10);

    // 标题
    lv_obj_t *title_label = lv_label_create(main_screen);
    lv_label_set_text(title_label, "Settings");
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_letter_space(title_label, 1, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 20);

    // 菜单容器
    lv_obj_t *menu_container = lv_obj_create(main_screen);
    lv_obj_set_size(menu_container, 380, 400);
    lv_obj_set_style_bg_color(menu_container, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
    lv_obj_set_style_border_width(menu_container, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(menu_container, 20, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(menu_container,LV_OPA_0,0);
    lv_obj_align(menu_container, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_scrollbar_mode(menu_container,LV_SCROLLBAR_MODE_OFF);
    
    // 菜单项样式（与睡眠应用 item 一致）
    static lv_style_t menu_item_style;
    lv_style_init(&menu_item_style);
    lv_style_set_bg_color(&menu_item_style, lv_color_hex(0x131520));
    lv_style_set_bg_opa(&menu_item_style, LV_OPA_COVER);
    lv_style_set_border_width(&menu_item_style, 1);
    lv_style_set_border_color(&menu_item_style, lv_color_hex(0x1F2133));
    lv_style_set_radius(&menu_item_style, 12);
    lv_style_set_pad_all(&menu_item_style, 12);
    
    static lv_style_t menu_text_style;
    lv_style_init(&menu_text_style);
    lv_style_set_text_color(&menu_text_style, lv_color_white());
    
    static lv_style_t icon_style;
    lv_style_init(&icon_style);
    lv_style_set_text_color(&icon_style, lv_color_hex(0x4a9cff));
    // lv_style_set_text_font(&icon_style, &lv_font_montserrat_16);
    lv_style_set_text_font(&icon_style, &lv_font_montserrat_14);
    
    // 菜单项数组
    const char *menu_items[] = {"网络连接", "音量调节", "亮度调节", "关于"};
    const char *menu_icons[] = {LV_SYMBOL_WIFI, LV_SYMBOL_VOLUME_MID, 
                               LV_SYMBOL_EYE_OPEN, LV_SYMBOL_HOME};
    
    // 创建菜单项
    for (int i = 0; i < 4; i++) {
        lv_obj_t *menu_item = lv_btn_create(menu_container);
        lv_obj_set_size(menu_item, 340, 60);
        lv_obj_add_style(menu_item, &menu_item_style, LV_PART_MAIN);
        lv_obj_align(menu_item, LV_ALIGN_TOP_MID, 0, 20 + i * 70);
        
        // 添加点击事件
        lv_obj_add_event_cb(menu_item, item_click_event, LV_EVENT_CLICKED, i);

        // 图标
        lv_obj_t *icon = lv_label_create(menu_item);
        lv_label_set_text(icon, menu_icons[i]);
        lv_obj_add_style(icon, &icon_style, LV_PART_MAIN);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 15, 0);
        
        // 文本
        lv_obj_t *label = lv_label_create(menu_item);
        lv_label_set_text(label, menu_items[i]);
        lv_obj_add_style(label, &menu_text_style, LV_PART_MAIN);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 50, 0);
        lv_obj_set_style_text_font(label, font_manager_get_font(20), 0);
        
        // 右箭头
        lv_obj_t *arrow = lv_label_create(menu_item);
        lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(arrow, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -15, 0);
    }

    lv_obj_add_event_cb(main_screen, setting_gesture_event_cb, LV_EVENT_GESTURE, main_screen);
    lv_obj_remove_flag(main_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

// 音量滑块事件
static void ring_volume_slider_event(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    ring_volume = lv_slider_get_value(slider);
    
    // 更新显示值
    lv_obj_t *label = (lv_obj_t*)lv_event_get_user_data(e);
    static char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", ring_volume);
    lv_label_set_text(label, buf);
}

static void media_volume_slider_event(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    media_volume = lv_slider_get_value(slider);
    
    // 更新显示值
    lv_obj_t *label = (lv_obj_t*)lv_event_get_user_data(e);
    static char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", media_volume);
    lv_label_set_text(label, buf);
}

// 创建音量设置界面
void create_volume_screen(void) {
    volume_screen = lv_obj_create(get_watch_scr());
    lv_obj_set_size(volume_screen, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_set_style_bg_color(volume_screen, lv_color_hex(0x1a1a2e), LV_PART_MAIN);
    lv_obj_set_style_radius(volume_screen, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(volume_screen, 0, 0);

    // 标题
    lv_obj_t *title_label = lv_label_create(volume_screen);
    lv_label_set_text(title_label, "音量设置");
    lv_obj_set_style_text_font(title_label, font_manager_get_font(20), 0);
    lv_obj_set_style_text_color(title_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title_label, font_manager_get_font(20), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 30);

    // 设置容器
    lv_obj_t *settings_container = lv_obj_create(volume_screen);
    lv_obj_set_size(settings_container, 380, 280);
    lv_obj_set_style_bg_color(settings_container, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
    lv_obj_set_style_border_width(settings_container, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(settings_container, 20, LV_PART_MAIN);
    lv_obj_align(settings_container, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_opa(settings_container,LV_OPA_0,0);

    // 铃声音量
    lv_obj_t *ring_label = lv_label_create(settings_container);
    lv_label_set_text(ring_label, "铃声");
    lv_obj_set_style_text_font(ring_label, font_manager_get_font(20), 0);
    lv_obj_set_style_text_color(ring_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(ring_label, LV_ALIGN_TOP_LEFT, 30, 30);
    
    lv_obj_t *ring_slider = lv_slider_create(settings_container);
    lv_slider_set_value(ring_slider, ring_volume, LV_ANIM_OFF);
    lv_obj_set_size(ring_slider, 200, 20);
    lv_obj_align(ring_slider, LV_ALIGN_TOP_LEFT, 30, 70);
    
    lv_obj_t *ring_value = lv_label_create(settings_container);
    static char ring_buf[8];
    snprintf(ring_buf, sizeof(ring_buf), "%d%%", ring_volume);
    lv_label_set_text(ring_value, ring_buf);
    lv_obj_set_style_text_color(ring_value, lv_color_hex(0xcccccc), LV_PART_MAIN);
    lv_obj_align(ring_value, LV_ALIGN_TOP_RIGHT, -30, 45);
    
    lv_obj_add_event_cb(ring_slider, ring_volume_slider_event, LV_EVENT_VALUE_CHANGED, ring_value);
    
    // 媒体音量
    lv_obj_t *media_label = lv_label_create(settings_container);
    lv_label_set_text(media_label, "媒体");
    lv_obj_set_style_text_color(media_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(media_label, font_manager_get_font(20), 0);
    lv_obj_align(media_label, LV_ALIGN_TOP_LEFT, 30, 120);
    
    lv_obj_t *media_slider = lv_slider_create(settings_container);
    lv_slider_set_value(media_slider, media_volume, LV_ANIM_OFF);
    lv_obj_set_size(media_slider, 200, 20);
    lv_obj_align(media_slider, LV_ALIGN_TOP_LEFT, 30, 160);
    
    lv_obj_t *media_value = lv_label_create(settings_container);
    static char media_buf[8];
    snprintf(media_buf, sizeof(media_buf), "%d%%", media_volume);
    lv_label_set_text(media_value, media_buf);
    lv_obj_set_style_text_color(media_value, lv_color_hex(0xcccccc), LV_PART_MAIN);
    lv_obj_align(media_value, LV_ALIGN_TOP_RIGHT, -30, 135);
    
    lv_obj_add_event_cb(media_slider, media_volume_slider_event, LV_EVENT_VALUE_CHANGED, media_value);

    // 启用拖拽功能以支持手势识别
    // lv_obj_add_flag(volume_screen, LV_OBJ_FLAG_SCROLLABLE);
    // lv_obj_set_scroll_dir(volume_screen, LV_DIR_HOR);
    // lv_obj_set_scroll_snap_x(volume_screen, LV_SCROLL_SNAP_NONE);

    // //添加手势事件处理器
    // lv_obj_add_event_cb(volume_screen, gesture_event_handler, LV_EVENT_ALL, NULL);

    lv_obj_add_event_cb(volume_screen, sub_page_gesture_cb, LV_EVENT_GESTURE, volume_screen);
    lv_obj_remove_flag(volume_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // 返回按钮
    create_back_button(volume_screen);
}

// 亮度滑块事件
static void brightness_slider_event(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    brightness_level = lv_slider_get_value(slider);
    
    // 更新显示值
    lv_obj_t *label = (lv_obj_t*)lv_event_get_user_data(e);
    static char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", brightness_level);
    lv_label_set_text(label, buf);
}

// 创建亮度设置界面
void create_brightness_screen(void) {
    brightness_screen = lv_obj_create(get_watch_scr());
    lv_obj_set_size(brightness_screen, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_set_style_bg_color(brightness_screen, lv_color_hex(0x1a1a2e), LV_PART_MAIN);
    lv_obj_set_style_radius(brightness_screen, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(brightness_screen, 0, 0);
      
    // 标题
    lv_obj_t *title_label = lv_label_create(brightness_screen);
    lv_label_set_text(title_label, "亮度设置");
    lv_obj_set_style_text_color(title_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title_label, font_manager_get_font(20), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 30);
    
    // 设置容器
    lv_obj_t *settings_container = lv_obj_create(brightness_screen);
    lv_obj_set_size(settings_container, 380, 200);
    lv_obj_set_style_bg_color(settings_container, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
    lv_obj_set_style_border_width(settings_container, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(settings_container, 20, LV_PART_MAIN);
    lv_obj_align(settings_container, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_opa(settings_container,LV_OPA_0,0);
    
    // 亮度图标
    lv_obj_t *brightness_icon = lv_label_create(settings_container);
    lv_label_set_text(brightness_icon, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_color(brightness_icon, lv_color_hex(0xffcc00), LV_PART_MAIN);
    lv_obj_set_style_text_font(brightness_icon, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(brightness_icon, LV_ALIGN_TOP_MID, 0, 20);
    
    // 亮度滑块
    lv_obj_t *brightness_slider = lv_slider_create(settings_container);
    lv_slider_set_value(brightness_slider, brightness_level, LV_ANIM_OFF);
    lv_obj_set_size(brightness_slider, 250, 20);
    lv_obj_align(brightness_slider, LV_ALIGN_TOP_MID, 0, 90);
    
    // 亮度值显示
    lv_obj_t *brightness_value = lv_label_create(settings_container);
    static char brightness_buf[8];
    snprintf(brightness_buf, sizeof(brightness_buf), "%d%%", brightness_level);
    lv_label_set_text(brightness_value, brightness_buf);
    lv_obj_set_style_text_color(brightness_value, lv_color_white(), LV_PART_MAIN);
    // lv_obj_set_style_text_font(brightness_value, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_font(brightness_value, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(brightness_value, LV_ALIGN_TOP_MID, 0, 120);
    
    lv_obj_add_event_cb(brightness_slider, brightness_slider_event, LV_EVENT_VALUE_CHANGED, brightness_value);

    // 启用拖拽功能以支持手势识别
    // lv_obj_add_flag(brightness_screen, LV_OBJ_FLAG_SCROLLABLE);
    // lv_obj_set_scroll_dir(brightness_screen, LV_DIR_HOR);
    // lv_obj_set_scroll_snap_x(brightness_screen, LV_SCROLL_SNAP_NONE);

    // //添加手势事件处理器
    // lv_obj_add_event_cb(brightness_screen, gesture_event_handler, LV_EVENT_ALL, NULL);

    // 添加手势事件处理
    lv_obj_add_event_cb(brightness_screen, sub_page_gesture_cb, LV_EVENT_GESTURE, brightness_screen);
    lv_obj_remove_flag(brightness_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);

      // 返回按钮
    create_back_button(brightness_screen);
}

// 创建关于界面
void create_about_screen(void) {
    about_screen = lv_obj_create(get_watch_scr());
    lv_obj_set_size(about_screen, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_set_style_bg_color(about_screen, lv_color_hex(0x1a1a2e), LV_PART_MAIN);
    lv_obj_set_style_radius(about_screen, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(about_screen, 0, 0);
    
    // 标题
    lv_obj_t *title_label = lv_label_create(about_screen);
    lv_label_set_text(title_label, "关于");
    lv_obj_set_style_text_color(title_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title_label, font_manager_get_font(20), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 30);
    
    // 信息容器
    lv_obj_t *info_container = lv_obj_create(about_screen);
    lv_obj_set_size(info_container, 380, 280);
    lv_obj_set_style_bg_color(info_container, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
    lv_obj_set_style_border_width(info_container, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(info_container, 20, LV_PART_MAIN);
    lv_obj_align(info_container, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_opa(info_container,LV_OPA_0,0);
    
    // 应用图标
    lv_obj_t *app_icon = lv_label_create(info_container);
    lv_label_set_text(app_icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(app_icon, lv_color_hex(0x4a9cff), LV_PART_MAIN);
    lv_obj_set_style_text_font(app_icon, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(app_icon, LV_ALIGN_TOP_MID, 0, 20);
    
    // 应用名称
    lv_obj_t *app_name = lv_label_create(info_container);
    lv_label_set_text(app_name, "Watch");
    lv_obj_set_style_text_color(app_name, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(app_name, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(app_name, LV_ALIGN_TOP_MID, 0, 90);
    
    // 版本信息
    lv_obj_t *version_label = lv_label_create(info_container);
    lv_label_set_text(version_label, "Version: 2.1.0");
    lv_obj_set_style_text_color(version_label, lv_color_hex(0xcccccc), LV_PART_MAIN);
    // lv_obj_set_style_text_font(version_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_font(version_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(version_label, LV_ALIGN_TOP_MID, 0, 140);
    
    // 设备信息
    lv_obj_t *device_label = lv_label_create(info_container);
    lv_label_set_text(device_label, "Type: Watch Pro");
    lv_obj_set_style_text_color(device_label, lv_color_hex(0xcccccc), LV_PART_MAIN);
    // lv_obj_set_style_text_font(device_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_font(device_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(device_label, LV_ALIGN_TOP_MID, 0, 180);
    
    // 序列号
    lv_obj_t *serial_label = lv_label_create(info_container);
    lv_label_set_text(serial_label, "Serial number: WT20241234001");
    lv_obj_set_style_text_color(serial_label, lv_color_hex(0xcccccc), LV_PART_MAIN);
    // lv_obj_set_style_text_font(serial_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_font(serial_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(serial_label, LV_ALIGN_TOP_MID, 0, 210);
    
    // 版权信息
    lv_obj_t *copyright_label = lv_label_create(info_container);
    lv_label_set_text(copyright_label, "© 2024 Technology Companies");
    lv_obj_set_style_text_color(copyright_label, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_text_font(copyright_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(copyright_label, LV_ALIGN_BOTTOM_MID, 0, 0);

    // 启用拖拽功能以支持手势识别
    // lv_obj_add_flag(about_screen, LV_OBJ_FLAG_SCROLLABLE);
    // lv_obj_set_scroll_dir(about_screen, LV_DIR_HOR);
    // lv_obj_set_scroll_snap_x(about_screen, LV_SCROLL_SNAP_NONE);

    // //添加手势事件处理器
    // lv_obj_add_event_cb(about_screen, gesture_event_handler, LV_EVENT_ALL, NULL);

    // 添加手势事件处理
    lv_obj_add_event_cb(about_screen, sub_page_gesture_cb, LV_EVENT_GESTURE, about_screen);
    lv_obj_remove_flag(about_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // 返回按钮
    create_back_button(about_screen);
    
}

// 应用初始化
void settings_app_start(void) {
    // 创建所有界面
    create_main_settings_screen();
}
