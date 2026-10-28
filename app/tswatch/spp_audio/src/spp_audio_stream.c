/****************************************************************************
 * apps/examples/tswatch/spp_audio/spp_audio_stream.c
 *
 * Captures mic PCM (16kHz/16bit/mono) via the Vela media framework
 * (media_recorder / MEDIA_SOURCE_MIC) and streams it over the SPP
 * (bt_transport) channel to the phone, framed so the phone can reconstruct
 * 20ms Opus-compressed packets (decoded back to 16k/16bit/mono PCM on the
 * phone) and (de)multiplex control messages.
 *
 * Why the media framework and NOT a direct open of /dev/audio/pcm0c?
 *   A direct open + AUDIOIOC_CONFIGURE of /dev/audio/pcm0c routes to the BES
 *   audio HAL:  af_stream_open(codec=0, capture) -> hal_dma_get_chan(periph=70)
 *   On this board periph 70 (the internal codec=0 capture DMA) is invalid, so
 *   hal_dma_get_chan() ASSERTs ("Invalid DMA periph: 70", hal_dma_get_chan:932)
 *   -> kernel panic -> board_reset(ap force panic audio) -> reboot.
 *   That assert is inside the (prebuilt) HAL and CANNOT be trapped from
 *   userspace, so opening the raw node bricks the device on "start recording".
 *
 *   The SAME broken codec=0 capture DMA is why the SMF/MicDomain capture used
 *   to time out (mic0 EVENT_STARTED(result=-1), no "pcm0c,start"): the direct
 *   path just makes the identical failure fatal instead of silent. So the
 *   mic-no-data problem is a board-level codec/DMA bring-up defect, not app
 *   logic -- see notes below. The media_recorder path is the supported,
 *   non-crashing way to ask for the mic; it is kept here so the app stays
 *   usable and so the failure (if any) surfaces as a clean error, not a reboot.
 *
 * It also exposes a live amplitude (0..1) derived from real microphone energy,
 * so the UI can draw a sound-level meter that reacts to voice.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#include <time.h>
#include <inttypes.h>
#include <sys/stat.h>

#include <media_recorder.h>
#include <media_defs.h>

#include <opus.h>              /* 手表侧 Opus 编码器（SDK 已编入 libopus，CONFIG_LIB_OPUS=y）*/

#include "bt_transport.h"      /* bt_transport_send / is_connected / 连接回调 */
#include "offline_recording_store.h"  /* 离线录音落盘 / 回传清单 */
#include "../../offline_recording/include/offline_recording_list.h"  /* 列表页同步提示 */

/****************************************************************************
 * Frame protocol (little-endian fields, payload depends on type)
 *
 *   byte0     : magic  = 0xA5
 *   byte1     : type   = 0x01 PCM     (原始 16k/16bit 小端；历史兼容，现改发 Opus)
 *                       0x02 CONTROL (payload[0]: 1=session_start, 2=session_stop)
 *                       0x03 TEXT    (payload = UTF-8 status string)
 *                       0x04 OPUS    (payload = 一个 Opus 包，40ms@16k/mono；
 *                                    由手机端解码回 PCM 再上传 ASR)
 *   byte2..3  : seq    (uint16, wraps)
 *   byte4..5  : len    (uint16, payload length in bytes)
 *   byte6..   : payload
 ****************************************************************************/

#define SPP_STREAM_MAGIC   0xA5

#define SPP_FRAME_TYPE_PCM      0x01
#define SPP_FRAME_TYPE_CONTROL  0x02
#define SPP_FRAME_TYPE_TEXT     0x03
#define SPP_FRAME_TYPE_OPUS     0x04   /* 手表端编码后的 Opus 音频包 */

/* 离线录音回传协议（设计文档 §3）：TYPE_FILE 三段 + 反向 TYPE_FILE_ACK */
#define SPP_FRAME_TYPE_FILE     0x05   /* 手表→手机：离线音频文件回传 */
#define SPP_FRAME_TYPE_FILE_ACK 0x06   /* 手机→手表：离线文件处理结果 */
#define FILE_SUB_START  0x00   /* payload: fileId(8) | totalSize(4) | codec(1) | sampleRate(4) | channels(1) | nameLen(1) | name */
#define FILE_SUB_DATA   0x01   /* payload: chunkIndex(2 BE) | data(≤512) */
#define FILE_SUB_END    0x02   /* payload: fileId(8) | crc32(4 BE) */

#define SPP_CTRL_SESSION_START  0x01
#define SPP_CTRL_SESSION_STOP   0x02

/* 采集帧粒度：20ms @ 16kHz/16bit/mono = 320 samples = 640 bytes（用于 VAD/计量与 Opus 输入累积）*/
#define SPP_FRAME_SAMPLES  320
#define SPP_FRAME_BYTES   (SPP_FRAME_SAMPLES * 2)

/* Opus 编码：每 2 个 20ms 帧合成一个 40ms 包（Opus 在 16k 下的合法帧长 320/640 sample）。*/
#define OPUS_SAMPLE_RATE    16000
#define OPUS_BITRATE        16000   /* bps；~2KB/s，相比原始 PCM 32KB/s 降低约 16 倍 */
#define OPUS_PACKET_SAMPLES (SPP_FRAME_SAMPLES * 2)   /* 640 samples = 40ms */
/* Opus 单包上限（40ms@16kbps 实际远小于此；给足余量）。*/
#define OPUS_MAX_PACKET     512

