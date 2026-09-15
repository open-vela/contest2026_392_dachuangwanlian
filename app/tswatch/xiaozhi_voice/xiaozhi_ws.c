/****************************************************************************
 * apps/examples/xiaozhi_voice/xiaozhi_ws.c
 *
 * A lightweight WebSocket-over-TLS (wss) client.
 *   - TLS layer: mbedtls (pattern borrowed from packages/demos/mimo)
 *   - WebSocket framing: RFC 6455
 *
 * Supports the subset needed by the xiaozhi protocol:
 *   - client->server text(0x1) and binary(0x2) frames, masked
 *   - server->client text/binary/close frames, unmasked
 *   - payload lengths up to 65535 (16-bit length); 64-bit length not needed
 *     here (Opus frames and JSON messages are short)
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "xiaozhi_ws.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define WS_RXBUF_SIZE  4096

/* WebSocket opcodes */
#define WS_OPCODE_CONTINUATION 0x0
#define WS_OPCODE_TEXT         0x1
#define WS_OPCODE_BINARY       0x2
#define WS_OPCODE_CLOSE        0x8
#define WS_OPCODE_PING         0x9
#define WS_OPCODE_PONG         0xa

#define WS_FIN_BIT   0x80
#define WS_MASK_BIT  0x80

/* Largest single payload buffered for masking before a TLS write.
 * Covers Opus frames (<= ~400B); large JSON frames spill to a slow path. */
#define XZ_WS_PAYLOAD_MAX 1024

/****************************************************************************
 * Private Functions - TLS
 ****************************************************************************/

/* BIO send: write to the plain TCP fd. */
static int ws_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
  int fd = *(int *)ctx;
  ssize_t ret = send(fd, buf, len, 0);
  if (ret < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
          return MBEDTLS_ERR_SSL_WANT_WRITE;
        }
      return -EIO;
    }

  return (int)ret;
}

/* BIO recv: read from the plain TCP fd. */
static int ws_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
  int fd = *(int *)ctx;
  ssize_t ret = recv(fd, buf, len, 0);
  if (ret < 0)
    {
      /* EAGAIN/EWOULDBLOCK means the socket recv timed out (SO_RCVTIMEO).
       * Tell mbedtls to retry instead of treating it as a fatal error. */
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
          return MBEDTLS_ERR_SSL_WANT_READ;
        }
      return -EIO;
    }

  if (ret == 0)
    {
      return MBEDTLS_ERR_SSL_WANT_READ;
    }

  return (int)ret;
}

