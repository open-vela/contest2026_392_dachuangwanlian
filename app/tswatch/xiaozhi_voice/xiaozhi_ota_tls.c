/****************************************************************************
 * apps/examples/xiaozhi_voice/xiaozhi_ota_tls.c
 *
 * mbedtls-backed webclient_tls_ops: lets the webclient (used by the OTA
 * POST) speak https://. Without this, webclient sees tls_ops==NULL for an
 * "https" URL and rejects it with -ENOTSUP.
 *
 * connect():  resolve host, TCP-connect, run the mbedtls handshake.
 *              Returns an opaque webclient_tls_connection* (our struct).
 * send/recv:   wrap mbedtls_ssl_write / mbedtls_ssl_read, mapping the
 *              MBEDTLS_ERR_SSL_WANT_* retry codes to -EAGAIN for webclient.
 * get_poll_info: hand back the underlying TCP fd for poll-based waits.
 * close():     tear down TLS + socket.
 *
 * Server certificate verification is skipped (MBEDTLS_VERIFY_NONE) to keep
 * the embedded build simple; the OTA response carries only a token, and the
 * subsequent wss connection (xiaozhi_ws.c) is the real authenticated channel.
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

#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/net_sockets.h>

#include <netutils/webclient.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* The webclient_tls_connection the TLS ops hand back to webclient. */
struct xz_tls_conn_s
{
  int                      fd;       /* underlying TCP socket */
  mbedtls_ssl_context      ssl;
  mbedtls_ssl_config       conf;
  mbedtls_entropy_context  entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* BIO send/recv for mbedtls over the plain TCP fd. */
static int xz_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
  struct xz_tls_conn_s *c = (struct xz_tls_conn_s *)ctx;
  ssize_t ret = send(c->fd, buf, len, 0);
  if (ret < 0)
    {
      return MBEDTLS_ERR_NET_SEND_FAILED;
    }

  return (int)ret;
}

static int xz_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
  struct xz_tls_conn_s *c = (struct xz_tls_conn_s *)ctx;
  ssize_t ret = recv(c->fd, buf, len, 0);
  if (ret < 0)
    {
      return MBEDTLS_ERR_NET_RECV_FAILED;
    }

  if (ret == 0)
    {
      return MBEDTLS_ERR_SSL_WANT_READ;
    }

  return (int)ret;
}

/* connect: DNS -> socket -> TCP connect -> TLS handshake.
 * Matches the webclient_tls_ops::connect signature.
 */
static int xz_tls_connect(FAR void *ctx, FAR const char *hostname,
                          FAR const char *port, unsigned int timeout_second,
                          FAR struct webclient_tls_connection **connp)
{
  struct xz_tls_conn_s *c;
  struct hostent *he;
  struct sockaddr_in server;
  int portnum = atoi(port);
  int ret;

  (void)ctx;

  /* Use a default timeout of 10 seconds if caller didn't specify one. */

  if (timeout_second == 0)
    {
      timeout_second = 10;
    }

  c = calloc(1, sizeof(*c));
  if (c == NULL)
    {
      return -ENOMEM;
    }

  c->fd = -1;

  /* --- DNS --- */
  he = gethostbyname(hostname);
  if (he == NULL)
    {
      printf("[xiaozhi] ota-tls: DNS failed for %s\n", hostname);
      ret = -EHOSTUNREACH;
      goto err_free;
    }

  /* --- TCP socket + connect --- */
  c->fd = socket(AF_INET, SOCK_STREAM, 0);
  if (c->fd < 0)
    {
      printf("[xiaozhi] ota-tls: socket failed: %d\n", errno);
      ret = -EIO;
      goto err_free;
    }

  /* Set send/recv timeouts so connect and I/O don't block forever. */

  {
    struct timeval tv;
    tv.tv_sec  = timeout_second;
    tv.tv_usec = 0;
    setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(c->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  }

  memset(&server, 0, sizeof(server));
  server.sin_family = AF_INET;
  server.sin_port   = htons(portnum);
  memcpy(&server.sin_addr, he->h_addr_list[0], he->h_length);

  if (connect(c->fd, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
      printf("[xiaozhi] ota-tls: TCP connect failed: %d\n", errno);
      ret = -ECONNREFUSED;
      goto err_sock;
    }

  /* --- mbedtls setup --- */
  mbedtls_ssl_init(&c->ssl);
  mbedtls_ssl_config_init(&c->conf);
  mbedtls_entropy_init(&c->entropy);
  mbedtls_ctr_drbg_init(&c->ctr_drbg);

  ret = mbedtls_ctr_drbg_seed(&c->ctr_drbg, mbedtls_entropy_func,
                              &c->entropy,
                              (const unsigned char *)"xiaozhi", 7);
  if (ret != 0)
    {
      printf("[xiaozhi] ota-tls: drbg seed failed: -0x%x\n", -ret);
      ret = -EIO;
      goto err_tls;
    }

  ret = mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
  if (ret != 0)
    {
      printf("[xiaozhi] ota-tls: ssl config failed: -0x%x\n", -ret);
      ret = -EIO;
      goto err_tls;
    }

  mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_NONE);
  mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &c->ctr_drbg);

  ret = mbedtls_ssl_setup(&c->ssl, &c->conf);
  if (ret != 0)
    {
      printf("[xiaozhi] ota-tls: ssl setup failed: -0x%x\n", -ret);
      ret = -EIO;
      goto err_tls;
    }

  /* SNI so the server serves the right cert. */
  mbedtls_ssl_set_hostname(&c->ssl, hostname);
  mbedtls_ssl_set_bio(&c->ssl, c, xz_bio_send, xz_bio_recv, NULL);

  /* --- TLS handshake --- */
  {
    int hs_retries = 0;
    int hs_max = (int)timeout_second * 2; /* ~500ms per retry */

    while ((ret = mbedtls_ssl_handshake(&c->ssl)) != 0)
      {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
            ret != MBEDTLS_ERR_SSL_WANT_WRITE)
          {
            printf("[xiaozhi] ota-tls: handshake failed: -0x%x\n", -ret);
            ret = -EIO;
            goto err_tls;
          }

        if (++hs_retries >= hs_max)
          {
            printf("[xiaozhi] ota-tls: handshake timeout (%ds)\n",
                   timeout_second);
            ret = -ETIMEDOUT;
            goto err_tls;
          }

        usleep(500000); /* 500ms between retries */
      }
  }

  *connp = (struct webclient_tls_connection *)c;
  return 0;

