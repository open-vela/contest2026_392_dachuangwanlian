#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/alarm_view.h"
#include "../include/alarm_model.h"
#include "../include/alarm_presenter.h"
#include "../../utils/include/font_manager.h"
#include "../../utils/include/circle_screen.h"
#include "../../main_page/include/watch_start.h"

LV_IMAGE_DECLARE(clock_icon);

static AlarmView alarm_view;

static lv_style_t screen_style;
static lv_style_t clock_style;
static lv_style_t nowtime_style;
static lv_style_t btn_style;
static lv_style_t roller_sel_style;
static lv_style_t roller_style;
// 事件回调函数
static void goto_edit_cb(lv_event_t* e) {
    alarm_view_show_edit_screen();
}
static void goto_repeat_cb(lv_event_t* e) {
    alarm_view_show_repeat_screen();
}
static void back_main_cb(lv_event_t* e) {
    alarm_view_show_main_screen();
}

static void back_mode_cb(lv_event_t* e) {
    alarm_view_show_mode_screen();
}
static void delete_alarm_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    LV_LOG_USER("删除闹钟 %d\n", idx);
    alarm_presenter_remove_alarm(idx);
}

static void long_pressed_cb(lv_event_t* e) {
    alarm_presenter_set_long_pressed(true);
}

static void select_del_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t* cb_sel = lv_event_get_target(e);
    bool selected = lv_obj_has_state(cb_sel, LV_STATE_CHECKED);
    alarm_presenter_select_alarm(idx, selected);
}
static void del_selected_cb(lv_event_t* e) {
    alarm_presenter_remove_selected_alarms();
}
void alarm_view_remove_alarm(int index) {
    if (index < 0 || index >= MAX_ALARM_COUNT || !alarm_view.alarm_containers[index]) return;
    lv_obj_delete(alarm_view.alarm_containers[index]);
    alarm_view.alarm_containers[index] = NULL;
    alarm_view.alarm_switches[index] = NULL;
    alarm_view.alarm_checkboxes[index] = NULL;
    //alarm_view_update_alarm_list(); 
}
void alarm_view_remove_selected_alarms() {
    AlarmModel* model = get_alarm_model(); 
    for (int i = 0; i < model->alarm_count; i++) {
        if (model->alarms[i].selected) {
            alarm_view_remove_alarm(i); 
        }
    }
    //alarm_view_update_alarm_list(); 
}
static void goto_alarm_mode_cb(lv_event_t* e) {
    AlarmModel * model = get_alarm_model();
    if(model->long_pressed == true) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    LV_LOG_USER("闹钟index: %d\n",idx);
    if (idx >= 0) {
        alarm_view_show_mode_screen();
        lv_label_set_text(alarm_view.time_item, model->alarms[idx].time);
    }
}