/* SYNCING 超时回退阈值（ms）：超过此时间仍未收到手机 ACK 的回传中文件，
 * 在下次回传前回退为 PENDING 重试。取 5 分钟，大于手机 Ifasr_llm 正常耗时上限，
 * 既自愈 ACK 丢失，又避免正常处理期间误重发导致手机侧重复记录。*/
#define OFFLINE_SYNCING_TIMEOUT_MS  300000

/* Capture format we request from the media framework (raw PCM, we frame it). */
#define PCM_SAMPRATE     16000
#define PCM_CHANNELS     1
#define PCM_BPSAMP       16

/* media_recorder options: raw 16-bit mono 16kHz PCM, min frame 1920B
 * (one 60ms frame). Mirrors the proven xiaozhi capture options. */
#define PCM_RECORDER_OPTIONS \
  "fmt=[rate=#16000,ch=#1,bits=#16,width=#2],enc=[keys=pcm,imin=#1920]"

/* One read chunk: one 60ms frame @16k/mono/16bit = 960 samples = 1920 bytes. */
#define CAP_CHUNK_BYTES  (PCM_SAMPRATE * 60 / 1000 * PCM_CHANNELS * (PCM_BPSAMP / 8))

/* Sensitivity: mean-abs PCM value mapped to amplitude 1.0. Real speech sits
 * roughly 1000-5000, loud peaks higher; silence ~0. 3000 gives a lively but
 * not saturated meter. Tune if needed. */
#define AMP_FULLSCALE  3000.0f

/* Software mic gain applied DURING RECORDING only. It is multiplied into every
 * captured PCM sample right after media_recorder_read_data() and BEFORE the
 * VAD / amplitude meter / BT send, so all three see the boosted signal. This
 * is a safe, app-level boost that complements the codec's hardware capture
 * gain (CODEC_SADC_VOL in the board tgt_hardware.h).
 *
 * Why software rather than only hardware: the previous logs showed the raw mic
 * energy reaching the app was extremely low (mean-abs ~3..57, near silence),
 * so the VAD never opened and nothing was forwarded to the phone. Raising this
 * factor lifts the level into the usable speech range.
 *
 * Tune-instructions: start ~8.0 (≈18 dB). If the next log's "VAD stat energy"
 * after speaking is still far below the VAD threshold (floor*3), raise this;
 * if loud peaks clip (meter pinned at 1.0 with distortion), lower it.
 * Can also be overridden at compile time with -DMIC_GAIN=12.0 */
#ifndef MIC_GAIN
#define MIC_GAIN  8.0f
#endif

/* Accumulator headroom: a single capture chunk can be up to one 60ms frame;
 * keep well above one SPP frame. */
#define ACC_BUF_SIZE   (SPP_FRAME_BYTES * 8)

static bool        g_active = false;
static uint16_t    g_seq = 0;

/* 复用发送缓冲：头(6) + 最大可能载荷。最大载荷出现在"Opus 不可用退回发原始 PCM"
 * 时的 40ms 整包 = OPUS_PACKET_SAMPLES*2 = 1280 字节，故直接给足余量；Opus 包
 * (<=OPUS_MAX_PACKET) 远小于此。 */
static uint8_t     g_tx_buf[6 + 2048];

/* Capture thread state */
static pthread_t   g_capture_tid;
static volatile bool g_capture_tid_valid = false;

/* media_recorder handle (owned by the capture thread, touched by stop() to
 * unblock a pending read_data). Protected by g_mu. */
static void       *g_recorder = NULL;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

/* PCM accumulator: feeds fixed 20ms(640B) frames regardless of driver chunk. */
static uint8_t     g_acc[ACC_BUF_SIZE];
static size_t      g_acc_len = 0;

/* Opus 编码器与 40ms(=2×20ms) 编码输入缓冲。 */
static OpusEncoder *g_opus = NULL;
static opus_int16   g_opus_pcm[OPUS_PACKET_SAMPLES];
static size_t       g_opus_pcm_len = 0;

/* Live amplitude 0..1 (mean-abs of PCM, EMA-smoothed). Read by the UI timer
 * (main thread); written by the capture thread. */
static volatile float g_amplitude = 0.0f;

/* 离线缓存：当前录音会话的本地落盘句柄（SPP 未连时把音频帧写入 eMMC）。
 * 文件以 rec_<UTCms>.bin 命名，内部按 TYPE_OPUS（或 TYPE_PCM 回退）SPP 帧顺序落盘，
 * 回传时按帧读出每个音频包作为 TYPE_FILE 的一个 DATA 分片。 */
static FILE    *g_offline_fp = NULL;
static char     g_offline_name[64];
static bool     g_offline_open = false;
static uint16_t g_cache_seq = 0;

