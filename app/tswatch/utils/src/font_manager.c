#include "../include/font_manager.h"

static lv_font_t* fonts[FONT_MAX] = {NULL};

// 初始化字体指针
void font_manager_init(void)
{
    fonts[FONT_SMALL] = &lv_font_chinese_ali_bold_14;
    fonts[FONT_MEDIUM] = &lv_font_chinese_ali_bold_16;
    fonts[FONT_LARGE] = &lv_font_chinese_ali_bold_20;
}

lv_font_t* font_manager_get_font(font_size_t size)
{
    if (size >= FONT_MAX) {
        return fonts[FONT_MEDIUM];
    }
    return fonts[size];
}