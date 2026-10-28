#ifndef __OFFLINE_RECORDING_STORE_H__
#define __OFFLINE_RECORDING_STORE_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>      /* FILE（offline_store_open_new 返回 FILE*） */

/* 离线录音缓存目录（手表数据根；Kconfig LVX_WATCH_DATA_ROOT 默认 /emmc）。
 * 列表页（§11）与回传逻辑（§4）都扫描此目录下的 rec_*.bin，保持单一约定。 */
#define OFFLINE_DIR           "/emmc"
#define OFFLINE_FILE_PREFIX   "rec_"
#define OFFLINE_FILE_SUFFIX   ".bin"

/* 运行时内存表容量（同时存在的离线文件数，实际远小于此）。 */
#define OFFLINE_MAX_FILES     32

typedef enum {
    OFFLINE_PENDING = 0,  /* 待回传 */
    OFFLINE_SYNCING,      /* 回传中（已发 TYPE_FILE，等手机 ACK） */
    OFFLINE_SYNCED       /* 已同步（手机 ACK success，即将删本地文件） */
} offline_state_t;

typedef struct {
    char            name[64];      /* rec_<UTCms>.bin */
    uint64_t        start_ts_ms;   /* 从文件名解析（UTC 毫秒） */
    uint32_t        size_bytes;    /* 从 stat 取 */
    offline_state_t  state;        /* 运行时状态（见内存状态表） */
} offline_file_entry_t;

/* 扫描 /emmc/rec_*.bin，按 start_ts_ms 倒序（最新在最上）填充 out[]，最多 max 条。返回实际条数。 */
int  offline_store_scan(offline_file_entry_t *out, int max);

/* 运行态标记（仅 PENDING 文件实际存在于磁盘）：
 *  SYNCING = 回传中；SYNCED = 同步成功并删文件；PENDING = 退回待重发。 */
void offline_store_mark_syncing(const char *name);
void offline_store_mark_synced(const char *name);   /* 标记已同步并删除本地文件 */
void offline_store_mark_pending(const char *name);  /* 回退为待重发（ACK fail 用） */

/* 将"已开始回传但超过 timeout_ms 仍未收到 ACK"的 SYNCING 项回退为 PENDING，
 * 供回传前调用，避免 ACK 丢失 / 手机未处理导致文件永久卡死无法重发。 */
void offline_store_reset_stale_syncing(uint32_t timeout_ms);

/* 删除本地文件（PENDING 长按删除 / 同步成功后清理）。返回 0 成功。 */
int  offline_store_remove(const char *name);

/* 标记 / 查询当前正在写入的缓存文件（录音进行中）。列表页据此跳过半截文件。
 * name=NULL 表示当前无正在写入的文件。 */
void offline_store_set_writing(const char *name);
bool offline_store_is_writing(const char *name);

/* 开一个本次录音的追加写文件，文件名 rec_<UTCms>.bin；成功返回 FILE* 并填充 out_name（不含目录）。 */
FILE *offline_store_open_new(uint64_t start_ts_ms, char *out_name, size_t name_sz);

/* 从 rec_<UTCms>.bin 文件名解析开始时间戳；非法返回 0。 */
uint64_t offline_store_parse_ts(const char *name);

/* CRC32（IEEE 802.3，与手机端 java.util.zip.CRC32 一致），用于 TYPE_FILE_END 校验。 */
uint32_t offline_crc32(const uint8_t *buf, size_t len);

#endif /* __OFFLINE_RECORDING_STORE_H__ */
