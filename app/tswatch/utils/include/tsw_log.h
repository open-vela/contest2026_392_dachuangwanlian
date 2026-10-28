/**
 * @file tsw_log.h
 * @brief tswatch 统一日志打印模块
 *
 * 背景: 工程 defconfig 中 CONFIG_LV_LOG_LEVEL_NONE=y, 导致 LV_LOG_USER 等
 *       LVGL 日志宏实际不会输出。本模块基于 printf 直通串口控制台
 *       (log.txt 中可见, 如 "[watch_start] Power button pressed!"),
 *       与系统日志风格一致, 供 tswatch 各模块直接调用。
 *
 * 用法:
 *   1. 在模块 .c 文件顶部 include 本头文件前定义标签:
 *        #define TSW_LOG_TAG "main_page"
 *        #include "tsw_log.h"
 *      未定义时默认标签为 "TSW"。
 *
 *   2. 使用日志宏 (printf 风格):
 *        TSW_LOGE("open failed: %d", errno);   // 错误
 *        TSW_LOGW("retry %d", n);              // 警告
 *        TSW_LOGI("started, page=%s", name);   // 信息
 *        TSW_LOGD("x=%d y=%d", x, y);          // 调试(默认不输出)
 *
 *   3. 十六进制转储 (调试图片/协议数据):
 *        TSW_LOG_HEX("recv", buf, len);
 *
 *   4. 运行时调整级别:
 *        tsw_log_set_level(TSW_LOG_LEVEL_DEBUG);
 *        tsw_log_set_level(TSW_LOG_LEVEL_ERROR);
 *
 *   5. 编译期裁剪 (进一步省 flash/栈): 在 Makefile 中加
 *        CFLAGS += -DTSW_LOG_COMPILE_LEVEL=1   // 只保留 ERROR
 *
 * 输出格式 (对齐系统日志 "[ 1191.068500] [ 7] [ap] msg"):
 *   [  119.068][I][main_page] main_page.c:88: started, page=home
 *   [  119.071][E][xiaozhi ] xiaozhi_ws.c:120: connect failed: -1
 *   [  119.080][H][recv    ] 0000: 89 50 4e 47 0d 0a 1a 0a ...
 */

#ifndef __APPS_EXAMPLES_TSWATCH_UTILS_TSW_LOG_H
#define __APPS_EXAMPLES_TSWATCH_UTILS_TSW_LOG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 日志级别定义 ---------------- */

#define TSW_LOG_LEVEL_NONE  0  /* 关闭所有日志 */
#define TSW_LOG_LEVEL_ERROR 1  /* 仅错误 */
#define TSW_LOG_LEVEL_WARN  2  /* 错误 + 警告 */
#define TSW_LOG_LEVEL_INFO  3  /* 错误 + 警告 + 信息 (推荐默认) */
#define TSW_LOG_LEVEL_DEBUG 4  /* 全部 */

#ifndef TSW_LOG_COMPILE_LEVEL
#define TSW_LOG_COMPILE_LEVEL TSW_LOG_LEVEL_INFO  /* 编译期最高保留级别 */
#endif

/* 模块标签: 在 include 本头文件之前 #define TSW_LOG_TAG "xxx" 覆盖 */
#ifndef TSW_LOG_TAG
#define TSW_LOG_TAG "TSW"
#endif

/* ---------------- API ---------------- */

/* 运行时获取/设置日志级别 (编译期已裁剪的级别无法再打开) */
int  tsw_log_get_level(void);
void tsw_log_set_level(int level);

/* 日志实现函数 (一般不直接调用, 请使用下方宏) */
void tsw_log_write(int level, const char *tag, const char *file,
                   int line, const char *fmt, ...);

/* 十六进制转储: 每行 16 字节, 附带 ASCII */
void tsw_log_hexdump(const char *tag, const void *data, unsigned int len);

/* ---------------- 日志宏 ---------------- */

#if TSW_LOG_COMPILE_LEVEL >= TSW_LOG_LEVEL_ERROR
#define TSW_LOGE(...) \
    tsw_log_write(TSW_LOG_LEVEL_ERROR, TSW_LOG_TAG, __FILE__, __LINE__, __VA_ARGS__)
#else
#define TSW_LOGE(...) do { } while (0)
#endif

#if TSW_LOG_COMPILE_LEVEL >= TSW_LOG_LEVEL_WARN
#define TSW_LOGW(...) \
    tsw_log_write(TSW_LOG_LEVEL_WARN, TSW_LOG_TAG, __FILE__, __LINE__, __VA_ARGS__)
#else
#define TSW_LOGW(...) do { } while (0)
#endif

#if TSW_LOG_COMPILE_LEVEL >= TSW_LOG_LEVEL_INFO
#define TSW_LOGI(...) \
    tsw_log_write(TSW_LOG_LEVEL_INFO, TSW_LOG_TAG, __FILE__, __LINE__, __VA_ARGS__)
#else
#define TSW_LOGI(...) do { } while (0)
#endif

#if TSW_LOG_COMPILE_LEVEL >= TSW_LOG_LEVEL_DEBUG
#define TSW_LOGD(...) \
    tsw_log_write(TSW_LOG_LEVEL_DEBUG, TSW_LOG_TAG, __FILE__, __LINE__, __VA_ARGS__)
#else
#define TSW_LOGD(...) do { } while (0)
#endif

/* 十六进制转储宏: 受 DEBUG 级别控制 (运行时 level >= DEBUG 才输出) */
#define TSW_LOG_HEX(tag_, data_, len_) \
    do { \
        if (tsw_log_get_level() >= TSW_LOG_LEVEL_DEBUG) { \
            tsw_log_hexdump(tag_, (const void *)(data_), (unsigned int)(len_)); \
        } \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* __APPS_EXAMPLES_TSWATCH_UTILS_TSW_LOG_H */
