/****************************************************************************
 * apps/examples/xiaozhi_voice/xiaozhi_ws.h
 *
 * A lightweight WebSocket-over-TLS (wss) client for the xiaozhi protocol.
 * TLS via mbedtls; WebSocket framing per RFC 6455.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_XIAOZHI_VOICE_XIAOZHI_WS_H
#define __APPS_EXAMPLES_XIAOZHI_VOICE_XIAOZHI_WS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct xiaozhi_ws_s
{
  /* TLS layer (mbedtls) */
  mbedtls_ssl_context      ssl;
  mbedtls_ssl_config       conf;
  mbedtls_entropy_context  entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
  int                      fd;       /* underlying TCP socket */

  bool                     connected; /* wss handshake completed */

  /* Received-frame reassembly */
  uint8_t                 *rxbuf;
  size_t                   rxbuf_cap;
  size_t                   rxbuf_len;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: xiaozhi_ws_connect
 *
 * Description:
 *   Connect to a wss:// URL, perform the TLS handshake and the WebSocket
 *   HTTP Upgrade handshake, carrying the given custom headers:
 *     Authorization: Bearer <token>
 *     Device-Id: <device_id>
 *     Client-Id: <client_id>
 *     Protocol-Version: 1
 *
 *   The path and host are parsed from ws_url. token is appended as
 *   ?token=<token> if not already present.
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 ****************************************************************************/

int xiaozhi_ws_connect(struct xiaozhi_ws_s *ws, const char *ws_url,
                       const char *token, const char *device_id,
                       const char *client_id);

/****************************************************************************
 * Name: xiaozhi_ws_send_text
 *
 * Description: Send a WebSocket text frame (opcode 0x1). The client->server
 *              frames are masked per RFC 6455.
 ****************************************************************************/

int xiaozhi_ws_send_text(struct xiaozhi_ws_s *ws, const char *data, size_t len);

/****************************************************************************
 * Name: xiaozhi_ws_send_binary
 *
 * Description: Send a WebSocket binary frame (opcode 0x2). Used for Opus
 *              audio uplink.
 ****************************************************************************/

int xiaozhi_ws_send_binary(struct xiaozhi_ws_s *ws,
                           const uint8_t *data, size_t len);

/****************************************************************************
 * Name: xiaozhi_ws_recv
 *
 * Description:
 *   Block until one complete WebSocket frame is received from the server.
 *   Writes a pointer to the payload into *payload and its length into
 *   *plen, and reports whether it was binary (true) or text (false) via
 *   *is_binary. The payload buffer is owned by the ws object and remains
 *   valid only until the next xiaozhi_ws_recv call.
 *
 *   Returns 0 on success, negative errno on failure/connection-close.
 ****************************************************************************/

int xiaozhi_ws_recv(struct xiaozhi_ws_s *ws, const uint8_t **payload,
                    size_t *plen, bool *is_binary);

/****************************************************************************
 * Name: xiaozhi_ws_close
 *
 * Description: Send a close frame and tear down TLS + socket.
 ****************************************************************************/

void xiaozhi_ws_close(struct xiaozhi_ws_s *ws);

#endif /* __APPS_EXAMPLES_XIAOZHI_VOICE_XIAOZHI_WS_H */
