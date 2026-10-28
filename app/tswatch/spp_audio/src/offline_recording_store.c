/****************************************************************************
 * offline_recording_store.c - 手表端离线录音文件清单管理
 *
 * 职责（与离线录音双路径转写设计文档 §4 / §11 对齐）：
 *   - 扫描 /emmc/rec_*.bin，按开始时间戳倒序（最新在最上）填充条目；
 *   - 维护运行时状态表（PENDING / SYNCING / SYNCED），供列表页与回传逻辑共用；
 *   - 开新缓存文件、删除本地文件、CRC32 校验。
 *
 * 文件系统为 openvela/NuttX 标准 POSIX VFS（opendir/readdir/stat/unlink/fopen），
 * 与 vela_fs_test 同款 API，无需特殊驱动（P0 已验证可写）。
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include "offline_recording_store.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <dirent.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <time.h>

/* 单调时钟（ms），用于 SYNCING 超时判定，不受系统时间被校时影响。 */
static uint64_t store_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000u);
}

/* 运行时状态表：仅在内存中记录 SYNCING/SYNCED（SYNCED 随即删文件，不会长期存在）。
 * 磁盘上存在的 rec_*.bin 且不在本表的项，scan 时一律视为 PENDING。 */
typedef struct {
    char           name[64];
    offline_state_t state;
    uint64_t       syncing_since_ms;  /* 进入 SYNCING 的单调时刻，用于超时回退 */
    bool           used;
} state_rec_t;

static state_rec_t g_states[OFFLINE_MAX_FILES];

static state_rec_t *state_find(const char *name)
{
    for (int i = 0; i < OFFLINE_MAX_FILES; i++)
        if (g_states[i].used && strcmp(g_states[i].name, name) == 0)
            return &g_states[i];
    return NULL;
}

static void state_set(const char *name, offline_state_t st)
{
    state_rec_t *r = state_find(name);
    if (!r) {
        for (int i = 0; i < OFFLINE_MAX_FILES; i++) {
            if (!g_states[i].used) {
                r = &g_states[i];
                r->used = true;
                strncpy(r->name, name, sizeof(r->name) - 1);
                r->name[sizeof(r->name) - 1] = '\0';
                break;
            }
        }
    }
    if (r) {
        r->state = st;
        if (st == OFFLINE_SYNCING)
            r->syncing_since_ms = store_now_ms();
    }
}

static void state_clear(const char *name)
{
    state_rec_t *r = state_find(name);
    if (r)
        r->used = false;
}

uint64_t offline_store_parse_ts(const char *name)
{
    if (!name)
        return 0;
    const char *p = strstr(name, OFFLINE_FILE_PREFIX);
    if (!p)
        return 0;
    p += strlen(OFFLINE_FILE_PREFIX);
    char *end = NULL;
    uint64_t ts = (uint64_t)strtoull(p, &end, 10);
    if (end == p || ts == 0)
        return 0;
    return ts;
}

int offline_store_scan(offline_file_entry_t *out, int max)
{
    if (!out || max <= 0)
        return 0;

    DIR *d = opendir(OFFLINE_DIR);
    if (!d)
        return 0;

    int count = 0;
    struct dirent *e;
    const size_t pre_len = strlen(OFFLINE_FILE_PREFIX);
    const size_t suf_len = strlen(OFFLINE_FILE_SUFFIX);

    while ((e = readdir(d)) != NULL && count < max) {
        const char *nm = e->d_name;
        size_t nl = strlen(nm);

        /* 仅接受 rec_<ts>.bin */
        if (nl <= pre_len + suf_len)
            continue;
        if (strncmp(nm, OFFLINE_FILE_PREFIX, pre_len) != 0)
            continue;
        if (strcmp(nm + nl - suf_len, OFFLINE_FILE_SUFFIX) != 0)
            continue;

        struct stat st;
        char path[160];
        snprintf(path, sizeof(path), "%s/%s", OFFLINE_DIR, nm);
        if (stat(path, &st) != 0)
            continue;

        offline_file_entry_t *ent = &out[count];
        memset(ent, 0, sizeof(*ent));
        strncpy(ent->name, nm, sizeof(ent->name) - 1);
        ent->start_ts_ms = offline_store_parse_ts(nm);
        ent->size_bytes  = (uint32_t)st.st_size;

        state_rec_t *sr = state_find(nm);
        ent->state = sr ? sr->state : OFFLINE_PENDING;
        count++;
    }
    closedir(d);

    /* 按 start_ts_ms 倒序（插入排序，文件数很少） */
    for (int i = 1; i < count; i++) {
        offline_file_entry_t tmp = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].start_ts_ms < tmp.start_ts_ms) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = tmp;
    }
    return count;
}

void offline_store_mark_syncing(const char *name)
{
    state_set(name, OFFLINE_SYNCING);
}

void offline_store_mark_synced(const char *name)
{
    char path[160];
    snprintf(path, sizeof(path), "%s/%s", OFFLINE_DIR, name);
    unlink(path);
    state_clear(name);
    printf("[offline_store] 同步成功，已删除本地文件: %s\n", name);
}

void offline_store_mark_pending(const char *name)
{
    /* ACK fail 或超时：退回 PENDING 待下次重发 */
    state_set(name, OFFLINE_PENDING);
}

void offline_store_reset_stale_syncing(uint32_t timeout_ms)
{
    uint64_t now = store_now_ms();
    for (int i = 0; i < OFFLINE_MAX_FILES; i++) {
        state_rec_t *r = &g_states[i];
        if (!r->used || r->state != OFFLINE_SYNCING)
            continue;
        if (now - r->syncing_since_ms >= (uint64_t)timeout_ms) {
            printf("[offline_store] SYNCING 超时(%ums)，回退 PENDING 重发: %s\n",
                   timeout_ms, r->name);
            r->state = OFFLINE_PENDING;
        }
    }
}

int offline_store_remove(const char *name)
{
    char path[160];
    snprintf(path, sizeof(path), "%s/%s", OFFLINE_DIR, name);
    int r = unlink(path);
    state_clear(name);
    printf("[offline_store] 已删除本地文件: %s (r=%d)\n", name, r);
    return r;
}

FILE *offline_store_open_new(uint64_t start_ts_ms, char *out_name, size_t name_sz)
{
    char path[160];
    snprintf(path, sizeof(path), "%s/%s%" PRIu64 "%s",
             OFFLINE_DIR, OFFLINE_FILE_PREFIX, start_ts_ms, OFFLINE_FILE_SUFFIX);
    if (out_name && name_sz > 0) {
        /* 仅文件名（不含目录），如 rec_1718600000000.bin */
        const char *base = path + strlen(OFFLINE_DIR) + 1;
        strncpy(out_name, base, name_sz - 1);
        out_name[name_sz - 1] = '\0';
    }
    return fopen(path, "wb");
}

uint32_t offline_crc32(const uint8_t *buf, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint32_t)buf[i];
        for (int k = 0; k < 8; k++)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return ~crc;
}

/* 当前正在写入（录音进行中）的缓存文件名（仅在内存）。 */
static char g_writing_name[64];

void offline_store_set_writing(const char *name)
{
    if (name) {
        strncpy(g_writing_name, name, sizeof(g_writing_name) - 1);
        g_writing_name[sizeof(g_writing_name) - 1] = '\0';
    } else {
        g_writing_name[0] = '\0';
    }
}

bool offline_store_is_writing(const char *name)
{
    return name && g_writing_name[0] != '\0' &&
           strcmp(name, g_writing_name) == 0;
}
