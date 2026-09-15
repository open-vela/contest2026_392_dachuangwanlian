/****************************************************************************
 * apps/examples/xiaozhi_voice/xiaozhi_audio.h
 *
 * Audio capture (mic -> Opus) and playback (Opus -> speaker) wrappers.
 * Encoding uses the Opus API directly (16kHz mono, VOIP mode, 60ms frames)
 * to produce standard-compliant Opus frames compatible with the xiaozhi
 * server. Decoding uses the Vela media framework (media_player) in buffer
 * mode.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_XIAOZHI_VOICE_XIAOZHI_AUDIO_H
#define __APPS_EXAMPLES_XIAOZHI_VOICE_XIAOZHI_AUDIO_H

#include <stddef.h>
#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Opaque handle -- actual struct lives in xiaozhi_audio.c. */
struct xiaozhi_audio_s;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Allocate and initialise the audio subsystem (creates Opus encoder).
 * Returns NULL on failure. */
struct xiaozhi_audio_s *xiaozhi_audio_init(void);

/* Open the recorder in PCM buffer mode (16kHz mono 16-bit), then start
 * capturing. The caller then reads Opus-encoded frames with
 * xiaozhi_audio_read_opus() which performs internal PCM->Opus encoding.
 */
int xiaozhi_audio_start_capture(struct xiaozhi_audio_s *a);

/* Read one Opus-encoded frame (up to maxlen bytes).  Blocks until a
 * frame is available.  Returns the frame length (>0) or a negative errno.
 */
ssize_t xiaozhi_audio_read_opus(struct xiaozhi_audio_s *a,
                                uint8_t *buf, size_t maxlen);

/* Stop the recorder. */
int xiaozhi_audio_stop_capture(struct xiaozhi_audio_s *a);

/* Reset the accumulated RMS energy. Call at Speak press, before capturing. */
void xiaozhi_audio_rms_reset(struct xiaozhi_audio_s *a);

/* Return the RMS amplitude (0..32767) of all PCM captured since the last
 * reset. Used to reject silent / accidental presses. */
int xiaozhi_audio_rms_get(struct xiaozhi_audio_s *a);

/* Open the player in buffer mode, 24kHz mono Opus, ready to receive
 * server TTS frames. */
int xiaozhi_audio_start_playback(struct xiaozhi_audio_s *a);

/* Feed one Opus frame (received from the server) to the player. */
int xiaozhi_audio_write_opus(struct xiaozhi_audio_s *a,
                             const uint8_t *data, size_t len);

/* Stop the player. */
int xiaozhi_audio_stop_playback(struct xiaozhi_audio_s *a);

/* Tear down recorder, player, and Opus encoder. */
void xiaozhi_audio_deinit(struct xiaozhi_audio_s *a);

#endif /* __APPS_EXAMPLES_XIAOZHI_VOICE_XIAOZHI_AUDIO_H */