/****************************************************************************
 * Voice Activity Detection (VAD)
 *
 * Every 20ms PCM frame is fed to the VAD purely for diagnostics (OPEN/CLOSED
 * logging + the live amplitude meter). The VAD decision NO LONGER gates
 * sending: ALL frames -- including silence -- are encoded to Opus and streamed
 * continuously to the phone. This keeps RTASR's audio stream unbroken, so it
 * never hits the 15s silence-timeout disconnect, and slashes BT data volume
 * (Opus VBR compresses silence to a few bytes per 40ms packet). VAD still stays
 * open for VAD_HANGOVER_MS after the last speech frame, only to keep the
 * diagnostic "speech" flag stable.
 *
 * The detector is a lightweight energy check (mean-abs of the 16-bit samples):
 *   - an adaptive noise floor tracks the ambient level while not speaking;
 *   - a frame is "speech" when its energy > noise_floor * ratio AND above an
 *     absolute floor (ignores codec / digital silence);
 *   - a short debounce (VAD_OPEN_DEBOUNCE frames) rejects single-frame spikes.
 * Tune VAD_SPEECH_RATIO / VAD_ABS_MIN against real mic recordings if needed.
 ****************************************************************************/

#define VAD_FRAME_MS        20
#define VAD_HANGOVER_MS     1000
#define VAD_HANGOVER_FRAMES (VAD_HANGOVER_MS / VAD_FRAME_MS)   /* 40 */
#define VAD_OPEN_DEBOUNCE   2      /* consecutive speech frames to open */
#define VAD_SPEECH_RATIO    3.0f   /* speech if energy > floor * ratio */
#define VAD_ABS_MIN         300.0f /* absolute speech gate (ignore codec noise) */
#define VAD_NOISE_FLOOR_MIN 50.0f  /* lower bound for adaptive noise floor */

typedef struct {
    bool     speech;        /* current open/closed decision */
    float    noise_floor;   /* adaptive ambient energy (mean-abs) */
    float    max_energy;    /* peak frame energy seen this session (diag) */
    int32_t  tail_frames;   /* remaining hangover frames (>=0) */
    int32_t  speech_run;    /* consecutive speech frames (debounce) */
    bool     prev_speech;   /* for transition logging */
} vad_state_t;

static vad_state_t g_vad;

/* Reset VAD to a clean state for a new recording session. */
static void vad_reset(void)
{
    memset(&g_vad, 0, sizeof(g_vad));
    g_vad.noise_floor = VAD_NOISE_FLOOR_MIN;
}

/* Feed one 25ms frame (SPP_FRAME_SAMPLES samples). Returns true if the frame
 * should be sent to the phone (VAD open), false if it is silence (drop). */
static bool vad_process_frame(const int16_t *pcm, size_t samples)
{
    uint64_t sum = 0;
    for (size_t i = 0; i < samples; i++)
      {
        int32_t v = pcm[i];
        if (v < 0) v = -v;
        sum += (uint64_t)v;
      }
    float e = (samples > 0) ? ((float)sum / (float)samples) : 0.0f;

    /* Track peak energy so a brief utterance missed by the 1s snapshot still
     * shows up in the diagnostic output. */
    if (e > g_vad.max_energy)
        g_vad.max_energy = e;

    /* Adapt the noise floor only while not actively speaking, so a loud
     * utterance does not drag the floor up and later mask real speech. */
    if (!g_vad.speech)
      {
        g_vad.noise_floor = g_vad.noise_floor * 0.95f + e * 0.05f;
        if (g_vad.noise_floor < VAD_NOISE_FLOOR_MIN)
            g_vad.noise_floor = VAD_NOISE_FLOOR_MIN;
      }

    bool is_speech = (e > g_vad.noise_floor * VAD_SPEECH_RATIO) &&
                     (e > VAD_ABS_MIN);

    if (is_speech)
      {
        g_vad.speech_run++;
        if (g_vad.speech_run >= VAD_OPEN_DEBOUNCE)
            g_vad.tail_frames = VAD_HANGOVER_FRAMES;   /* (re)arm hangover */
      }
    else
      {
        g_vad.speech_run = 0;
        if (g_vad.tail_frames > 0)
            g_vad.tail_frames--;   /* countdown only during silence */
      }

    g_vad.speech = (g_vad.speech_run >= VAD_OPEN_DEBOUNCE) ||
                   (g_vad.tail_frames > 0);

    if (g_vad.speech != g_vad.prev_speech)
      {
        printf("[spp_audio] VAD %s (energy=%.0f floor=%.0f tail=%d)\n",
               g_vad.speech ? "OPEN" : "CLOSED",
               e, g_vad.noise_floor, (int)g_vad.tail_frames);
        g_vad.prev_speech = g_vad.speech;
      }

    /* Periodic ~1s snapshot so a silent/quiet session is still diagnosable:
     * confirms frames are flowing and shows mic energy vs the VAD threshold.
     * (energy=frame mean-abs, floor=adaptive noise floor, thr=effective gate) */
    static int dbg_cnt = 0;
    if (++dbg_cnt >= VAD_HANGOVER_FRAMES)   /* ~40 frames ~= 1 second */
      {
        dbg_cnt = 0;
        float thr = (g_vad.noise_floor * VAD_SPEECH_RATIO > VAD_ABS_MIN)
                        ? g_vad.noise_floor * VAD_SPEECH_RATIO : VAD_ABS_MIN;
        printf("[spp_audio] VAD stat energy=%.0f floor=%.0f thr=%.0f max=%.0f speech=%d\n",
               e, g_vad.noise_floor, thr, g_vad.max_energy, (int)g_vad.speech);
      }

    return g_vad.speech;
}

