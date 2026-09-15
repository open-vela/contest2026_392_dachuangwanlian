#ifndef CIRCLE_SCREEN_H
#define CIRCLE_SCREEN_H

#include <lvgl/lvgl.h>

#define LV_CIRCLE_WATCH 455

lv_obj_t* get_watch_scr(void);
void destroy_watch_scr(void);
bool is_watch_scr_created(void);

#endif /* CIRCLE_SCREEN_H */