#ifndef XIAOZHI_VOICE_H
#define XIAOZHI_VOICE_H

#include <lvgl/lvgl.h>

/**
 * @brief 启动小智语音应用
 *
 * 创建并显示小智语音对话界面，用户可以点击Start按钮开始语音对话
 */
void xiaozhi_voice_start(void);

/**
 * @brief 删除小智语音应用界面
 *
 * 隐藏并删除小智语音应用的所有UI元素
 */
void xiaozhi_voice_delete(void);

#endif /* XIAOZHI_VOICE_H */