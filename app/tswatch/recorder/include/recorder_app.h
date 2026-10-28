#ifndef RECORDER_APP_H
#define RECORDER_APP_H

#include <lvgl/lvgl.h>

/**
 * @brief 启动录音 App
 *
 * 创建录音界面：标题 + 状态文字 + 声音振幅条(录音时随麦克风能量律动) +
 * 两个按钮「开始录音」「停止录音」。右滑退出。录音经蓝牙 SPP 实时发到手机端。
 */
void recorder_start(void);

/**
 * @brief 打开录音 App 并立即开始录音
 *
 * 供外部（如小智 MCP 语音指令）调用：若录音界面尚未创建则先 recorder_start()
 * 建好 UI，再进入录音中态并启动 SPP 推流 / 离线落盘。与用户在界面点「开始录音」
 * 效果一致。在 LVGL 线程调用。
 */
void recorder_start_recording(void);

#endif /* RECORDER_APP_H */