/* Build a frame into g_tx_buf and send it via bt_transport. */
static void spp_send_frame(uint8_t type, const uint8_t *payload, uint16_t len)
{
    if (!bt_transport_is_connected())
        return;

    g_tx_buf[0] = SPP_STREAM_MAGIC;
    g_tx_buf[1] = type;
    g_tx_buf[2] = (uint8_t)(g_seq >> 8);
    g_tx_buf[3] = (uint8_t)(g_seq & 0xFF);
    g_tx_buf[4] = (uint8_t)(len >> 8);
    g_tx_buf[5] = (uint8_t)(len & 0xFF);
    if (len > 0 && payload != NULL)
        memcpy(g_tx_buf + 6, payload, len);

    g_seq++;

    int ret = bt_transport_send(g_tx_buf, (uint16_t)(6 + len));
    if (ret < 0)
        printf("[spp_audio] send failed (not connected?)\n");
}

/* Update live amplitude from captured PCM (mean-abs, EMA-smoothed). */
static void spp_update_amplitude(const int16_t *pcm, size_t samples)
{
    uint64_t sum = 0;
    for (size_t i = 0; i < samples; i++)
      {
        int32_t v = pcm[i];
        if (v < 0) v = -v;
        sum += (uint64_t)v;
      }

    float mae = (samples > 0) ? ((float)sum / (float)samples) : 0.0f;
    float amp = mae / AMP_FULLSCALE;
    if (amp > 1.0f) amp = 1.0f;

    /* EMA: keep the meter from flickering frame-to-frame. */
    g_amplitude = g_amplitude * 0.6f + amp * 0.4f;
}

/* Apply a fixed software gain to captured PCM (recording only), saturating to
 * the int16 range so loud peaks do not wrap. Called between read and VAD/send. */
static void spp_apply_mic_gain(int16_t *pcm, size_t samples)
{
    for (size_t i = 0; i < samples; i++)
      {
        int32_t v = (int32_t)pcm[i];
        int32_t g = (int32_t)(v * MIC_GAIN);
        if (g > 32767) g = 32767;
        else if (g < -32768) g = -32768;
        pcm[i] = (int16_t)g;
      }
}

/* ---------- 离线缓存 / 回传辅助函数 ---------- */

/* 当前 UTC 毫秒（文件名时间戳 + 列表页时间展示用）。 */
static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000u);
}

/* 大端读 8 字节（fileId）。 */
static uint64_t read_u64_be(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v = (v << 8) | p[i];
    return v;
}

/* 把一帧（type + payload）按 SPP 帧头写入当前离线缓存文件（仅缓存线程调用）。 */
static void offline_cache_write_frame(uint8_t type, const uint8_t *payload, uint16_t len)
{
    if (!g_offline_fp)
        return;
    uint8_t hdr[6];
    hdr[0] = SPP_STREAM_MAGIC;
    hdr[1] = type;
    hdr[2] = (uint8_t)(g_cache_seq >> 8);
    hdr[3] = (uint8_t)(g_cache_seq & 0xFF);
    hdr[4] = (uint8_t)(len >> 8);
    hdr[5] = (uint8_t)(len & 0xFF);
    g_cache_seq++;
    fwrite(hdr, 1, 6, g_offline_fp);
    if (len > 0 && payload)
        fwrite(payload, 1, len, g_offline_fp);
    fflush(g_offline_fp);   /* 保证断连/掉电前已落盘（文件较小，fflush 即可，fsync 可选） */
}

/* 读取一个 rec_*.bin（内部是 TYPE_OPUS / TYPE_PCM 帧序列），经 TYPE_FILE 协议回传。
 * 每个音频包作为单独一个 FILE_DATA 分片（Opus 包 ≤ OPUS_MAX_PACKET ≤ 512B，恰为一分片）。
 * 手机侧按 chunkIndex 重组后逐片解码；ACK 由 spp_rx_cb 异步处理。 */
