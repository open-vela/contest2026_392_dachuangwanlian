#ifndef ALARM_VIEW_H
#define ALARM_VIEW_H

#include "lvgl/lvgl.h"
#include "lvgl/src/drivers/sdl/lv_sdl_window.h"
#include <unistd.h>
#include <stdio.h>

#define MAX_ALARM_COUNT 10
// 视图组件
typedef struct {
    lv_obj_t *main_time_label;
    lv_obj_t *edit_time_label;
    lv_obj_t *mode_time_label;
    lv_obj_t *repeat_time_label;
    lv_obj_t* main_screen;
    lv_obj_t* edit_screen;
    lv_obj_t* mode_screen;
    lv_obj_t* repeat_screen;
    lv_obj_t* alarm_container;
    lv_obj_t* item_container;
    lv_obj_t* clock_img;
    lv_obj_t* no_clock_label;
    lv_obj_t* hour_roller;
    lv_obj_t* minute_roller;
    lv_obj_t* del_sel_btn;
    lv_obj_t* btn_addClock;
    lv_obj_t* clock_label;
    //lv_obj_t* alarm_mode;
    lv_obj_t* time_item;
    
    // 闹钟项UI组件数组
    lv_obj_t* alarm_containers[10];
    lv_obj_t* alarm_switches[10];
    lv_obj_t* alarm_checkboxes[10];
} AlarmView;

typedef enum{
    SCREEN_MAIN,
    SCREEN_EDIT,
    SCREEN_MODE,
    SCREEN_REPEAT
}ScreenType;

void alarm_view_init();
void alarm_view_update_time(const char* time_str);
void alarm_view_update_alarm_list();
void alarm_view_create_alarm(char* time_str, int index);
void alarm_view_remove_alarm(int index); 
void alarm_view_remove_selected_alarms();
void alarm_view_show_main_screen();
void alarm_view_show_main_screen_quick();  /* 仅切换显示+刷新列表，不重建卡片（用于新增闹钟后返回） */
void alarm_view_show_edit_screen();
void alarm_view_show_mode_screen();
void alarm_view_show_repeat_screen();
AlarmView* get_alarm_view();

#endif