err_tls:
  mbedtls_ssl_free(&c->ssl);
  mbedtls_ssl_config_free(&c->conf);
  mbedtls_ctr_drbg_free(&c->ctr_drbg);
  mbedtls_entropy_free(&c->entropy);

err_sock:
  if (c->fd >= 0)
    {
      close(c->fd);
    }

err_free:
  free(c);
  return ret;
}

static ssize_t xz_tls_send(FAR void *ctx,
                           FAR struct webclient_tls_connection *conn,
                           FAR const void *buf, size_t len)
{
  struct xz_tls_conn_s *c = (struct xz_tls_conn_s *)conn;
  int ret;
  (void)ctx;

  ret = mbedtls_ssl_write(&c->ssl, (const unsigned char *)buf, len);
  if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
      ret == MBEDTLS_ERR_SSL_WANT_WRITE)
    {
      return -EAGAIN;
    }

  if (ret < 0)
    {
      printf("[xiaozhi] ota-tls: send err -0x%x (len=%zu)\n", -ret, len);
      return -EIO;
    }

  return ret;
}

static ssize_t xz_tls_recv(FAR void *ctx,
                            FAR struct webclient_tls_connection *conn,
                            FAR void *buf, size_t len)
{
  struct xz_tls_conn_s *c = (struct xz_tls_conn_s *)conn;
  int ret;
  (void)ctx;

  ret = mbedtls_ssl_read(&c->ssl, (unsigned char *)buf, len);
  if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
      ret == MBEDTLS_ERR_SSL_WANT_WRITE)
    {
      /* No data yet on a blocking socket — shouldn't normally happen
       * mid-response, but if it does, surface it so webclient can poll.
       */
      return -EAGAIN;
    }

  if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == 0)
    {
      /* Server sent a clean TLS close. Distinguish "no response at all"
       * (server hung up before sending anything) from a post-data close.
       */
      printf("[xiaozhi] ota-tls: peer closed (close_notify)\n");
      return 0;   /* let webclient treat as clean EOF, not a hard reset */
    }

  if (ret < 0)
    {
      printf("[xiaozhi] ota-tls: recv err -0x%x\n", -ret);
      return -EIO;
    }

  return ret;
}

static int xz_tls_close(FAR void *ctx,
                         FAR struct webclient_tls_connection *conn)
{
  struct xz_tls_conn_s *c = (struct xz_tls_conn_s *)conn;
  (void)ctx;

  if (c == NULL)
    {
      return 0;
    }

  mbedtls_ssl_close_notify(&c->ssl);
  mbedtls_ssl_free(&c->ssl);
  mbedtls_ssl_config_free(&c->conf);
  mbedtls_ctr_drbg_free(&c->ctr_drbg);
  mbedtls_entropy_free(&c->entropy);

  if (c->fd >= 0)
    {
      close(c->fd);
    }

  free(c);
  return 0;
}

static int xz_tls_get_poll_info(FAR void *ctx,
                                 FAR struct webclient_tls_connection *conn,
                                 FAR struct webclient_poll_info *info)
{
  struct xz_tls_conn_s *c = (struct xz_tls_conn_s *)conn;
  (void)ctx;

  info->fd = c->fd;
  info->flags = 0;
  return 0;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* The TLS ops vector plugged into webclient_context.tls_ops by the OTA
 * caller. init_connection is left NULL (we don't tunnel through an http
 * proxy); webclient only needs it for https-over-proxy, which we never use.
 */
const struct webclient_tls_ops g_xz_tls_ops =
{
  .connect        = xz_tls_connect,
  .send           = xz_tls_send,
  .recv           = xz_tls_recv,
  .close          = xz_tls_close,
  .get_poll_info  = xz_tls_get_poll_info,
  .init_connection = NULL,
};