static void offline_flush_one(const offline_file_entry_t *ent)
{
    char path[160];
    snprintf(path, sizeof(path), "%s/%s", OFFLINE_DIR, ent->name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("[spp_audio] flush: 打开失败 %s\n", path);
        return;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0) {
        fclose(f);
        offline_store_remove(ent->name);
        return;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)fsize);
    if (!buf) {
        fclose(f);
        return;
    }
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        free(buf);
        fclose(f);
        return;
    }
    fclose(f);

    /* 第一遍：解析内部音频帧，统计音频包总数与总字节，确认有可回传数据。 */
    uint64_t file_id  = ent->start_ts_ms;
    uint32_t total    = 0;
    int      npackets = 0;
    int      codec    = 1;   /* 默认 Opus；若混入 TYPE_PCM 则整文件按 PCM(0) */
    long off = 0;
    while (off + 6 <= fsize) {
        if (buf[off] != SPP_STREAM_MAGIC) { off++; continue; }
        uint8_t  ftype = buf[off + 1];
        int flen = ((buf[off + 4] << 8) | buf[off + 5]) & 0xFFFF;
        if (off + 6 + (long)flen > fsize) break;
        if (ftype == SPP_FRAME_TYPE_OPUS || ftype == SPP_FRAME_TYPE_PCM) {
            total += (uint32_t)flen;
            npackets++;
            if (ftype == SPP_FRAME_TYPE_PCM)
                codec = 0;
        }
        off += 6 + (long)flen;
    }
    if (npackets == 0 || total == 0) {
        free(buf);
        offline_store_remove(ent->name);   /* 无音频内容，直接清掉 */
        return;
    }

    offline_store_mark_syncing(ent->name);

    /* FILE_START：subtype(1) + fileId(8) + totalSize(4) + codec(1) + sampleRate(4) + channels(1) + nameLen(1) + name */
    int name_len = (int)strlen(ent->name);
    uint8_t start[1 + 8 + 4 + 1 + 4 + 1 + 1 + 64];
    int sp = 0;
    start[sp++] = FILE_SUB_START;
    for (int i = 7; i >= 0; i--) start[sp++] = (uint8_t)(file_id >> (i * 8));
    start[sp++] = (uint8_t)((total >> 24) & 0xFF);
    start[sp++] = (uint8_t)((total >> 16) & 0xFF);
    start[sp++] = (uint8_t)((total >> 8) & 0xFF);
    start[sp++] = (uint8_t)(total & 0xFF);
    start[sp++] = (uint8_t)codec;
    start[sp++] = (uint8_t)(OPUS_SAMPLE_RATE >> 24 & 0xFF);
    start[sp++] = (uint8_t)(OPUS_SAMPLE_RATE >> 16 & 0xFF);
    start[sp++] = (uint8_t)(OPUS_SAMPLE_RATE >> 8 & 0xFF);
    start[sp++] = (uint8_t)(OPUS_SAMPLE_RATE & 0xFF);
    start[sp++] = (uint8_t)1;                 /* channels = 1 */
    start[sp++] = (uint8_t)name_len;
    memcpy(start + sp, ent->name, name_len);
    sp += name_len;
    spp_send_frame(SPP_FRAME_TYPE_FILE, start, (uint16_t)sp);

    /* FILE_DATA：subtype(1) + chunkIndex(2 BE) + data(≤512)。每包一帧。 */
    off = 0;
    int chunk = 0;
    uint32_t crc = 0xFFFFFFFFu;
    while (off + 6 <= fsize) {
        if (buf[off] != SPP_STREAM_MAGIC) { off++; continue; }
        uint8_t  ftype = buf[off + 1];
        int flen = ((buf[off + 4] << 8) | buf[off + 5]) & 0xFFFF;
        if (off + 6 + (long)flen > fsize) break;
        if (ftype == SPP_FRAME_TYPE_OPUS || ftype == SPP_FRAME_TYPE_PCM) {
            const uint8_t *pkt = buf + off + 6;
            uint8_t data[1 + 2 + OPUS_MAX_PACKET];
            int dp = 0;
            data[dp++] = FILE_SUB_DATA;
            data[dp++] = (uint8_t)((chunk >> 8) & 0xFF);
            data[dp++] = (uint8_t)(chunk & 0xFF);
            memcpy(data + dp, pkt, (size_t)flen);
            dp += flen;
            spp_send_frame(SPP_FRAME_TYPE_FILE, data, (uint16_t)dp);
            /* CRC32 按发送顺序累加各音频包原始字节 */
            for (int k = 0; k < flen; k++) {
                crc ^= (uint32_t)pkt[k];
                for (int b = 0; b < 8; b++)
                    crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
            }
            chunk++;
        }
        off += 6 + (long)flen;
    }
    crc = ~crc;

    /* FILE_END：subtype(1) + fileId(8) + crc32(4 BE) */
    uint8_t end[1 + 8 + 4];
    int ep = 0;
    end[ep++] = FILE_SUB_END;
    for (int i = 7; i >= 0; i--) end[ep++] = (uint8_t)(file_id >> (i * 8));
    end[ep++] = (uint8_t)((crc >> 24) & 0xFF);
    end[ep++] = (uint8_t)((crc >> 16) & 0xFF);
    end[ep++] = (uint8_t)((crc >> 8) & 0xFF);
    end[ep++] = (uint8_t)(crc & 0xFF);
    spp_send_frame(SPP_FRAME_TYPE_FILE, end, (uint16_t)ep);

    free(buf);
    printf("[spp_audio] flush 已回传 %s: 包数=%d 总字节=%u crc=0x%08X\n",
           ent->name, chunk, total, crc);
}