/* 根据开关状态刷新闹钟卡片配色：开启→橙底橙边+橙色时间；关闭→整体半透+白色时间 */
static void apply_alarm_item_style(int idx, bool is_on) {
    if (idx < 0 || idx >= MAX_ALARM_COUNT) return;
    lv_obj_t * alarm_item = alarm_view.alarm_containers[idx];
    lv_obj_t * sw = alarm_view.alarm_switches[idx];
    if (!alarm_item) return;
    /* content 是 alarm_item 的第一个子对象 */
    lv_obj_t * content = lv_obj_get_child(alarm_item, 0);
    if (!content) return;
    /* 时间标签是 content 的第一个子对象 */
    lv_obj_t * alarm_time = lv_obj_get_child(content, 0);

    if (is_on) {
        /* 开启态: 橙底 rgba(255,165,2,0.08) + 橙边 rgba(255,165,2,0.3) + 橙色时间 */
        lv_obj_set_style_bg_color(content, lv_color_make(0xFF, 0xA5, 0x02), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(content, 20, LV_PART_MAIN);
        lv_obj_set_style_border_color(content, lv_color_make(0xFF, 0xA5, 0x02), LV_PART_MAIN);
        lv_obj_set_style_border_opa(content, LV_OPA_30, LV_PART_MAIN);
        lv_obj_set_style_opa(content, LV_OPA_COVER, LV_PART_MAIN);
        if (alarm_time) lv_obj_set_style_text_color(alarm_time, lv_color_make(0xFF, 0xA5, 0x02), LV_PART_MAIN);
    } else {
        /* 关闭态: 灰底 + 1px 灰边 + 整体半透明 opacity:0.5 + 白色时间 */
        lv_obj_set_style_bg_color(content, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(content, 15, LV_PART_MAIN);
        lv_obj_set_style_border_color(content, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_border_opa(content, 31, LV_PART_MAIN);
        lv_obj_set_style_opa(content, LV_OPA_50, LV_PART_MAIN);
        if (alarm_time) lv_obj_set_style_text_color(alarm_time, lv_color_white(), LV_PART_MAIN);
    }
    if (sw) {
        if (is_on) lv_obj_add_state(sw, LV_STATE_CHECKED);
        else lv_obj_clear_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_invalidate(alarm_item);
}

/* 开关切换回调：实时刷新卡片配色 */
static void alarm_switch_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t * sw = lv_event_get_target(e);
    bool is_on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    LV_LOG_USER("闹钟 %d 开关切换: %s\n", idx, is_on ? "开启" : "关闭");
    apply_alarm_item_style(idx, is_on);
}

static void add_clock_cb(lv_event_t* e) {
    char hour[8], minute[8];
    lv_roller_get_selected_str(alarm_view.hour_roller, hour, sizeof(hour));
    lv_roller_get_selected_str(alarm_view.minute_roller, minute, sizeof(minute));
    LV_LOG_USER("添加闹钟 %s:%s\n", hour, minute);
    LV_LOG_USER("hour长度: %zu, minute长度: %zu\n", strlen(hour), strlen(minute));
    alarm_presenter_add_alarm(hour, minute);
    //alarm_view_show_main_screen();
}

void alarm_view_create_alarm(char* time_str, int index) {
    LV_LOG_USER("进入alarm_view_create_alarm函数\n");
    AlarmModel* model = get_alarm_model();
    LV_LOG_USER("model->alarm_count: %d\n", model->alarm_count);
    LV_LOG_USER("time_str: %s\n", time_str);
    LV_LOG_USER("crrent_idx: %d\n", index);
    
    // 检查索引是否有效
    if (model->alarm_count > MAX_ALARM_COUNT) {
        LV_LOG_USER("闹钟数量无效: %d\n", model->alarm_count);
        return;
    }

    //new new code
    if (index < 0 || index >= MAX_ALARM_COUNT)
    {
        LV_LOG_USER("闹钟数组索引越界： %d\n", index);
        return;
    }
    
    // 使用正确的索引（最后一个闹钟的索引）
      //int current_idx = model->alarm_count - 1;
      int current_idx = index;
    LV_LOG_USER("current_idx: %d\n", current_idx);
    
    // 检查索引是否有效
    if (current_idx < 0 || current_idx >= MAX_ALARM_COUNT) {
        LV_LOG_USER("闹钟数组索引越界: %d\n", current_idx);
        return;
    }
    
    lv_obj_t * alarm_item = lv_obj_create(alarm_view.alarm_container);
    if (!alarm_item) {
        LV_LOG_USER("创建alarm_item失败\n");
        return;
    }
    LV_LOG_USER("alarm_item创建成功\n");

    lv_obj_set_size(alarm_item, lv_pct(100), 84);
    lv_obj_set_style_bg_color(alarm_item, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(alarm_item, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(alarm_item, 0, LV_PART_MAIN);
    /* 去掉框框本身可滑动 + 去掉滚动条 */
    lv_obj_remove_flag(alarm_item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(alarm_item, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(alarm_item, LV_DIR_NONE);
    lv_obj_set_style_pad_all(alarm_item, 0, 0);
    lv_obj_set_style_border_side(alarm_item, LV_BORDER_SIDE_NONE, LV_PART_MAIN);
    lv_obj_set_layout(alarm_item, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(alarm_item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(alarm_item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t * content = lv_obj_create(alarm_item);
    if (!content) {
        LV_LOG_USER("创建content失败\n");
        lv_obj_del(alarm_item);
        return;
    }
    LV_LOG_USER("content创建成功\n");
    
    lv_obj_set_size(content, lv_pct(100), 84);
    /* 去掉 content 自身滚动 + 滚动条 */
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(content, LV_DIR_NONE);
    /* 背景: rgba(255,255,255,0.06) —— 半透明白底（深底上呈淡灰半透） */
    lv_obj_set_style_bg_color(content, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(content, 15, LV_PART_MAIN);
    /* 边框: 1px solid rgba(255,255,255,0.12) */
    lv_obj_set_style_border_width(content, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(content, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(content, 31, LV_PART_MAIN);
    lv_obj_set_style_border_side(content, LV_BORDER_SIDE_FULL, LV_PART_MAIN);
    /* 内边距 14px 18px */
    lv_obj_set_style_pad_ver(content, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(content, 18, LV_PART_MAIN);
    lv_obj_set_style_margin_all(content, 0, LV_PART_MAIN);
    /* 圆角 16px */
    lv_obj_set_style_radius(content, 16, LV_PART_MAIN);

    lv_obj_add_event_cb(content, long_pressed_cb, LV_EVENT_LONG_PRESSED, NULL);

    lv_obj_t * alarm_time = lv_label_create(content);
    if (!alarm_time) {
        LV_LOG_USER("创建alarm_time失败\n");
        lv_obj_del(alarm_item);
        return;
    }
    LV_LOG_USER("alarm_time创建成功\n");

    // lv_label_set_text(alarm_time, time_str);
    lv_label_set_text(alarm_time, time_str);
    lv_obj_set_style_text_font(alarm_time, &lv_font_montserrat_22, LV_PART_MAIN);  /* ≈26px 加粗 */
    /* 时间颜色由 apply_alarm_item_style() 统一设置，此处先设白色占位 */
    lv_obj_set_style_text_color(alarm_time, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(alarm_time, LV_ALIGN_LEFT_MID, 0, -7);

    lv_obj_t * alarm_mode = lv_label_create(content);
    if (!alarm_mode) {
        LV_LOG_USER("创建alarm_mode失败\n");
        lv_obj_del(alarm_item);
        return;
    }
    LV_LOG_USER("alarm_mode创建成功\n");

    lv_obj_set_style_text_font(alarm_mode, font_manager_get_font(FONT_SMALL), LV_PART_MAIN);  /* 14px 最接近12px */
    lv_obj_set_style_text_color(alarm_mode, lv_color_make(0x5A, 0x5F, 0x80), LV_PART_MAIN);   /* #5A5F80 */
    /* 副标题文案匹配 HTML 范例 */
    const char* meta_text[] = {"周一至周五", "午休·每天", "睡觉·仅一次"};
    const char* mt = (current_idx < 3) ? meta_text[current_idx] : "每天";
    lv_label_set_text(alarm_mode, mt);
    lv_obj_align(alarm_mode, LV_ALIGN_LEFT_MID, 0, 13);

    lv_obj_t * sw = lv_switch_create(content);
    if (!sw) {
        LV_LOG_USER("创建switch失败\n");
        lv_obj_del(alarm_item);
        return;
    }
    LV_LOG_USER("switch创建成功\n");
    
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_size(sw, 40, 20);
    lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, alarm_switch_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)current_idx);

    lv_obj_t * checkbox_del = lv_checkbox_create(content);
    if (!checkbox_del) {
        LV_LOG_USER("创建checkbox_del失败\n");
        lv_obj_del(alarm_item);
        return;
    }
    LV_LOG_USER("checkbox_del创建成功\n");
    
    lv_obj_align(checkbox_del, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_size(checkbox_del, 20, 20);
    lv_obj_set_style_radius(checkbox_del, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(checkbox_del, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(checkbox_del, lv_color_make(85, 70, 78), LV_PART_INDICATOR);
    lv_obj_add_flag(checkbox_del, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(checkbox_del, select_del_cb, LV_EVENT_CLICKED, (void*)(intptr_t)current_idx);

    lv_obj_t * delete_btn = lv_button_create(alarm_item);
    if (!delete_btn) {
        LV_LOG_USER("创建delete_btn失败\n");
        lv_obj_del(alarm_item);
        return;
    }
    LV_LOG_USER("delete_btn创建成功\n");
    
    lv_obj_set_size(delete_btn, 50, 50);
    lv_obj_set_style_bg_color(delete_btn, lv_color_make(255, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_radius(delete_btn, 10, LV_PART_MAIN);
    lv_obj_align(delete_btn, LV_ALIGN_RIGHT_MID, 0, 0); 

    lv_obj_t * delete_label = lv_label_create(delete_btn);
    if (!delete_label) {
        LV_LOG_USER("创建delete_label失败\n");
        lv_obj_del(alarm_item);
        return;
    }
    LV_LOG_USER("delete_label创建成功\n");
    
    lv_label_set_text(delete_label, LV_SYMBOL_TRASH);
    lv_obj_set_style_text_color(delete_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(delete_label);
    lv_obj_add_event_cb(delete_btn, delete_alarm_cb, LV_EVENT_CLICKED, (void*)(intptr_t)current_idx);

    lv_obj_add_event_cb(content, goto_alarm_mode_cb, LV_EVENT_CLICKED, (void*)(intptr_t)current_idx);

    // 存储引用
    alarm_view.alarm_containers[current_idx] = alarm_item;
    alarm_view.alarm_switches[current_idx] = sw;
    alarm_view.alarm_checkboxes[current_idx] = checkbox_del;

    /* 初始开关状态：预置的前两个开启、第三个关闭（匹配 HTML 范例）；用户新增的默认开启 */
    bool init_on = (current_idx != 2);
    if (!init_on) lv_obj_clear_state(sw, LV_STATE_CHECKED);
    apply_alarm_item_style(current_idx, init_on);

    // 强制更新布局
    lv_obj_update_layout(alarm_item);
    lv_obj_update_layout(alarm_view.alarm_container);

    // 强制刷新显示
    lv_obj_invalidate(alarm_view.alarm_container);

    LV_LOG_USER("alarm_view_create_alarm执行完毕,current_idx: %d\n", current_idx);
}

//new code
static void exit_alarm(void)
{
    AlarmView* view = get_alarm_view();
    if (view) {
        /* 删除所有闹钟UI对象，释放内存 */
        if (view->main_screen) {
            lv_obj_delete(view->main_screen);
            view->main_screen = NULL;
        }
        if (view->edit_screen) {
            lv_obj_delete(view->edit_screen);
            view->edit_screen = NULL;
        }
        if (view->mode_screen) {
            lv_obj_delete(view->mode_screen);
            view->mode_screen = NULL;
        }
        if (view->repeat_screen) {
            lv_obj_delete(view->repeat_screen);
            view->repeat_screen = NULL;
        }
        memset(view->alarm_containers, 0, sizeof(view->alarm_containers));
        memset(view->alarm_switches, 0, sizeof(view->alarm_switches));
        memset(view->alarm_checkboxes, 0, sizeof(view->alarm_checkboxes));

        /* 子label对象随screen一起被删除，必须置NULL防止悬空指针 */
        view->main_time_label = NULL;
        view->clock_label = NULL;
        view->clock_img = NULL;
        view->no_clock_label = NULL;
        view->alarm_container = NULL;
        view->btn_addClock = NULL;
        view->del_sel_btn = NULL;
        view->edit_time_label = NULL;
        view->hour_roller = NULL;
        view->minute_roller = NULL;
        view->mode_time_label = NULL;
        view->time_item = NULL;
        view->item_container = NULL;
        view->repeat_time_label = NULL;

        LV_LOG_USER("闹钟界面已清理\n");
    }
    /* 恢复上一个页面（应用列表或表盘） */
    nav_return_from_app();
}
static void alarm_gesture_event_cb(lv_event_t *e)
{
    lv_obj_t * obj = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    switch (code)
    {
    case LV_EVENT_GESTURE:
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        if (dir == LV_DIR_RIGHT)
        {
            LV_LOG_USER("闹钟界面右滑退出\n");
            exit_alarm();
            break;
        }
    default:
        break;
    }
}

static void exit_alarm_edit(void)
{
    AlarmView* view = get_alarm_view();
    if (view && view->edit_screen) {
        // lv_obj_add_flag(view->edit_screen, LV_OBJ_FLAG_HIDDEN);
        // //lv_obj_delete(view->edit_screen);
        // view->edit_screen = NULL;
        alarm_view_show_main_screen();
        LV_LOG_USER("edit界面隐藏\n");
    }
}
static void alarm_edit_gesture_event_cb(lv_event_t *e)
{
    lv_obj_t * obj = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    switch (code)
    {
    case LV_EVENT_GESTURE:
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        if (dir == LV_DIR_RIGHT)
        {
            LV_LOG_USER("edit界面右滑退出\n");
            exit_alarm_edit();
            break;
        }
    default:
        break;
    }
}

static void exit_alarm_mode(void)
{
    AlarmView* view = get_alarm_view();
    if (view && view->mode_screen) {
        // lv_obj_add_flag(view->mode_screen, LV_OBJ_FLAG_HIDDEN);
        // //lv_obj_delete(view->mode_screen);
        // view->mode_screen = NULL;
        alarm_view_show_main_screen();
        LV_LOG_USER("mode界面隐藏\n");
    }
}
static void alarm_mode_gesture_event_cb(lv_event_t *e)
{
    lv_obj_t * obj = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    switch (code)
    {
    case LV_EVENT_GESTURE:
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        if (dir == LV_DIR_RIGHT)
        {
            LV_LOG_USER("mode界面右滑退出\n");
            exit_alarm_mode();
            break;
        }
    default:
        break;
    }
}

static void exit_alarm_repeat(void)
{
    AlarmView* view = get_alarm_view();
    if (view && view->repeat_screen) {
        // lv_obj_add_flag(view->repeat_screen, LV_OBJ_FLAG_HIDDEN);
        // //lv_obj_delete(view->repeat_screen);
        // view->repeat_screen = NULL;
        alarm_view_show_mode_screen();        
        LV_LOG_USER("repeat界面隐藏\n");
    }
}
static void alarm_repeat_gesture_event_cb(lv_event_t *e)
{
    lv_obj_t * obj = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    switch (code)
    {
    case LV_EVENT_GESTURE:
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        if (dir == LV_DIR_RIGHT)
        {
            LV_LOG_USER("repeat界面右滑退出\n");
            exit_alarm_repeat();
            break;
        }
    default:
        break;
    }
}
void alarm_view_restore_alarms_display(void)
{
    AlarmModel *model = get_alarm_model();
    LV_LOG_USER("恢复，闹钟数量为：%d", model->alarm_count);

    // 检查是否已经有闹钟UI显示（避免重复重建）
    bool has_alarms_ui = false;
    for (int i = 0; i < MAX_ALARM_COUNT; i++) {
        if (alarm_view.alarm_containers[i] != NULL) {
            has_alarms_ui = true;
            break;
        }
    }

    // 如果已经有闹钟UI，只更新显示状态，不重建
    if (has_alarms_ui) {
        LV_LOG_USER("闹钟UI已存在，仅更新显示状态");
        alarm_view_update_alarm_list();
        return;
    }

    // 首次进入或UI被清除后，创建闹钟UI
    for (int i = 0; i < model->alarm_count; i++) {
        LV_LOG_USER("创建闹钟UI: 索引=%d, 时间=%s", i, model->alarms[i].time);
        alarm_view_create_alarm(model->alarms[i].time, i);
    }

    alarm_view_update_alarm_list();
}
//new code

//页面跳转事件
static void clock_page_switch_to(ScreenType screen_type)
{
    //隐藏所有屏幕
    if (alarm_view.main_screen) {
        lv_obj_add_flag(alarm_view.main_screen, LV_OBJ_FLAG_HIDDEN);
        //lv_obj_delete(alarm_view.main_screen);
    }
    if (alarm_view.edit_screen) {
        lv_obj_add_flag(alarm_view.edit_screen, LV_OBJ_FLAG_HIDDEN);
        //lv_obj_delete(alarm_view.edit_screen);
    }
    if (alarm_view.mode_screen) {
        lv_obj_add_flag(alarm_view.mode_screen, LV_OBJ_FLAG_HIDDEN);
        //lv_obj_delete(alarm_view.mode_screen);
    }
    if (alarm_view.repeat_screen) {
        lv_obj_add_flag(alarm_view.repeat_screen, LV_OBJ_FLAG_HIDDEN);
        //lv_obj_delete(alarm_view.repeat_screen);
    }
    
    //显示指定屏幕
    switch(screen_type) {
        case SCREEN_MAIN:
            if (alarm_view.main_screen) {
                lv_obj_clear_flag(alarm_view.main_screen, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(alarm_view.main_screen);
            }
            break;
        case SCREEN_EDIT:
            if (alarm_view.edit_screen) {
                lv_obj_clear_flag(alarm_view.edit_screen, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(alarm_view.edit_screen);
            }
            break;
        case SCREEN_MODE:
            if (alarm_view.mode_screen) {
                lv_obj_clear_flag(alarm_view.mode_screen, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(alarm_view.mode_screen);
            }
            break;
        case SCREEN_REPEAT:
            if (alarm_view.repeat_screen) {
                lv_obj_clear_flag(alarm_view.repeat_screen, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(alarm_view.repeat_screen);
            }
            break;
    }
    
    lv_refr_now(NULL);
}
//new code

void alarm_view_init() {
    static bool styles_initialized = false;

    LV_LOG_USER("视图初始化开始\n");

    // 如果已经初始化过，先清理旧的UI对象，避免内存泄漏
    if (alarm_view.main_screen != NULL) {
        LV_LOG_USER("清理旧的闹钟界面\n");
        lv_obj_delete(alarm_view.main_screen);
        alarm_view.main_screen = NULL;
    }
    if (alarm_view.edit_screen != NULL) {
        lv_obj_delete(alarm_view.edit_screen);
        alarm_view.edit_screen = NULL;
    }
    if (alarm_view.mode_screen != NULL) {
        lv_obj_delete(alarm_view.mode_screen);
        alarm_view.mode_screen = NULL;
    }
    if (alarm_view.repeat_screen != NULL) {
        lv_obj_delete(alarm_view.repeat_screen);
        alarm_view.repeat_screen = NULL;
    }

    memset(alarm_view.alarm_containers, 0, sizeof(alarm_view.alarm_containers));
    memset(alarm_view.alarm_switches, 0, sizeof(alarm_view.alarm_switches));
    memset(alarm_view.alarm_checkboxes, 0, sizeof(alarm_view.alarm_checkboxes));

    AlarmModel* model = get_alarm_model();

    /* 样式只需初始化一次，避免重复创建导致内存膨胀和耗时 */
    if (!styles_initialized) {
        lv_style_init(&screen_style);
        lv_style_set_bg_color(&screen_style, lv_color_black());

        lv_style_init(&clock_style);
        lv_style_set_text_color(&clock_style, lv_color_white());

        lv_style_init(&nowtime_style);
        lv_style_set_text_font(&nowtime_style, &lv_font_montserrat_20);
        lv_style_set_text_color(&nowtime_style, lv_color_white());

        lv_style_init(&btn_style);
        lv_style_set_radius(&btn_style, 30);

        lv_style_init(&roller_sel_style);
        lv_style_set_text_font(&roller_sel_style, &lv_font_montserrat_32);
        lv_style_set_text_color(&roller_sel_style, lv_color_white());
        lv_style_set_bg_color(&roller_sel_style, lv_color_black());
        lv_style_set_border_color(&roller_sel_style, lv_color_black());

        lv_style_init(&roller_style);
        lv_style_set_text_font(&roller_style, &lv_font_montserrat_24);
        lv_style_set_text_color(&roller_style, lv_color_white());
        lv_style_set_bg_color(&roller_style, lv_color_black());
        lv_style_set_border_color(&roller_style, lv_color_black());

        styles_initialized = true;
    }

    alarm_view.main_screen = lv_obj_create(get_watch_scr());
    lv_obj_set_size(alarm_view.main_screen, 455, 455);
    lv_obj_set_style_bg_color(alarm_view.main_screen, lv_color_black(), 0);
    lv_obj_set_style_radius(alarm_view.main_screen, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(alarm_view.main_screen, 0, 0);
      //lv_obj_t * mask_main = lv_obj_create(alarm_view.main_screen);
      //lv_obj_set_size(mask_main, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
      //lv_obj_set_style_bg_color(mask_main, lv_color_black(), 0);

      // lv_obj_set_size(mask_main,LV_CIRCLE_WATCH,LV_CIRCLE_WATCH);
      // lv_obj_set_style_radius(mask_main,LV_RADIUS_CIRCLE,0);
      // lv_obj_add_style(mask_main, &screen_style, LV_PART_MAIN);
      // lv_obj_align(mask_main,LV_ALIGN_CENTER,0,0);

    //new code
    // 添加手势支持
    lv_obj_add_event_cb(alarm_view.main_screen, alarm_gesture_event_cb, LV_EVENT_GESTURE, alarm_view.main_screen);
    lv_obj_remove_flag(alarm_view.main_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    //new code

    alarm_view.main_time_label = lv_label_create(alarm_view.main_screen);
    //char buffer[8];
    //get_current_time(buffer);
    alarm_model_get_current_time();
    lv_label_set_text(alarm_view.main_time_label, model->current_time);
    lv_obj_align(alarm_view.main_time_label, LV_ALIGN_TOP_MID, 0, -10);
    lv_obj_add_style(alarm_view.main_time_label, &nowtime_style, LV_PART_MAIN);
    //lv_timer_create(update_time_cb, 60000, NULL);

    alarm_view.clock_label = lv_label_create(alarm_view.main_screen);
    lv_obj_set_style_text_font(alarm_view.clock_label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_label_set_text(alarm_view.clock_label, "Clock");
    lv_obj_align(alarm_view.clock_label, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_text_color(alarm_view.clock_label, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(alarm_view.clock_label, 1, 0);

    alarm_view.clock_img = lv_image_create(alarm_view.main_screen);
    lv_image_set_src(alarm_view.clock_img, &clock_icon);
    //lv_image_set_src(alarm_view.clock_img, "");
    lv_obj_align(alarm_view.clock_img, LV_ALIGN_CENTER, 0, -10);
    lv_image_set_scale(alarm_view.clock_img, 32);

    alarm_view.no_clock_label = lv_label_create(alarm_view.main_screen);
    lv_obj_set_style_text_font(alarm_view.no_clock_label, font_manager_get_font(14), LV_PART_MAIN);
    lv_label_set_text(alarm_view.no_clock_label, "暂未设置任何闹钟");
    lv_obj_align(alarm_view.no_clock_label, LV_ALIGN_CENTER, 0, 50);

    alarm_view.alarm_container = lv_obj_create(alarm_view.main_screen);
    lv_obj_set_size(alarm_view.alarm_container, 380, 280);
    lv_obj_align(alarm_view.alarm_container, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_layout(alarm_view.alarm_container,LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(alarm_view.alarm_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(alarm_view.alarm_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_color(alarm_view.alarm_container, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_color(alarm_view.alarm_container, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_pad_row(alarm_view.alarm_container, 12, LV_PART_MAIN);
    lv_obj_set_scroll_dir(alarm_view.alarm_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(alarm_view.alarm_container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(alarm_view.alarm_container, LV_OBJ_FLAG_HIDDEN); 

    alarm_view.btn_addClock = lv_button_create(alarm_view.main_screen);
    lv_obj_add_style(alarm_view.btn_addClock, &btn_style, LV_PART_MAIN);
    lv_obj_align(alarm_view.btn_addClock, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_size(alarm_view.btn_addClock, 100, 40);
    lv_obj_add_event_cb(alarm_view.btn_addClock, goto_edit_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * label_addClock = lv_label_create(alarm_view.btn_addClock);
    lv_label_set_text(label_addClock, "+");
    lv_obj_set_style_text_font(label_addClock, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(label_addClock, LV_ALIGN_CENTER, 0, 0);

    alarm_view.del_sel_btn = lv_button_create(alarm_view.main_screen);
    lv_obj_add_style(alarm_view.del_sel_btn, &btn_style, LV_PART_MAIN);
    lv_obj_set_style_bg_color(alarm_view.del_sel_btn,lv_color_make(36, 32, 35),LV_PART_MAIN);
    lv_obj_align(alarm_view.del_sel_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_size(alarm_view.del_sel_btn, 100, 40);
    lv_obj_add_event_cb(alarm_view.del_sel_btn, del_selected_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_add_flag(alarm_view.del_sel_btn,LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * label_del_sel = lv_label_create(alarm_view.del_sel_btn);
    lv_label_set_text(label_del_sel, LV_SYMBOL_TRASH);
    lv_obj_align(label_del_sel, LV_ALIGN_CENTER, 0, 0);

    alarm_view.edit_screen = lv_obj_create(get_watch_scr());
    lv_obj_set_size(alarm_view.edit_screen, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_set_style_bg_color(alarm_view.edit_screen, lv_color_black(), 0);
    lv_obj_set_style_radius(alarm_view.edit_screen, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(alarm_view.edit_screen, 0, 0);
    // lv_obj_t * mask_edit = lv_obj_create(alarm_view.edit_screen);
    // lv_obj_set_size(mask_edit,LV_CIRCLE_WATCH,LV_CIRCLE_WATCH);
    // lv_obj_set_style_radius(mask_edit,LV_RADIUS_CIRCLE,0);
    // lv_obj_add_style(mask_edit, &screen_style, LV_PART_MAIN);
    // lv_obj_align(mask_edit,LV_ALIGN_CENTER,0,0);

    //new code
    // 添加手势支持
    lv_obj_add_event_cb(alarm_view.edit_screen, alarm_edit_gesture_event_cb, LV_EVENT_GESTURE, alarm_view.edit_screen);
    lv_obj_remove_flag(alarm_view.edit_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    //new code

    alarm_view.edit_time_label = lv_label_create(alarm_view.edit_screen);
    //char buffer2[8];
    //get_current_time(buffer2);
    alarm_model_get_current_time();
    lv_label_set_text(alarm_view.edit_time_label, model->current_time);
    lv_obj_align(alarm_view.edit_time_label, LV_ALIGN_TOP_MID, 0, -10);
    lv_obj_add_style(alarm_view.edit_time_label, &nowtime_style, LV_PART_MAIN);
    //lv_timer_create(update_time_cb, 60000, NULL);

    lv_obj_t * back_main = lv_label_create(alarm_view.edit_screen);
    lv_label_set_text(back_main, "<");
    lv_obj_set_style_text_font(back_main, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_add_flag(back_main, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back_main, back_main_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(back_main, LV_ALIGN_TOP_MID, -60, 20);
    lv_obj_add_style(back_main, &clock_style, LV_PART_MAIN);

    lv_obj_t * edit_title = lv_label_create(alarm_view.edit_screen);
    lv_obj_set_style_text_font(edit_title, font_manager_get_font(20), LV_PART_MAIN);
    lv_label_set_text(edit_title, "时间设置");
    lv_obj_align(edit_title, LV_ALIGN_TOP_MID, 10, 20);
    lv_obj_add_style(edit_title, &clock_style, LV_PART_MAIN);

    const char *hours_str =
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n"
        "10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n"
        "20\n21\n22\n23";    
    const char *minutes_str = 
    "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n"
    "10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n"
    "20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n"
    "30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n"
    "40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n"
    "50\n51\n52\n53\n54\n55\n56\n57\n58\n59";
    
    alarm_view.hour_roller = lv_roller_create(alarm_view.edit_screen);
    lv_roller_set_options(alarm_view.hour_roller, hours_str, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(alarm_view.hour_roller, 5);
    lv_obj_set_width(alarm_view.hour_roller, 150);
    lv_obj_align(alarm_view.hour_roller, LV_ALIGN_CENTER, -80, 0);
    lv_roller_set_selected(alarm_view.hour_roller, 0, LV_ANIM_ON);
    lv_obj_add_style(alarm_view.hour_roller, &roller_style, LV_PART_MAIN);
    lv_obj_add_style(alarm_view.hour_roller, &roller_sel_style, LV_PART_SELECTED);

    lv_obj_t * colon = lv_label_create(alarm_view.edit_screen);
    lv_label_set_text(colon, ":");
    lv_obj_set_style_text_font(colon, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_set_style_text_color(colon, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(colon, LV_ALIGN_CENTER, 0, 0);

    alarm_view.minute_roller = lv_roller_create(alarm_view.edit_screen);
    lv_roller_set_options(alarm_view.minute_roller, minutes_str, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(alarm_view.minute_roller, 5);
    lv_obj_set_width(alarm_view.minute_roller, 150);
    lv_obj_align(alarm_view.minute_roller, LV_ALIGN_CENTER, 80, 0);
    lv_roller_set_selected(alarm_view.minute_roller, 0, LV_ANIM_ON);
    lv_obj_add_style(alarm_view.minute_roller, &roller_style, LV_PART_MAIN);
    lv_obj_add_style(alarm_view.minute_roller, &roller_sel_style, LV_PART_SELECTED);

    lv_obj_t * btn_confirmClock = lv_button_create(alarm_view.edit_screen);
    lv_obj_add_style(btn_confirmClock, &btn_style, LV_PART_MAIN);
    lv_obj_align(btn_confirmClock, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_size(btn_confirmClock, 100, 40);
    lv_obj_add_event_cb(btn_confirmClock, add_clock_cb, LV_EVENT_CLICKED,NULL); 

    lv_obj_t * label_confirmClock = lv_label_create(btn_confirmClock);
    lv_label_set_text(label_confirmClock, LV_SYMBOL_OK);
    lv_obj_align(label_confirmClock, LV_ALIGN_CENTER, 0, 0);

    alarm_view.mode_screen = lv_obj_create(get_watch_scr());
    lv_obj_set_size(alarm_view.mode_screen, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_set_style_bg_color(alarm_view.mode_screen, lv_color_black(), 0);
    lv_obj_set_style_radius(alarm_view.mode_screen, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(alarm_view.mode_screen, 0, 0);

    // lv_obj_t * mask_mode = lv_obj_create(alarm_view.mode_screen);
    // lv_obj_set_size(mask_mode,LV_CIRCLE_WATCH,LV_CIRCLE_WATCH);
    // lv_obj_set_style_radius(mask_mode,LV_RADIUS_CIRCLE,0);
    // lv_obj_add_style(mask_mode, &screen_style, LV_PART_MAIN);
    // lv_obj_align(mask_mode,LV_ALIGN_CENTER,0,0);

    //new code
    // 添加手势支持
    lv_obj_add_event_cb(alarm_view.mode_screen, alarm_mode_gesture_event_cb, LV_EVENT_GESTURE, alarm_view.mode_screen);
    lv_obj_remove_flag(alarm_view.mode_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    //new code

    alarm_view.mode_time_label = lv_label_create(alarm_view.mode_screen);
    //char buffer3[8];
    //get_current_time(buffer3);
    alarm_model_get_current_time();
    lv_label_set_text(alarm_view.mode_time_label, model->current_time);
    lv_obj_align(alarm_view.mode_time_label, LV_ALIGN_TOP_MID, 0, -10);
    lv_obj_add_style(alarm_view.mode_time_label, &nowtime_style, LV_PART_MAIN);
    //lv_timer_create(update_time_cb, 60000, NULL);

    lv_obj_t * back_main2 = lv_label_create(alarm_view.mode_screen);
    lv_label_set_text(back_main2, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_main2, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_add_flag(back_main2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back_main2, back_main_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(back_main2, LV_ALIGN_TOP_MID, -60, 20);
    lv_obj_add_style(back_main2, &clock_style, LV_PART_MAIN);

    lv_obj_t * edit_title2 = lv_label_create(alarm_view.mode_screen);
    lv_obj_set_style_text_font(edit_title2, font_manager_get_font(20), LV_PART_MAIN);
    lv_label_set_text(edit_title2, "编辑闹钟");
    lv_obj_align(edit_title2, LV_ALIGN_TOP_MID, 10, 20);
    lv_obj_add_style(edit_title2, &clock_style, LV_PART_MAIN);

    alarm_view.item_container = lv_obj_create(alarm_view.mode_screen);
    lv_obj_set_size(alarm_view.item_container, 380, 280);
    lv_obj_align(alarm_view.item_container, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_flex_flow(alarm_view.item_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(alarm_view.item_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(alarm_view.item_container, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_color(alarm_view.item_container, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_pad_row(alarm_view.item_container,5,LV_PART_MAIN);
    lv_obj_set_scroll_dir(alarm_view.item_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(alarm_view.item_container, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t * item_content = lv_obj_create(alarm_view.item_container);
    lv_obj_set_size(item_content, lv_pct(100), 60);
    lv_obj_set_style_bg_color(item_content, lv_color_make(36, 32, 35), LV_PART_MAIN);
    lv_obj_set_style_border_width(item_content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(item_content, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(item_content, 20, LV_PART_MAIN);

    alarm_view.time_item = lv_label_create(item_content);
    lv_obj_set_style_text_font(alarm_view.time_item, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_set_style_text_color(alarm_view.time_item, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(alarm_view.time_item, LV_ALIGN_LEFT_MID, 20, -10);

    lv_obj_t * time_change = lv_label_create(item_content);
    lv_obj_set_style_text_font(time_change, font_manager_get_font(14), LV_PART_MAIN);
    lv_obj_set_style_text_color(time_change, lv_color_make(85, 70, 78), LV_PART_MAIN);
    lv_label_set_text(time_change, "更改时间");
    lv_obj_align(time_change, LV_ALIGN_LEFT_MID, 25, 13);

    lv_obj_t * goto_modeEdit = lv_label_create(item_content);
    lv_label_set_text(goto_modeEdit,LV_SYMBOL_RIGHT);
    lv_obj_align(goto_modeEdit, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_text_color(goto_modeEdit,lv_color_make(85, 70, 78),LV_PART_MAIN);

    lv_obj_t * item_content2 = lv_obj_create(alarm_view.item_container);
    lv_obj_set_size(item_content2, lv_pct(100), 60);
    lv_obj_set_style_bg_color(item_content2, lv_color_make(36, 32, 35), LV_PART_MAIN);
    lv_obj_set_style_border_width(item_content2, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(item_content2, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(item_content2, 20, LV_PART_MAIN);
    lv_obj_add_event_cb(item_content2,goto_repeat_cb,LV_EVENT_CLICKED,LV_PART_MAIN);

    lv_obj_t * alarm_mode = lv_label_create(item_content2);
    lv_label_set_text(alarm_mode,"每天");
    lv_obj_set_style_text_font(alarm_mode, font_manager_get_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(alarm_mode, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(alarm_mode, LV_ALIGN_LEFT_MID, 20, -10);

    lv_obj_t * repeat_change = lv_label_create(item_content2);
    lv_obj_set_style_text_font(repeat_change, font_manager_get_font(14), LV_PART_MAIN);
    lv_obj_set_style_text_color(repeat_change, lv_color_make(85, 70, 78), LV_PART_MAIN);
    lv_label_set_text(repeat_change, "设置重复");
    lv_obj_align(repeat_change, LV_ALIGN_LEFT_MID, 25, 13);

    goto_modeEdit = lv_label_create(item_content2);
    lv_label_set_text(goto_modeEdit,LV_SYMBOL_RIGHT);
    lv_obj_align(goto_modeEdit, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_text_color(goto_modeEdit,lv_color_make(85, 70, 78),LV_PART_MAIN);

    lv_obj_t * item_content3 = lv_obj_create(alarm_view.item_container);
    lv_obj_set_size(item_content3, lv_pct(100), 60);
    lv_obj_set_style_bg_color(item_content3, lv_color_make(36, 32, 35), LV_PART_MAIN);
    lv_obj_set_style_border_width(item_content3, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(item_content3, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(item_content3, 20, LV_PART_MAIN);

    lv_obj_t * vibratio_label = lv_label_create(item_content3);
    lv_label_set_text(vibratio_label,"仅振动提醒");
    lv_obj_set_style_text_font(vibratio_label, font_manager_get_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(vibratio_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(vibratio_label, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t * vibration_sw = lv_switch_create(item_content3);
    lv_obj_align(vibration_sw, LV_ALIGN_RIGHT_MID, -10, 0);

    lv_obj_t * item_content4 = lv_obj_create(alarm_view.item_container);
    lv_obj_set_size(item_content4, lv_pct(100), 60);
    lv_obj_set_style_bg_color(item_content4, lv_color_make(36, 32, 35), LV_PART_MAIN);
    lv_obj_set_style_border_width(item_content4, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(item_content4, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(item_content4, 20, LV_PART_MAIN);

    lv_obj_t * intel_label = lv_label_create(item_content4);
    lv_label_set_text(intel_label,"智能唤醒");
    lv_obj_set_style_text_font(intel_label, font_manager_get_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(intel_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(intel_label, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t * intel_sw = lv_switch_create(item_content4);
    lv_obj_align(intel_sw, LV_ALIGN_RIGHT_MID, -10, 0);

    alarm_view.repeat_screen = lv_obj_create(get_watch_scr());
    lv_obj_set_size(alarm_view.repeat_screen, LV_CIRCLE_WATCH, LV_CIRCLE_WATCH);
    lv_obj_set_style_bg_color(alarm_view.repeat_screen, lv_color_black(), 0);
    lv_obj_set_style_radius(alarm_view.repeat_screen, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(alarm_view.repeat_screen, 0, 0);

    // lv_obj_t * mask_repeat = lv_obj_create(alarm_view.repeat_screen);
    // lv_obj_set_size(mask_repeat,LV_CIRCLE_WATCH,LV_CIRCLE_WATCH);
    // lv_obj_set_style_radius(mask_repeat,LV_RADIUS_CIRCLE,0);
    // lv_obj_add_style(mask_repeat, &screen_style, LV_PART_MAIN);
    // lv_obj_align(mask_repeat,LV_ALIGN_CENTER,0,0);

    //new code
    // 添加手势支持
    lv_obj_add_event_cb(alarm_view.repeat_screen, alarm_repeat_gesture_event_cb, LV_EVENT_GESTURE, alarm_view.repeat_screen);
    lv_obj_remove_flag(alarm_view.repeat_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    //new code

    alarm_view.repeat_time_label = lv_label_create(alarm_view.repeat_screen);
    //char buffer4[8];
    //get_current_time(buffer4);
    alarm_model_get_current_time();
    lv_label_set_text(alarm_view.repeat_time_label, model->current_time);
    lv_obj_align(alarm_view.repeat_time_label, LV_ALIGN_TOP_MID, 0, -10);
    lv_obj_add_style(alarm_view.repeat_time_label, &nowtime_style, LV_PART_MAIN);
    //lv_timer_create(update_time_cb, 60000, NULL);

    lv_obj_t * back_main3 = lv_label_create(alarm_view.repeat_screen);
    lv_label_set_text(back_main3, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_main3, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_add_flag(back_main3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back_main3, back_mode_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(back_main3, LV_ALIGN_TOP_MID, -60, 20);
    lv_obj_add_style(back_main3, &clock_style, LV_PART_MAIN);

    lv_obj_t * edit_title3 = lv_label_create(alarm_view.repeat_screen);
    lv_obj_set_style_text_font(edit_title3, font_manager_get_font(20), LV_PART_MAIN);
    lv_label_set_text(edit_title3, "设置重复");
    lv_obj_align(edit_title3, LV_ALIGN_TOP_MID, 10, 20);
    lv_obj_add_style(edit_title3, &clock_style, LV_PART_MAIN);

    lv_obj_t * repeat_day = lv_obj_create(alarm_view.repeat_screen);
    lv_obj_set_size(repeat_day, 380, 280);
    lv_obj_align(repeat_day, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_flex_flow(repeat_day, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(repeat_day, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(repeat_day, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_color(repeat_day, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_pad_row(repeat_day,5,LV_PART_MAIN);
    lv_obj_set_scroll_dir(repeat_day, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(repeat_day, LV_SCROLLBAR_MODE_OFF);
    char day[8][8]={"每天","周一","周二","周三","周四","周五","周六","周日"};
    for(int i=0;i<8;i++){
        lv_obj_t * day_container = lv_obj_create(repeat_day);
        lv_obj_set_size(day_container,lv_pct(100),60);
        lv_obj_set_style_bg_color(day_container, lv_color_make(36, 32, 35), LV_PART_MAIN);
        lv_obj_set_style_border_width(day_container, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(day_container, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(day_container, 20, LV_PART_MAIN);
        lv_obj_t * day_label = lv_label_create(day_container);
        lv_obj_set_style_text_font(day_label, font_manager_get_font(16), LV_PART_MAIN);
        lv_label_set_text(day_label,day[i]);
        lv_obj_set_style_text_color(day_label,lv_color_white(),LV_PART_MAIN);
        lv_obj_align(day_label,LV_ALIGN_LEFT_MID,10,0);
        lv_obj_t * repeat_checkbox = lv_checkbox_create(day_container);
        lv_obj_align(repeat_checkbox, LV_ALIGN_RIGHT_MID, -10, 0);
        lv_obj_set_size(repeat_checkbox,20,20);
        lv_obj_set_style_radius(repeat_checkbox,LV_RADIUS_CIRCLE,LV_PART_INDICATOR);
        lv_obj_set_style_border_width(repeat_checkbox,0,LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(repeat_checkbox,lv_color_make(85, 70, 78),LV_PART_INDICATOR);
        if(i==0){
            lv_obj_add_state(repeat_checkbox,LV_STATE_CHECKED);
        }
    }
    lv_obj_t * repeat_check = lv_button_create(alarm_view.repeat_screen);
    lv_obj_add_style(repeat_check,&btn_style,LV_PART_MAIN);
    lv_obj_align(repeat_check, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_size(repeat_check, 100, 40);
    
    lv_obj_t * label_confirmRepeat = lv_label_create(repeat_check);
    lv_label_set_text(label_confirmRepeat, LV_SYMBOL_OK);
    lv_obj_align(label_confirmRepeat, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(repeat_check,back_main_cb,LV_EVENT_CLICKED,NULL);
    LV_LOG_USER("视图初始化结束\n");
}

void alarm_view_update_time(const char* time_str) {
    if (alarm_view.main_time_label)
        lv_label_set_text(alarm_view.main_time_label, time_str);
    if (alarm_view.edit_time_label)
        lv_label_set_text(alarm_view.edit_time_label, time_str);
    if (alarm_view.mode_time_label)
        lv_label_set_text(alarm_view.mode_time_label, time_str);
    if (alarm_view.repeat_time_label)
        lv_label_set_text(alarm_view.repeat_time_label, time_str);
}

void alarm_view_update_alarm_list() {
    LV_LOG_USER("进入alarm_view_update_alarm_list\n");
    /* 界面已清理时直接返回，避免访问悬空指针 */
    if (!alarm_view.main_screen) return;
    AlarmModel* model = get_alarm_model();
    LV_LOG_USER("model->alarm_count:%d\n", model->alarm_count);
    if (model->alarm_count == 0) {
        lv_obj_remove_flag(alarm_view.clock_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(alarm_view.no_clock_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(alarm_view.alarm_container, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(alarm_view.clock_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(alarm_view.no_clock_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(alarm_view.alarm_container, LV_OBJ_FLAG_HIDDEN);
        LV_LOG_USER("操作成功\n");
    }
    
    if (model->long_pressed) {
        for (int i = 0; i < MAX_ALARM_COUNT; i++) {
            if(alarm_view.alarm_containers[i] != NULL){
                if(alarm_view.alarm_switches[i] != NULL)
                    lv_obj_add_flag(alarm_view.alarm_switches[i], LV_OBJ_FLAG_HIDDEN);
                if(alarm_view.alarm_checkboxes[i] != NULL)
                    lv_obj_remove_flag(alarm_view.alarm_checkboxes[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        lv_obj_add_flag(alarm_view.btn_addClock, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(alarm_view.del_sel_btn, LV_OBJ_FLAG_HIDDEN);
        
        if (model->del_alarm_count == 0) {
            lv_obj_remove_flag(alarm_view.del_sel_btn, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_color(alarm_view.del_sel_btn, lv_color_make(36, 32, 35), LV_PART_MAIN);
        } else {
            lv_obj_add_flag(alarm_view.del_sel_btn, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_color(alarm_view.del_sel_btn, lv_color_make(32, 149, 246), LV_PART_MAIN);
        }
        
        lv_obj_set_style_text_font(alarm_view.clock_label, font_manager_get_font(20), LV_PART_MAIN);
        lv_label_set_text_fmt(alarm_view.clock_label, "已选%d项", model->del_alarm_count);
    } else {
        lv_obj_add_flag(alarm_view.del_sel_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(alarm_view.btn_addClock, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(alarm_view.clock_label, &lv_font_montserrat_32, LV_PART_MAIN);
        lv_label_set_text(alarm_view.clock_label, "Clock");
        
        for (int i = 0; i < MAX_ALARM_COUNT; i++) {
            if(alarm_view.alarm_containers[i] != NULL){
                if(alarm_view.alarm_checkboxes[i] != NULL)
                    lv_obj_add_flag(alarm_view.alarm_checkboxes[i], LV_OBJ_FLAG_HIDDEN);
                if(alarm_view.alarm_switches[i] != NULL)
                    lv_obj_remove_flag(alarm_view.alarm_switches[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        LV_LOG_USER("操作2成功\n");
    }
    // 强制刷新
    lv_obj_invalidate(alarm_view.alarm_container);
}

void alarm_view_show_main_screen() {
    LV_LOG_USER("显示主屏幕\n");

    AlarmView* view = get_alarm_view();

    /* exit_alarm() 会把 main_screen 置 NULL 但不删除 LVGL 对象（orphan）。
     * 重新进入闹钟时 alarm_presenter_init 的 static guard 跳过 alarm_view_init，
     * 导致 main_screen 仍为 NULL → 黑屏。此处检测并重建。 */
    if (view && view->main_screen == NULL) {
        LV_LOG_USER("main_screen 为 NULL，重新初始化视图\n");
        alarm_view_init();
        alarm_view_restore_alarms_display();
    }

    if (view && view->main_screen) {
        // 确保界面可见
        lv_obj_clear_flag(view->main_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(view->main_screen);

        // 刷新闹钟列表显示（不再重建，避免异步删除与同步创建冲突）
        alarm_view_update_alarm_list();

        // 强制刷新
        lv_obj_invalidate(view->main_screen);
        lv_refr_now(NULL);
    }

    clock_page_switch_to(SCREEN_MAIN);
    LV_LOG_USER("显示主屏幕结束\n");
}

/* 仅切换显示并刷新列表显示，不删除/重建已有卡片 —— 新增闹钟后用，避免重建卡死 */
void alarm_view_show_main_screen_quick() {
    AlarmView* view = get_alarm_view();
    if (view && view->main_screen) {
        lv_obj_clear_flag(view->main_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(view->main_screen);
        alarm_view_update_alarm_list();
        lv_obj_invalidate(view->main_screen);
        lv_refr_now(NULL);
    }
    clock_page_switch_to(SCREEN_MAIN);
}

void alarm_view_show_edit_screen() {
    LV_LOG_USER("显示edit屏幕\n");
    
    clock_page_switch_to(SCREEN_EDIT);
    //lv_screen_load(alarm_view.edit_screen);
    LV_LOG_USER("显示edit屏幕结束\n");
}

void alarm_view_show_mode_screen() {
    LV_LOG_USER("显示mode屏幕\n");
    clock_page_switch_to(SCREEN_MODE);
    //lv_screen_load(alarm_view.mode_screen);
    LV_LOG_USER("显示mode屏幕结束\n");
}

void alarm_view_show_repeat_screen() {
    LV_LOG_USER("显示repeat屏幕\n");
    clock_page_switch_to(SCREEN_REPEAT);
    //lv_screen_load(alarm_view.repeat_screen);
    LV_LOG_USER("显示repeat屏幕结束\n");
}

AlarmView* get_alarm_view() {
    return &alarm_view;
}