static int ws_tls_init(struct xiaozhi_ws_s *ws)
{
  int ret;

  mbedtls_ssl_init(&ws->ssl);
  mbedtls_ssl_config_init(&ws->conf);
  mbedtls_entropy_init(&ws->entropy);
  mbedtls_ctr_drbg_init(&ws->ctr_drbg);

  ret = mbedtls_ctr_drbg_seed(&ws->ctr_drbg, mbedtls_entropy_func,
                              &ws->entropy,
                              (const unsigned char *)"xiaozhi", 7);
  if (ret != 0)
    {
      printf("[xiaozhi] drbg seed failed: -0x%x\n", -ret);
      return -EIO;
    }

  ret = mbedtls_ssl_config_defaults(&ws->conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
  if (ret != 0)
    {
      printf("[xiaozhi] ssl config defaults failed: -0x%x\n", -ret);
      return -EIO;
    }

  /* Skip server cert verification for embedded use.
   * For production, configure CA certs via mbedtls_ssl_conf_ca_chain().
   */
  mbedtls_ssl_conf_authmode(&ws->conf, MBEDTLS_SSL_VERIFY_NONE);
  mbedtls_ssl_conf_rng(&ws->conf, mbedtls_ctr_drbg_random, &ws->ctr_drbg);

  ret = mbedtls_ssl_setup(&ws->ssl, &ws->conf);
  if (ret != 0)
    {
      printf("[xiaozhi] ssl setup failed: -0x%x\n", -ret);
      return -EIO;
    }

  return 0;
}

/* Connect TCP, do TLS handshake over it. */
static int ws_tls_connect(struct xiaozhi_ws_s *ws, const char *host,
                         const char *port)
{
  struct hostent *he;
  struct sockaddr_in server;
  int portnum = atoi(port);
  int ret;

  he = gethostbyname(host);
  if (he == NULL)
    {
      printf("[xiaozhi] DNS resolve failed for %s\n", host);
      return -EIO;
    }

  ws->fd = socket(AF_INET, SOCK_STREAM, 0);
  if (ws->fd < 0)
    {
      printf("[xiaozhi] socket() failed: %d\n", errno);
      return -EIO;
    }

  memset(&server, 0, sizeof(server));
  server.sin_family = AF_INET;
  server.sin_port   = htons(portnum);
  memcpy(&server.sin_addr, he->h_addr_list[0], he->h_length);

  if (connect(ws->fd, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
      printf("[xiaozhi] TCP connect failed: %d\n", errno);
      close(ws->fd);
      ws->fd = -1;
      return -EIO;
    }

  /* Set socket send/recv timeouts to prevent blocking forever.
   * Use shorter timeout (2s) so app_running check can interrupt faster. */

  struct timeval tv;
  tv.tv_sec = 2;
  tv.tv_usec = 0;
  setsockopt(ws->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(ws->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  ret = mbedtls_ssl_set_hostname(&ws->ssl, host);
  if (ret != 0)
    {
      printf("[xiaozhi] set hostname failed: -0x%x\n", -ret);
      goto err;
    }

  mbedtls_ssl_set_bio(&ws->ssl, &ws->fd, ws_bio_send, ws_bio_recv, NULL);

  int hs_retries = 0;
  while ((ret = mbedtls_ssl_handshake(&ws->ssl)) != 0)
    {
      if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
          ret != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
          printf("[xiaozhi] TLS handshake failed: -0x%x\n", -ret);
          goto err;
        }
      if (++hs_retries >= 20)
        {
          printf("[xiaozhi] TLS handshake timeout\n");
          goto err;
        }
      usleep(500000);
    }

  return 0;

err:
  close(ws->fd);
  ws->fd = -1;
  return -EIO;
}

/****************************************************************************
 * Private Functions - TLS read/write wrappers
 ****************************************************************************/

/* Send all bytes over TLS. Returns 0 on success, -errno on failure. */
static int ws_tls_write_all(struct xiaozhi_ws_s *ws,
                            const uint8_t *buf, size_t len)
{
  size_t written = 0;
  while (written < len)
    {
      int ret = mbedtls_ssl_write(&ws->ssl, buf + written, len - written);
      if (ret == MBEDTLS_ERR_SSL_WANT_WRITE ||
          ret == MBEDTLS_ERR_SSL_WANT_READ)
        {
          continue;
        }

      if (ret <= 0)
        {
          printf("[xiaozhi] tls write error: -0x%x\n", -ret);
          return -EIO;
        }

      written += ret;
    }

  return 0;
}

/* Read exactly n bytes over TLS into buf. Returns 0 on success, -errno. */
static int ws_tls_read_all(struct xiaozhi_ws_s *ws, uint8_t *buf, size_t n)
{
  size_t got = 0;
  while (got < n)
    {
      int ret = mbedtls_ssl_read(&ws->ssl, buf + got, n - got);
      if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
          ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
          continue;
        }

      if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == 0)
        {
          printf("[xiaozhi] tls peer closed during read\n");
          return -ECONNRESET;
        }

      if (ret < 0)
        {
          printf("[xiaozhi] tls read error: -0x%x\n", -ret);
          return -EIO;
        }

      got += ret;
    }

  return 0;
}

/****************************************************************************
 * Private Functions - WebSocket framing
 ****************************************************************************/

/* Generate a random 4-byte masking key. mbedtls_ctr_drbg_random is used as
 * a random source (no Date.now/random needed).
 */
static void ws_make_mask(uint8_t mask[4], struct xiaozhi_ws_s *ws)
{
  mbedtls_ctr_drbg_random(&ws->ctr_drbg, mask, 4);
}

/* Send one client->server frame (masked). */
static int ws_send_frame(struct xiaozhi_ws_s *ws, uint8_t opcode,
                         const uint8_t *data, size_t len)
{
  uint8_t hdr[14];
  size_t hdrlen;
  uint8_t mask[4];
  int ret;

  hdr[0] = WS_FIN_BIT | opcode;

  if (len <= 125)
    {
      hdr[1] = WS_MASK_BIT | (uint8_t)len;
      hdrlen = 2;
    }
  else if (len <= 65535)
    {
      hdr[1] = WS_MASK_BIT | 126;
      hdr[2] = (len >> 8) & 0xff;
      hdr[3] = len & 0xff;
      hdrlen = 4;
    }
  else
    {
      /* 64-bit length path. Opus/JSON frames are never this large, but
       * implement for completeness.
       */
      hdr[1] = WS_MASK_BIT | 127;
      uint64_t l = len;
      hdr[2] = 0; hdr[3] = 0; hdr[4] = 0; hdr[5] = 0;
      hdr[6] = (l >> 24) & 0xff;
      hdr[7] = (l >> 16) & 0xff;
      hdr[8] = (l >> 8) & 0xff;
      hdr[9] = l & 0xff;
      hdrlen = 10;
    }

  ret = ws_tls_write_all(ws, hdr, hdrlen);
  if (ret != 0)
    {
      return ret;
    }

  /* Masking key + masked payload. */
  ws_make_mask(mask, ws);
  ret = ws_tls_write_all(ws, mask, 4);
  if (ret != 0)
    {
      return ret;
    }

  if (len > 0)
    {
      /* Mask the whole payload into a local buffer, then send in a single
       * TLS write. Doing one mbedtls_ssl_write() per byte (the old code)
       * made every Opus frame cost hundreds of syscalls and was the main
       * cause of mic DMA buffer overruns / dropped frames. */
      uint8_t masked[XZ_WS_PAYLOAD_MAX];
      size_t cap = len < sizeof(masked) ? len : sizeof(masked);

      for (size_t i = 0; i < cap; i++)
        {
          masked[i] = data[i] ^ mask[i & 3];
        }

      ret = ws_tls_write_all(ws, masked, cap);
      if (ret != 0)
        {
          return ret;
        }

      /* Payloads larger than the local cap (only large JSON frames) are
       * sent in a second chunk. Opus frames never hit this path. */
      if (cap < len)
        {
          uint8_t one;
          for (size_t i = cap; i < len; i++)
            {
              one = data[i] ^ mask[i & 3];
              ret = ws_tls_write_all(ws, &one, 1);
              if (ret != 0)
                {
                  return ret;
                }
            }
        }
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int xiaozhi_ws_connect(struct xiaozhi_ws_s *ws, const char *ws_url,
                       const char *token, const char *device_id,
                       const char *client_id)
{
  char host[128];
  char path[256];
  char pathwithtok[320];   /* path + ?token=<token> for the GET line */
  char portstr[8];
  const char *p;
  const char *hoststart;
  const char *hostend;
  const char *pathstart;
  int port = 443;
  int ret;
  uint8_t wskey[16];
  char req[768];

  memset(ws, 0, sizeof(*ws));
  ws->fd = -1;

  /* Parse "wss://host[:port]/path..." */
  if (strncmp(ws_url, "wss://", 6) != 0)
    {
      printf("[xiaozhi] ws url must be wss://: %s\n", ws_url);
      return -EINVAL;
    }

  hoststart = ws_url + 6;
  pathstart = strchr(hoststart, '/');
  hostend   = pathstart ? pathstart : hoststart + strlen(hoststart);

  /* Detect port in host portion */
  p = memchr(hoststart, ':', hostend - hoststart);
  if (p != NULL)
    {
      size_t hlen = p - hoststart;
      memcpy(host, hoststart, hlen);
      host[hlen] = '\0';
      port = atoi(p + 1);
    }
  else
    {
      size_t hlen = hostend - hoststart;
      memcpy(host, hoststart, hlen);
      host[hlen] = '\0';
    }

  snprintf(portstr, sizeof(portstr), "%d", port);

  if (pathstart)
    {
      snprintf(path, sizeof(path), "%s", pathstart);
    }
  else
    {
      path[0] = '/'; path[1] = '\0';
    }

  /* The xiaozhi server expects the token both as a query string on the GET
   * line (?token=<token>) and as an Authorization: Bearer header. The
   * working Python probe builds "{ws_url}?token={ws_token}". Append it
   * here when the server's URL doesn't already carry one.
   */
  if (token != NULL && token[0] != '\0' && strchr(path, '?') == NULL)
    {
      snprintf(pathwithtok, sizeof(pathwithtok), "%s?token=%s", path, token);
    }
  else
    {
      snprintf(pathwithtok, sizeof(pathwithtok), "%s", path);
    }

  /* Allocate rx buffer. */
  ws->rxbuf_cap = WS_RXBUF_SIZE;
  ws->rxbuf = malloc(ws->rxbuf_cap);
  if (ws->rxbuf == NULL)
    {
      return -ENOMEM;
    }

  /* TLS init + connect. */
  ret = ws_tls_init(ws);
  if (ret != 0)
    {
      return ret;
    }

  ret = ws_tls_connect(ws, host, portstr);
  if (ret != 0)
    {
      return ret;
    }

  /* WebSocket HTTP Upgrade handshake. */
  mbedtls_ctr_drbg_random(&ws->ctr_drbg, wskey, 16);

  /* Build Sec-WebSocket-Key (base64 of 16 random bytes). We use a tiny
   * base64 encoder below.
   */
  char wskey_b64[32];
  {
    const char b64[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0;
    int i;
    for (i = 0; i < 16; i += 3)
      {
        uint32_t v = (wskey[i] << 16);
        if (i + 1 < 16) v |= (wskey[i + 1] << 8);
        if (i + 2 < 16) v |= wskey[i + 2];
        wskey_b64[o++] = b64[(v >> 18) & 0x3f];
        wskey_b64[o++] = b64[(v >> 12) & 0x3f];
        wskey_b64[o++] = (i + 1 < 16) ? b64[(v >> 6) & 0x3f] : '=';
        wskey_b64[o++] = (i + 2 < 16) ? b64[v & 0x3f] : '=';
      }

    wskey_b64[o] = '\0';
  }

  snprintf(req, sizeof(req),
    "GET %s HTTP/1.1\r\n"
    "Host: %s\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Key: %s\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "Authorization: Bearer %s\r\n"
    "Device-Id: %s\r\n"
    "Client-Id: %s\r\n"
    "Protocol-Version: 3\r\n"
    "\r\n",
    pathwithtok, host, wskey_b64, token, device_id, client_id);

  ret = ws_tls_write_all(ws, (const uint8_t *)req, strlen(req));
  if (ret != 0)
    {
      return ret;
    }

  /* Read the HTTP response line + headers until blank line. */
  {
    char line[256];
    int got101 = 0;

    /* Read line-by-line until \r\n\r\n */
    for (;;)
      {
        size_t li = 0;
        char prev = 0;

        while (li < sizeof(line) - 1)
          {
            uint8_t b;
            ret = ws_tls_read_all(ws, &b, 1);
            if (ret != 0)
              {
                return ret;
              }

            line[li] = (char)b;
            if (line[li] == '\n' && prev == '\r')
              {
                break;
              }

            prev = line[li];
            li++;
          }

        line[li] = '\0';

        /* Strip trailing \r\n */
        while (li > 0 && (line[li - 1] == '\r' || line[li - 1] == '\n'))
          {
            line[--li] = '\0';
          }

        if (li == 0)
          {
            break;  /* blank line: end of headers */
          }

        if (strstr(line, "101") != NULL && !got101)
          {
            got101 = 1;
          }
      }

    if (!got101)
      {
        return -EIO;
      }
  }

  ws->connected = true;
  return 0;
}

int xiaozhi_ws_send_text(struct xiaozhi_ws_s *ws, const char *data,
                         size_t len)
{
  if (!ws->connected)
    {
      return -ENOTCONN;
    }

  return ws_send_frame(ws, WS_OPCODE_TEXT, (const uint8_t *)data, len);
}

int xiaozhi_ws_send_binary(struct xiaozhi_ws_s *ws,
                           const uint8_t *data, size_t len)
{
  if (!ws->connected)
    {
      return -ENOTCONN;
    }

  return ws_send_frame(ws, WS_OPCODE_BINARY, data, len);
}

int xiaozhi_ws_recv(struct xiaozhi_ws_s *ws, const uint8_t **payload,
                    size_t *plen, bool *is_binary)
{
  uint8_t hdr[2];
  uint8_t mask[4];
  uint8_t opcode;
  bool masked;
  uint64_t plen64;
  int ret;

  if (!ws->connected)
    {
      return -ENOTCONN;
    }

  /* Read the first 2 bytes of the frame header. */
  ret = ws_tls_read_all(ws, hdr, 2);
  if (ret != 0)
    {
      return ret;
    }

  opcode = hdr[0] & 0x0f;
  masked = (hdr[1] & WS_MASK_BIT) != 0;
  plen64 = hdr[1] & 0x7f;

  if (plen64 == 126)
    {
      uint8_t ext[2];
      ret = ws_tls_read_all(ws, ext, 2);
      if (ret != 0)
        {
          return ret;
        }

      plen64 = ((uint64_t)ext[0] << 8) | ext[1];
    }
  else if (plen64 == 127)
    {
      uint8_t ext[8];
      ret = ws_tls_read_all(ws, ext, 8);
      if (ret != 0)
        {
          return ret;
        }

      plen64 = 0;
      for (int i = 0; i < 8; i++)
        {
          plen64 = (plen64 << 8) | ext[i];
        }
    }

  if (masked)
    {
      ret = ws_tls_read_all(ws, mask, 4);
      if (ret != 0)
        {
          return ret;
        }
    }

  if (plen64 > ws->rxbuf_cap)
    {
      printf("[xiaozhi] frame too large: %llu\n",
             (unsigned long long)plen64);
      return -EMSGSIZE;
    }

  /* Read the payload. */
  if (plen64 > 0)
    {
      ret = ws_tls_read_all(ws, ws->rxbuf, plen64);
      if (ret != 0)
        {
          return ret;
        }

      if (masked)
        {
          for (uint64_t i = 0; i < plen64; i++)
            {
              ws->rxbuf[i] ^= mask[i & 3];
            }
        }
    }

  /* Handle control frames transparently. */
  if (opcode == WS_OPCODE_CLOSE)
    {
      printf("[xiaozhi] server sent close frame\n");
      ws->connected = false;
      return -ECONNRESET;
    }

  if (opcode == WS_OPCODE_PING)
    {
      /* Echo as pong. */
      ws_send_frame(ws, WS_OPCODE_PONG, ws->rxbuf, plen64);
      return xiaozhi_ws_recv(ws, payload, plen, is_binary);
    }

  if (opcode == WS_OPCODE_PONG)
    {
      /* Ignore. */
      return xiaozhi_ws_recv(ws, payload, plen, is_binary);
    }

  /* Text or binary. */
  *payload    = ws->rxbuf;
  *plen       = plen64;
  *is_binary  = (opcode == WS_OPCODE_BINARY);
  return 0;
}

void xiaozhi_ws_close(struct xiaozhi_ws_s *ws)
{
  if (ws->connected)
    {
      uint8_t close_frame[] = { 0x88, 0x00 };
      ws_tls_write_all(ws, close_frame, 2);
    }

  mbedtls_ssl_close_notify(&ws->ssl);
  mbedtls_ssl_free(&ws->ssl);
  mbedtls_ssl_config_free(&ws->conf);
  mbedtls_ctr_drbg_free(&ws->ctr_drbg);
  mbedtls_entropy_free(&ws->entropy);

  if (ws->fd >= 0)
    {
      close(ws->fd);
      ws->fd = -1;
    }

  if (ws->rxbuf)
    {
      free(ws->rxbuf);
      ws->rxbuf = NULL;
    }

  ws->connected = false;
}