/* 扫描全部 PENDING 离线文件并逐个回传（跳过正在写的当前录音文件）。 */
static void offline_flush_all(void)
{
    if (!bt_transport_is_connected())
        return;
    /* 先回退"已发但超时未收到 ACK"的陈旧 SYNCING，避免永久卡死。
     * 超时取 5 分钟：大于手机端 Ifasr_llm 批量转写正常耗时上限，
     * 既能自愈 ACK 丢失，又不会在正常处理期间误重发导致手机侧重复记录。 */
    offline_store_reset_stale_syncing(OFFLINE_SYNCING_TIMEOUT_MS);
    offline_file_entry_t entries[OFFLINE_MAX_FILES];
int n = offline_store_scan(entries, OFFLINE_MAX_FILES);
    for (int i = 0; i < n; i++) {
        if (entries[i].state != OFFLINE_PENDING)
            continue;
        if (g_offline_open && strcmp(entries[i].name, g_offline_name) == 0)
            continue;   /* 正在写，等录音结束再传 */
        offline_flush_one(&entries[i]);
    }
}

/* 手机→手表 数据回调：仅处理 TYPE_FILE_ACK（其余忽略，避免影响现网路径）。 */
static void spp_rx_cb(const uint8_t *data, uint16_t len, const bt_address_t *peer)
{
    (void)peer;
    if (len >= 13 && data[0] == SPP_STREAM_MAGIC && data[1] == SPP_FRAME_TYPE_FILE_ACK) {
        uint64_t file_id = read_u64_be(data + 2);
        uint8_t  status  = data[10];
        char name[64];
        snprintf(name, sizeof(name), "%s%" PRIu64 "%s",
                 OFFLINE_FILE_PREFIX, file_id, OFFLINE_FILE_SUFFIX);
        if (status == 0) {
            printf("[spp_audio] 离线文件同步成功: %s\n", name);
            offline_store_mark_synced(name);      /* 删除本地文件 */
            offline_recordings_on_synced(name);   /* 通知列表页「已同步」提示 */
        } else {
            printf("[spp_audio] 离线文件同步失败，留待重发: %s\n", name);
            offline_store_mark_pending(name);     /* 退回 PENDING */
        }
    } else {
        printf("[spp_audio] 收到未处理的对端数据(%d bytes)，忽略\n", len);
    }
}

/* 手机连接建立回调：触发离线文件回传。 */
static void spp_on_connected(const bt_address_t *peer)
{
    (void)peer;
    printf("[spp_audio] SPP 已连接，触发离线文件回传\n");
    offline_flush_all();
}

/* 采集线程把 PCM 喂进来；这里按 20ms(640B) 累积，每凑满 2 帧(40ms) 编码成一个
 * Opus 包，连续发给手机（含静音）。静音段在 Opus VBR 下近乎 0 字节，既降低蓝牙
 * 数据量，又保证 RTASR 收到连续音频流，根除「15s 无音频超时」断连。
 * VAD 仅用于日志/振幅诊断，不再参与发送门控。Opus 编码器不可用时退回发原始 PCM。
 *
 * 离线逻辑：SPP 未连接时，把音频帧写入本地 eMMC 缓存文件（而非丢弃）；连上后由
 * spp_on_connected → offline_flush_all 回传。 */
static void accumulate_and_send(const uint8_t *data, size_t nbytes)
{
    if (nbytes == 0)
        return;

    /* overflow 保护（ACC_BUF_SIZE 留足余量，正常不会触发）。 */
    if (g_acc_len + nbytes > ACC_BUF_SIZE)
      {
        size_t over = (g_acc_len + nbytes) - ACC_BUF_SIZE;
        memmove(g_acc, g_acc + over, g_acc_len - over);
        g_acc_len -= over;
      }

    memcpy(g_acc + g_acc_len, data, nbytes);
    g_acc_len += nbytes;

    while (g_acc_len >= SPP_FRAME_BYTES)
      {
        const int16_t *frame = (const int16_t *)g_acc;

        /* VAD 仅用于诊断日志（OPEN/CLOSED、能量统计），不改变发送行为。 */
        vad_process_frame(frame, SPP_FRAME_SAMPLES);

        /* 把这一帧 20ms 拷进 Opus 输入缓冲，凑满 40ms(2 帧) 即编码发送。 */
        memcpy(g_opus_pcm + g_opus_pcm_len, frame, SPP_FRAME_BYTES);
        g_opus_pcm_len += SPP_FRAME_SAMPLES;

        if (g_opus_pcm_len >= OPUS_PACKET_SAMPLES)
          {
            if (g_opus != NULL)
              {
                uint8_t opus_pkt[OPUS_MAX_PACKET];
                opus_int32 n = opus_encode(g_opus, g_opus_pcm, OPUS_PACKET_SAMPLES,
                                           opus_pkt, OPUS_MAX_PACKET);
                if (n > 0)
                  {
                    /* SPP 已连 → 实时直发；未连 → 写本地 eMMC 缓存文件（待回传）。 */
                    if (bt_transport_is_connected())
                        spp_send_frame(SPP_FRAME_TYPE_OPUS, opus_pkt, (uint16_t)n);
                    else
                        offline_cache_write_frame(SPP_FRAME_TYPE_OPUS, opus_pkt, (uint16_t)n);
                  }
                else
                  {
                    printf("[spp_audio] opus_encode error: %ld\n", (long)n);
                  }
              }
            else
              {
                /* 编码器不可用：退回发送原始 16k PCM（手机端兼容 TYPE_PCM）。 */
                if (bt_transport_is_connected())
                    spp_send_frame(SPP_FRAME_TYPE_PCM, (const uint8_t *)g_opus_pcm,
                                   (uint16_t)(OPUS_PACKET_SAMPLES * 2));
                else
                    offline_cache_write_frame(SPP_FRAME_TYPE_PCM, (const uint8_t *)g_opus_pcm,
                                             (uint16_t)(OPUS_PACKET_SAMPLES * 2));
              }
            g_opus_pcm_len = 0;
          }

        memmove(g_acc, g_acc + SPP_FRAME_BYTES, g_acc_len - SPP_FRAME_BYTES);
        g_acc_len -= SPP_FRAME_BYTES;
      }
}

