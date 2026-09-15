#ifndef __LIST_H
#define __LIST_H

#include "lvgl.h"
#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>

#include "string.h"
#include "../../utils/include/font_manager.h"

// 尺寸配置
#define LAUNCHER_SIZE 455     // 圆形表盘尺寸
#define RADIUS        (LAUNCHER_SIZE / 2)
#define APP_SIZE      80      // 应用项大小
#define APP_GAP       20      // 应用间距
#define TOTAL_APP     15      // 应用总数（与 app_list[] 实际条目一致，越界会读到 NULL sym/name 致 lv_label_set_text 崩溃）
#define ROWS          ((TOTAL_APP + 1) / 2)  // 行数

// 函数声明
/* 卡片式应用列表（新 API）*/
void    app_launcher_create(lv_obj_t *parent);
void    app_launcher_delete(void);
void    app_launcher_next(void);
void    app_launcher_prev(void);
void    app_launcher_enter(void);
int8_t  app_launcher_get_selected(void);
lv_group_t *app_launcher_get_group(void);

/* 旧 API 适配：供 watch_start.c 页面栈调用，内部委托给 app_launcher_* */
lv_obj_t *applist_create_list(lv_obj_t *scr);
void applist_hide_page(void);
void applist_destroy(lv_obj_t *launcher);
void applist_show(void);
void applist_hide(void);
void cleanup_resources(void);

#endif 