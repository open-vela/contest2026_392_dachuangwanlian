#ifndef SETTING_H
#define SETTING_H

#include <lvgl/lvgl.h>
#include <lvgl/demos/lv_demos.h>
#include <uv.h>

void settings_app_start(void);
void create_back_button(lv_obj_t* parent);
void back_to_main_event(lv_event_t *e);
void setting_gesture_event_cb(lv_event_t *e);
void sub_page_gesture_cb(lv_event_t *e);
#endif