/* Capture thread: open the mic via media_recorder, pump PCM to SPP. */
static void *spp_capture_thread(void *arg)
{
    (void)arg;

    void *recorder;
    int ret;
    uint8_t chunk[CAP_CHUNK_BYTES];

    recorder = media_recorder_open(MEDIA_SOURCE_MIC);
    if (recorder == NULL)
      {
        printf("[spp_audio] media_recorder_open(MIC) failed\n");
        g_active = false;
        return NULL;
      }

    ret = media_recorder_prepare(recorder, NULL, PCM_RECORDER_OPTIONS);
    if (ret < 0)
      {
        printf("[spp_audio] media_recorder_prepare failed: %d\n", ret);
        media_recorder_close(recorder);
        g_active = false;
        return NULL;
      }

    ret = media_recorder_start(recorder);
    if (ret < 0)
      {
        printf("[spp_audio] media_recorder_start failed: %d\n", ret);
        media_recorder_close(recorder);
        g_active = false;
        return NULL;
      }

    pthread_mutex_lock(&g_mu);
    g_recorder = recorder;
    pthread_mutex_unlock(&g_mu);

    printf("[spp_audio] recorder started (MEDIA_SOURCE_MIC %dHz/%dch/%dbit), "
           "mic_gain=%.1f, pumping PCM...\n",
           PCM_SAMPRATE, PCM_CHANNELS, PCM_BPSAMP, (double)MIC_GAIN);

    while (g_active)
      {
        ssize_t n = media_recorder_read_data(recorder, chunk, sizeof(chunk));
        if (n <= 0)
          {
            if (n < 0)
                printf("[spp_audio] read_data error: %d\n", (int)n);
            else
                printf("[spp_audio] read_data EOF\n");
            break;
          }

        spp_apply_mic_gain((int16_t *)chunk, (size_t)n / 2);
        spp_update_amplitude((const int16_t *)chunk, (size_t)n / 2);
        accumulate_and_send(chunk, (size_t)n);
      }

    printf("[spp_audio] capture loop ended (active=%d)\n", g_active);

    pthread_mutex_lock(&g_mu);
    g_recorder = NULL;
    pthread_mutex_unlock(&g_mu);

    media_recorder_stop(recorder);
    media_recorder_close(recorder);

    g_amplitude = 0.0f;
    g_acc_len = 0;
    printf("[spp_audio] capture stopped & cleaned up\n");
    return NULL;
}

/* 录音 App 进入时调用：注册离线回传所需的"连接状态回调 + 接收回调"。
 * 仅注册、不启采集，故即使尚未开始录音，已存在的离线文件也能被回传；
 * 若 SPP 此刻已连，立即触发一次回传，覆盖"先连蓝牙、后进 App"的场景
 * （否则连接事件已发生、不会再触发 g_conn_cb，离线文件将永远等不到回传）。
 * 该函数可重复调用，幂等。 */
void spp_audio_ensure_offline_sync(void)
{
    bt_transport_set_conn_callback(spp_on_connected);
    bt_transport_set_rx_callback(spp_rx_cb);
    if (bt_transport_is_connected())
        offline_flush_all();
}

