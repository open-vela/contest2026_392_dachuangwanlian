/****************************************************************************
 * apps/examples/xiaozhi_voice/xiaozhi_audio.c
 *
 * Audio capture (mic -> Opus 16kHz) and playback (Opus 24kHz -> speaker)
 * using direct Opus API for encoding and Vela media framework for decoding.
 * The SMF framework's Opus encoder adds non-standard headers that break
 * compatibility with the xiaozhi server, so we use the Opus API directly
 * for encoding. For playback, the SMF decoder works correctly.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include <opus.h>

#include <media_recorder.h>
#include <media_player.h>
#include <media_defs.h>

#include "xiaozhi_audio.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Uplink: 16kHz mono Opus encoding parameters (direct API). */
#define XZ_OPUS_SAMPLE_RATE  16000
#define XZ_OPUS_CHANNELS     1
#define XZ_OPUS_BITRATE      32000
#define XZ_OPUS_FRAME_MS     60
#define XZ_OPUS_APPLICATION  OPUS_APPLICATION_VOIP
#define XZ_OPUS_FRAME_SIZE   (XZ_OPUS_SAMPLE_RATE * XZ_OPUS_FRAME_MS / 1000)  /* 960 samples */

/* PCM capture options: raw 16-bit mono 16kHz from mic via SMF. */
#define XZ_PCM_RECORDER_OPTIONS \
  "fmt=[rate=#16000,ch=#1,bits=#16,width=#2],enc=[keys=pcm,imin=#1920]"

/* Downlink: 24kHz mono Opus (xiaozhi server TTS sample rate, SMF native format). */
#define XZ_PLAYER_OPTIONS \
  "oMediaScript=[codec=opus,rate=#24000,ch=#1,bits=#16]"

/* Max single Opus frame we expect / send. */
#define XZ_OPUS_FRAME_MAX 512

/* PCM read buffer size: one 60ms frame at 16kHz mono 16-bit = 1920 bytes. */
#define XZ_PCM_BUF_SIZE (XZ_OPUS_FRAME_SIZE * XZ_OPUS_CHANNELS * sizeof(opus_int16))

/****************************************************************************
 * Private Data
 ****************************************************************************/

struct xiaozhi_audio_s
{
  /* Recording path (mic -> PCM -> Opus encode -> send). */
  void             *recorder;       /* SMF recorder handle (void*). */
  OpusEncoder      *opus_encoder;   /* Direct Opus encoder. */
  opus_int16        pcm_buf[XZ_OPUS_FRAME_SIZE * XZ_OPUS_CHANNELS];
  uint8_t           opus_buf[XZ_OPUS_FRAME_MAX];
  pthread_mutex_t   enc_mutex;

  /* RMS energy accumulation for "did the user actually speak?" detection.
   * Reset with xiaozhi_audio_rms_reset() at press, drained at release. */
  uint64_t          rms_sq_sum;     /* sum of sample^2 */
  uint32_t          rms_samples;     /* count of samples accumulated */

  /* Playback path (recv Opus -> SMF decode -> speaker). */
  void             *player;         /* SMF player handle (void*). */
};

static struct xiaozhi_audio_s g_audio;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

struct xiaozhi_audio_s *xiaozhi_audio_init(void)
{
  struct xiaozhi_audio_s *a = &g_audio;
  int err;

  memset(a, 0, sizeof(*a));
  pthread_mutex_init(&a->enc_mutex, NULL);

  /* Create Opus encoder: 16kHz mono, VOIP application, 60ms frames. */
  a->opus_encoder = opus_encoder_create(XZ_OPUS_SAMPLE_RATE,
                                        XZ_OPUS_CHANNELS,
                                        XZ_OPUS_APPLICATION,
                                        &err);
  if (a->opus_encoder == NULL)
    {
      printf("[xiaozhi] opus_encoder_create failed: %s\n", opus_strerror(err));
      return NULL;
    }

