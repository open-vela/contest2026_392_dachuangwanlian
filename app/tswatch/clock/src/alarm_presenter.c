#include <stdio.h>
#include <stdlib.h>
#include "../include/alarm_presenter.h"
#include "../include/alarm_model.h"
#include "../include/alarm_view.h"

static void update_time_cb(lv_timer_t* timer) {
    alarm_presenter_update_time();
}

void alarm_presenter_init() {
    static bool initialized = false;

    // 如果已经初始化过，只刷新显示，不重新创建UI
    if (initialized) {
        LV_LOG_USER("闹钟已初始化，仅刷新显示\n");
        alarm_view_show_main_screen();
        return;
    }

    AlarmModel *model = get_alarm_model();
    alarm_model_init();
    LV_LOG_USER("1");
    alarm_view_init();

    /* 预置 3 个闹钟（仅首次为空时）：07:00 周一至周五 / 12:30 午休·每天 / 22:00 睡觉·仅一次 */
    if (model->alarm_count == 0) {
        alarm_model_add_alarm("07:00");
        alarm_model_add_alarm("12:30");
        alarm_model_add_alarm("22:00");
    }
    alarm_view_restore_alarms_display();
    LV_LOG_USER("2");
    // 创建定时器
    lv_timer_create(update_time_cb, 60000, NULL);

    alarm_model_get_current_time();//newnew code
    alarm_presenter_update_time();//new code

    // 显示主屏幕
    alarm_view_show_main_screen();

    // while (1) {
    //     lv_timer_handler();
    //     usleep(1000);
    // }
    initialized = true;
    LV_LOG_USER("结束alarm_presenter_init\n");
}

void alarm_presenter_update_time() {
    alarm_model_get_current_time();
    alarm_view_update_time(get_alarm_model()->current_time);
}

void alarm_presenter_add_alarm(const char * hour, const char* minute) {
    alarm_presenter_add_alarm_with_mode(hour, minute, "每天");
}

void alarm_presenter_add_alarm_with_mode(const char * hour, const char* minute, const char* mode) {
    LV_LOG_USER("进入alarm_presenter_add_alarm_with_mode\n");
    if (!hour || !minute) {
        LV_LOG_USER("错误:hour或minute为NULL\n");
        return;
    }
    /* 将 hour/minute 规范化为2位数字，补前导零 */
    int h = atoi(hour);
    int m = atoi(minute);
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", h, m);
    LV_LOG_USER("hour:%s,minute:%s,mode:%s\n", hour, minute, mode ? mode : "每天");
    LV_LOG_USER("model添加时间:%s\n", time_str);
    bool success = alarm_model_add_alarm_with_mode(time_str, mode);
    LV_LOG_USER("model添加结果:%d\n", success);
    if (success == true) {
        LV_LOG_USER("model添加成功\n");
        AlarmModel* model = get_alarm_model();
        alarm_view_create_alarm(time_str, model->alarm_count -1);
        alarm_view_update_alarm_list();
        /* 用轻量返回（不重建卡片），避免在事件回调中删除对象导致卡死 */
        alarm_view_show_main_screen_quick();
    }
}

int alarm_presenter_remove_alarm_by_time(const char* hour, const char* minute) {
    if (!hour || !minute) return -1;
    int h = atoi(hour);
    int m = atoi(minute);
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", h, m);
    LV_LOG_USER("删除闹钟: %s\n", time_str);

    /* 先打开clock界面 */
    clock_main_start();

    int index = alarm_model_find_alarm(time_str);
    if (index < 0) {
        LV_LOG_USER("未找到闹钟: %s\n", time_str);
        return -1;
    }
    LV_LOG_USER("找到闹钟 index=%d, 删除中\n", index);
    alarm_presenter_remove_alarm(index);
    return index;
}

void alarm_presenter_remove_alarm(int index) {
    if (alarm_model_remove_alarm(index)) {
        alarm_view_remove_alarm(index); 
        alarm_view_update_alarm_list(); 
    }
}

void alarm_presenter_remove_selected_alarms() {
    alarm_model_remove_selected_alarms(); 
    alarm_view_remove_selected_alarms(); 
    alarm_view_update_alarm_list(); 
    alarm_presenter_set_long_pressed(false);
}

void alarm_presenter_select_alarm(int index, bool selected) {
    alarm_model_select_alarm(index, selected);
    alarm_view_update_alarm_list();
}

void alarm_presenter_set_long_pressed(bool long_pressed) {
    get_alarm_model()->long_pressed = long_pressed;
    alarm_view_update_alarm_list();
}