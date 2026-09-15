#ifndef FONT_MANAGER_H
#define FONT_MANAGER_H

#include "lvgl.h"

// 字体类型定义
typedef enum {
    FONT_SMALL = 0,     // 16px
    FONT_MEDIUM,        // 20px  
    FONT_LARGE,         // 24px
    FONT_MAX
} font_size_t;

// 字体声明
LV_FONT_DECLARE(lv_font_simsun_16_common);
LV_FONT_DECLARE(lv_font_simsun_20_common);
LV_FONT_DECLARE(lv_font_simsun_24_common);
LV_FONT_DECLARE(lv_font_chinese_ali_bold_14);
LV_FONT_DECLARE(lv_font_chinese_ali_bold_16);
LV_FONT_DECLARE(lv_font_chinese_ali_bold_20);

// 字体管理函数
void font_manager_init(void);
lv_font_t* font_manager_get_font(font_size_t size);

#endif /* FONT_MANAGER_H */