  /* Configure encoder for xiaozhi compatibility. */
  opus_encoder_ctl(a->opus_encoder, OPUS_SET_BITRATE(XZ_OPUS_BITRATE));
  opus_encoder_ctl(a->opus_encoder, OPUS_SET_COMPLEXITY(0));
  opus_encoder_ctl(a->opus_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
  opus_encoder_ctl(a->opus_encoder, OPUS_SET_LSB_DEPTH(16));
  /* Force 60ms frame size. */
  opus_encoder_ctl(a->opus_encoder, OPUS_SET_PACKET_LOSS_PERC(0));

  return a;
}

int xiaozhi_audio_start_capture(struct xiaozhi_audio_s *a)
{
  int ret;

  if (a == NULL)
    {
      return -EINVAL;
    }

  /* Tear down any previous recorder. */
  if (a->recorder != NULL)
    {
      media_recorder_stop(a->recorder);
      media_recorder_close(a->recorder);
      a->recorder = NULL;
    }

  a->recorder = media_recorder_open(MEDIA_SOURCE_MIC);
  if (a->recorder == NULL)
    {
      printf("[xiaozhi] media_recorder_open failed\n");
      return -EIO;
    }

  /* Buffer mode: capture raw PCM. We encode to Opus ourselves. */
  ret = media_recorder_prepare(a->recorder, NULL, XZ_PCM_RECORDER_OPTIONS);
  if (ret < 0)
    {
      printf("[xiaozhi] recorder prepare failed: %d\n", ret);
      media_recorder_close(a->recorder);
      a->recorder = NULL;
      return ret;
    }

  ret = media_recorder_start(a->recorder);
  if (ret < 0)
    {
      printf("[xiaozhi] recorder start failed: %d\n", ret);
      media_recorder_close(a->recorder);
      a->recorder = NULL;
      return ret;
    }

  return 0;
}

ssize_t xiaozhi_audio_read_opus(struct xiaozhi_audio_s *a,
                                uint8_t *buf, size_t maxlen)
{
  ssize_t total = 0;
  ssize_t n;
  int encoded_bytes;
  int frame_samples;

  if (a == NULL || a->recorder == NULL || buf == NULL)
    {
      return -EINVAL;
    }

  /* Read exactly one 60ms PCM frame from the SMF recorder.
   * At 16kHz mono 16-bit, one 60ms frame = 960 samples = 1920 bytes.
   * media_recorder_read_data may return partial reads, so loop until
   * we have the full frame.
   */
  while (total < XZ_PCM_BUF_SIZE)
    {
      n = media_recorder_read_data(a->recorder,
                                   (uint8_t *)a->pcm_buf + total,
                                   XZ_PCM_BUF_SIZE - total);
      if (n <= 0)
        {
          return n > 0 ? -EIO : n;
        }

      total += n;
    }

  /* Encode PCM to Opus. opus_encode takes sample count, not byte count.
   * frame_samples = total_bytes / (channels * bytes_per_sample)
   *               = 1920 / (1 * 2) = 960 samples for 60ms at 16kHz.
   */
  frame_samples = total / (XZ_OPUS_CHANNELS * sizeof(opus_int16));

  /* Accumulate PCM energy for "did the user actually speak?" detection.
   * Sum of squares across the just-read frame's samples. */
  for (int i = 0; i < frame_samples; i++)
    {
      int64_t v = a->pcm_buf[i];
      a->rms_sq_sum += (uint64_t)(v * v);
    }
  a->rms_samples += frame_samples;

  encoded_bytes = opus_encode(a->opus_encoder,
                              a->pcm_buf,
                              frame_samples,
                              a->opus_buf,
                              XZ_OPUS_FRAME_MAX);
  if (encoded_bytes < 0)
    {
      printf("[xiaozhi] opus_encode error: %s (samples=%d)\n",
             opus_strerror(encoded_bytes), frame_samples);
      return -EIO;
    }

  /* Copy encoded frame to caller's buffer. */
  if ((size_t)encoded_bytes > maxlen)
    {
      printf("[xiaozhi] opus frame too large: %d > %zu\n", encoded_bytes, maxlen);
      return -ENOMEM;
    }

  memcpy(buf, a->opus_buf, encoded_bytes);

  return (ssize_t)encoded_bytes;
}

int xiaozhi_audio_stop_capture(struct xiaozhi_audio_s *a)
{
  if (a == NULL || a->recorder == NULL)
    {
      return -EINVAL;
    }

  media_recorder_stop(a->recorder);
  media_recorder_close(a->recorder);
  a->recorder = NULL;
  return 0;
}

void xiaozhi_audio_rms_reset(struct xiaozhi_audio_s *a)
{
  if (a == NULL)
    {
      return;
    }

  a->rms_sq_sum = 0;
  a->rms_samples = 0;
}

int xiaozhi_audio_rms_get(struct xiaozhi_audio_s *a)
{
  if (a == NULL || a->rms_samples == 0)
    {
      return 0;
    }

  /* rms = sqrt(mean(sample^2)). Use a 32-bit fixed-point sqrt to avoid
   * pulling in libm just for this. The mean fits in uint32 (max sample^2
   * is ~1.07e9, averaged over many samples). */

  uint64_t mean_sq = a->rms_sq_sum / a->rms_samples;
  if (mean_sq == 0)
    {
      return 0;
    }

  /* Integer Newton-Raphson sqrt on the 64-bit value. */
  uint64_t x = mean_sq;
  uint64_t y = (x + 1) / 2;
  while (y < x)
    {
      x = y;
      y = (x + mean_sq / x) / 2;
    }

  /* x is floor(sqrt(mean_sq)). Clamp to int16 range. */
  if (x > 32767)
    {
      x = 32767;
    }

  return (int)x;
}

int xiaozhi_audio_start_playback(struct xiaozhi_audio_s *a)
{
  int ret;

  if (a == NULL)
    {
      return -EINVAL;
    }

  if (a->player != NULL)
    {
      media_player_stop(a->player);
      media_player_close(a->player, 0);
      a->player = NULL;
    }

  a->player = media_player_open(MEDIA_STREAM_MUSIC);
  if (a->player == NULL)
    {
      printf("[xiaozhi] media_player_open failed\n");
      return -EIO;
    }

  /* Buffer mode: url = NULL. The framework decodes Opus; we feed encoded
   * frames via media_player_write_data().
   */
  ret = media_player_prepare(a->player, NULL, XZ_PLAYER_OPTIONS);
  if (ret < 0)
    {
      printf("[xiaozhi] player prepare failed: %d\n", ret);
      media_player_close(a->player, 0);
      a->player = NULL;
      return ret;
    }

  ret = media_player_start(a->player);
  if (ret < 0)
    {
      printf("[xiaozhi] player start failed: %d\n", ret);
      media_player_close(a->player, 0);
      a->player = NULL;
      return ret;
    }

  return 0;
}

int xiaozhi_audio_write_opus(struct xiaozhi_audio_s *a,
                             const uint8_t *data, size_t len)
{
  ssize_t ret;

  if (a == NULL || a->player == NULL || data == NULL)
    {
      return -EINVAL;
    }

  ret = media_player_write_data(a->player, data, len);
  if (ret < 0)
    {
      printf("[xiaozhi] player write_data failed: %zd\n", ret);
      return ret;
    }

  return 0;
}

int xiaozhi_audio_stop_playback(struct xiaozhi_audio_s *a)
{
  if (a == NULL || a->player == NULL)
    {
      return -EINVAL;
    }

  media_player_stop(a->player);
  media_player_close(a->player, 0);
  a->player = NULL;
  return 0;
}

void xiaozhi_audio_deinit(struct xiaozhi_audio_s *a)
{
  if (a == NULL)
    {
      return;
    }

  if (a->recorder != NULL)
    {
      media_recorder_stop(a->recorder);
      media_recorder_close(a->recorder);
      a->recorder = NULL;
    }

  if (a->player != NULL)
    {
      media_player_stop(a->player);
      media_player_close(a->player, 0);
      a->player = NULL;
    }

  if (a->opus_encoder != NULL)
    {
      opus_encoder_destroy(a->opus_encoder);
      a->opus_encoder = NULL;
    }

  pthread_mutex_destroy(&a->enc_mutex);
}
