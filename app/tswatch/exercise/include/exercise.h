#ifndef EXERCISE_H
#define EXERCISE_H

#include <lvgl/lvgl.h>

/* 启动运动应用 UI（在当前 watch_scr 上创建） */
void exercise_start(void);

/* 跳转到运动应用并直接开始运动（跳过准备页倒计时） */
void exercise_start_running(void);

/* 停止当前运动，回到准备页 */
void exercise_stop(void);

#endif /* EXERCISE_H */
