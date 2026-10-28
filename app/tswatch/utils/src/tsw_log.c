/**
 * @file tsw_log.c
 * @brief tswatch 统一日志打印模块实现 (接口见 tsw_log.h)
 *
 * 实现说明:
 *  - 输出走 printf → 串口控制台 (与 log.txt 中 "[watch_start] ..." 一致),
 *    不依赖 CONFIG_LV_LOG_LEVEL 配置。
 *  - 消息先 vsnprintf 到栈缓冲再一次性 printf, 避免多线程行交错。
 *  - 时间戳取 CLOCK_MONOTONIC 秒 (对齐系统日志 "[ 1191.068500]"),
 *    获取失败时打印 [   0.000]。
 *  - flockfile/funlockfile 尽量保证整行原子输出 (NuttX libc 支持)。
 */

#include "tsw_log.h"

#include <stdarg.h>
#include <string.h>


/* NuttX 目标平台: 用系统 tick 计算时间戳, 不依赖 POSIX feature 宏
 * (裸编 clock_gettime/CLOCK_MONOTONIC 需要额外宏定义, 易踩坑) */
#include <nuttx/config.h>
#include <nuttx/clock.h>


/* 运行时日志级别: 默认 INFO, 可通过 tsw_log_set_level() 调整 */
static volatile int s_tsw_log_level = TSW_LOG_LEVEL_INFO;

/* 单条日志消息最大长度 (不含前缀), 超长截断 */
#define TSW_LOG_MSG_MAX 192

int tsw_log_get_level(void)
{
    return s_tsw_log_level;
}

void tsw_log_set_level(int level)
{
    if (level >= TSW_LOG_LEVEL_NONE && level <= TSW_LOG_LEVEL_DEBUG) {
        s_tsw_log_level = level;
        printf("[tsw_log] level set to %d (%s)\n", level,
               level == TSW_LOG_LEVEL_NONE  ? "NONE"  :
               level == TSW_LOG_LEVEL_ERROR ? "ERROR" :
               level == TSW_LOG_LEVEL_WARN  ? "WARN"  :
               level == TSW_LOG_LEVEL_INFO  ? "INFO"  : "DEBUG");
    }
}

/* 从 __FILE__ 完整路径中提取文件名 (找最后一个 '/') */
static const char *tsw_log_basename(const char *file)
{
    const char *p = file ? strrchr(file, '/') : NULL;
    return p ? p + 1 : (file ? file : "?");
}

/* 级别编号 → 单字符标记 */
static char tsw_level_char(int level)
{
    switch (level) {
    case TSW_LOG_LEVEL_ERROR: return 'E';
    case TSW_LOG_LEVEL_WARN:  return 'W';
    case TSW_LOG_LEVEL_INFO:  return 'I';
    case TSW_LOG_LEVEL_DEBUG: return 'D';
    default:                  return '?';
    }
}

/* 获取单调时间戳 (秒, 保留 3 位小数), 失败返回 0 */
static double tsw_log_timestamp(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
    }
    return 0.0;
}

void tsw_log_write(int level, const char *tag, const char *file,
                   int line, const char *fmt, ...)
{
    char msg[TSW_LOG_MSG_MAX];
    va_list ap;

    /* 运行时级别过滤 (编译期裁剪已在宏层面完成) */
    if (level > s_tsw_log_level || s_tsw_log_level == TSW_LOG_LEVEL_NONE) {
        return;
    }
    if (tag == NULL) {
        tag = "TSW";
    }

    /* 格式化消息体, 截断保证 NUL 结尾 */
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    msg[sizeof(msg) - 1] = '\0';

    /* 格式: [  119.068][I][main_page] main_page.c:88: 消息
     * tag 用 %-8s 左对齐定宽, 便于肉眼按列对齐过滤 */
    flockfile(stdout);
    printf("[%10.3f][%c][%-8s] %s:%d: %s\n",
           tsw_log_timestamp(),
           tsw_level_char(level),
           tag,
           tsw_log_basename(file),
           line,
           msg);
    funlockfile(stdout);
}

void tsw_log_hexdump(const char *tag, const void *data, unsigned int len)
{
    const unsigned char *p = (const unsigned char *)data;
    char ascii[17]; /* 16 字符 + NUL */
    unsigned int i, j;

    if (data == NULL || len == 0) {
        printf("[%10.3f][H][%-8s] hexdump: (null or empty, len=%u)\n",
               tsw_log_timestamp(), tag ? tag : "TSW", len);
        return;
    }

    flockfile(stdout);

    printf("[%10.3f][H][%-8s] hexdump %p, %u bytes:\n",
           tsw_log_timestamp(), tag ? tag : "TSW", data, len);

    for (i = 0; i < len; i += 16) {
        /* 每行: 偏移(4位十六进制) + 16字节十六进制 + ASCII */
        printf("%04x: ", i);

        for (j = 0; j < 16; j++) {
            if (i + j < len) {
                printf("%02x ", p[i + j]);
                ascii[j] = (p[i + j] >= 0x20 && p[i + j] < 0x7f)
                               ? (char)p[i + j] : '.';
            } else {
                printf("   "); /* 不足 16 字节补齐对齐 */
                ascii[j] = ' ';
            }
        }
        ascii[16] = '\0';

        printf(" |%s|\n", ascii);
    }

    funlockfile(stdout);
}