void spp_audio_stream_start(void)
{
    if (g_active)
        return;

    /* Clean up any straggler thread from a previous session. */
    if (g_capture_tid_valid)
      {
        g_active = false;
        pthread_join(g_capture_tid, NULL);
        g_capture_tid_valid = false;
      }

    g_active = true;
    g_amplitude = 0.0f;
    g_acc_len = 0;
    g_opus_pcm_len = 0;
    if (g_opus != NULL) { opus_encoder_destroy(g_opus); g_opus = NULL; }
    vad_reset();

    /* 开一个本次录音的离线缓存文件（SPP 未连时落盘用；若全程已连，停止时为空则删）。
     * 文件名 rec_<UTCms>.bin，内部按 TYPE_OPUS/TYPE_PCM SPP 帧顺序写，回传时按帧读出。 */
    {
        char name[64];
        g_offline_fp = offline_store_open_new(now_ms(), name, sizeof(name));
        if (g_offline_fp) {
            strncpy(g_offline_name, name, sizeof(g_offline_name) - 1);
            g_offline_name[sizeof(g_offline_name) - 1] = '\0';
            g_offline_open = true;
            offline_store_set_writing(g_offline_name);  /* 标记正在写入（列表页跳过） */
            printf("[spp_audio] 离线缓存文件已就绪: %s\n", g_offline_name);
        } else {
            printf("[spp_audio] 离线缓存文件创建失败（eMMC 可能只读/满），将仅实时发送\n");
        }
    }

    /* 注册连接/接收回调：手机连上即回传离线文件；接收手机 ACK 后删本地文件。 */
    bt_transport_set_conn_callback(spp_on_connected);
    bt_transport_set_rx_callback(spp_rx_cb);

    /* 创建 Opus 编码器：16k / 单声道 / VOIP 模式；每 40ms(2×20ms) 编一包。
     * 编码器内部定点运算，在手表 MCU 上开销极小（~1-2 MIPS）。*/
    {
        int oerr = 0;
        g_opus = opus_encoder_create(OPUS_SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP, &oerr);
        if (g_opus == NULL || oerr != OPUS_OK)
          {
            printf("[spp_audio] opus_encoder_create failed (err=%d)\n", oerr);
            g_opus = NULL;
          }
        else
          {
            opus_encoder_ctl(g_opus, OPUS_SET_BITRATE(OPUS_BITRATE));
            opus_encoder_ctl(g_opus, OPUS_SET_COMPLEXITY(5));
            /* VBR 保持默认开启：静音段近乎 0 字节，语音段约 16kbps。 */
          }
    }

    /* Tell the phone a recording session begins. Silently dropped if the
     * phone is not connected yet; the phone also keys off the SPP connection. */
    uint8_t ctrl = SPP_CTRL_SESSION_START;
    spp_send_frame(SPP_FRAME_TYPE_CONTROL, &ctrl, 1);

    /* The capture thread drives the SMF/media framework (media_recorder ->
     * codec init, ~150ms). That call chain needs a large stack: with the
     * default pthread stack (~6.5KB) it overflows and the ARM UsageFault
     * handler panics ("Stack Overflow" -> board_reset). Xiaozhi uses 32KB for
     * the very same media_recorder chain; mirror it here. */
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 32768);
    if (pthread_create(&g_capture_tid, &attr, spp_capture_thread, NULL) != 0)
      {
        printf("[spp_audio] pthread_create failed\n");
        pthread_attr_destroy(&attr);
        g_active = false;
        return;
      }
    pthread_attr_destroy(&attr);
    g_capture_tid_valid = true;

    printf("[spp_audio] stream started (media_recorder / MEDIA_SOURCE_MIC)\n");
}

void spp_audio_stream_stop(void)
{
    if (!g_active)
        return;

    /* Tell the phone the session ends (so it finalizes RTASR + summary). */
    uint8_t ctrl = SPP_CTRL_SESSION_STOP;
    spp_send_frame(SPP_FRAME_TYPE_CONTROL, &ctrl, 1);

    /* Stop the recorder first so any blocked read_data() in the capture thread
     * returns promptly, THEN join (avoids a hung join if the mic is silent). */
    pthread_mutex_lock(&g_mu);
    void *recorder = g_recorder;
    pthread_mutex_unlock(&g_mu);
    if (recorder != NULL)
        media_recorder_stop(recorder);

    g_active = false;
    if (g_capture_tid_valid)
      {
        pthread_join(g_capture_tid, NULL);
        g_capture_tid_valid = false;
      }
    g_amplitude = 0.0f;

    /* 编码并发送缓冲中剩余的不足 40ms 尾帧（补静音到 40ms），再销毁编码器。 */
    if (g_opus != NULL)
      {
        if (g_opus_pcm_len > 0)
          {
            memset((uint8_t *)g_opus_pcm + g_opus_pcm_len * 2, 0,
                   (OPUS_PACKET_SAMPLES - g_opus_pcm_len) * 2);
            uint8_t tail[OPUS_MAX_PACKET];
            opus_int32 n = opus_encode(g_opus, g_opus_pcm, OPUS_PACKET_SAMPLES,
                                       tail, OPUS_MAX_PACKET);
            if (n > 0)
                spp_send_frame(SPP_FRAME_TYPE_OPUS, tail, (uint16_t)n);
            g_opus_pcm_len = 0;
          }
        opus_encoder_destroy(g_opus);
        g_opus = NULL;
    }

    /* 关闭并收尾离线缓存文件：空文件（全程已连、未落盘）直接删；有内容则留待回传。
     * 必须放在尾帧编码之后，确保最后不足 40ms 的尾帧（若未连）也被缓存。 */
    if (g_offline_open && g_offline_fp) {
        fclose(g_offline_fp);
        g_offline_fp = NULL;
        g_offline_open = false;
        offline_store_set_writing(NULL);   /* 取消写入中标记 */
        struct stat st;
        char path[160];
        snprintf(path, sizeof(path), "%s/%s", OFFLINE_DIR, g_offline_name);
        if (stat(path, &st) == 0 && st.st_size == 0)
            offline_store_remove(g_offline_name);   /* 空文件无需保留 */
    }
    /* 若停录时已连，立即回传（含刚结束的、可能含离线数据的）文件。 */
    if (bt_transport_is_connected())
        offline_flush_all();

    printf("[spp_audio] stream stopped (VAD peak energy=%.0f floor=%.0f)\n",
           g_vad.max_energy, g_vad.noise_floor);
}

bool spp_audio_stream_is_active(void)
{
    return g_active;
}

float spp_audio_stream_get_amplitude(void)
{
    return g_amplitude;
}
