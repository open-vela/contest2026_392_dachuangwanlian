/****************************************************************************
 * apps/examples/xiaozhi_voice/xiaozhi_ota.c
 *
 * Xiaozhi OTA: POST https://api.tenclass.net/xiaozhi/ota/ with Device-Id,
 * parse the JSON response to extract websocket.url and websocket.token.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <netutils/webclient.h>
#include <netutils/cJSON.h>

#include "xiaozhi_ota.h"

/* TLS ops backed by mbedtls (xiaozhi_ota_tls.c). The webclient refuses
 * "https" URLs with -ENOTSUP unless a tls_ops vector is provided.
 */
extern const struct webclient_tls_ops g_xz_tls_ops;

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define OTA_URL "https://api.tenclass.net/xiaozhi/ota/"
#define OTA_BUF_SIZE  4096

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Context passed to the sink callback to accumulate the response body. */

struct ota_sink_ctx_s
{
  char *buf;
  size_t cap;
  size_t len;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* sink_callback: consume response body (the OTA JSON) into our buffer. */

static CODE int ota_sink_callback(FAR char **buffer, int offset, int datend,
                                  FAR int *buflen, FAR void *arg)
{
  struct ota_sink_ctx_s *ctx = arg;
  int chunk = datend - offset;

  if (chunk <= 0)
    {
      return 0;
    }

  if (ctx->len + chunk >= ctx->cap)
    {
      chunk = ctx->cap - ctx->len - 1;
      if (chunk <= 0)
        {
          return -ENOMEM;
        }
    }

  memcpy(ctx->buf + ctx->len, *buffer + offset, chunk);
  ctx->len += chunk;
  ctx->buf[ctx->len] = '\0';
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* Build the fixed OTA request body. mac is the device MAC string. */

static int build_ota_body(const char *mac, FAR char *out, size_t outsz)
{
  /* A minimal but server-accepted OTA body. The xiaozhi server is tolerant
   * about most fields; mac_address is the important one (also sent as the
   * Device-Id header).
   */

  return snprintf(out, outsz,
    "{\"flash_size\":16777216,\"minimum_free_heap_size\":8318916,"
    "\"mac_address\":\"%s\",\"chip_model_name\":\"esp32s3\","
    "\"chip_info\":{\"model\":9,\"cores\":2,\"revision\":2,\"features\":18},"
    "\"application\":{\"name\":\"xiaozhi\",\"version\":\"0.9.9\","
    "\"compile_time\":\"Jan 22 2025T20:40:23Z\","
    "\"idf_version\":\"v5.3.2-dirty\","
    "\"elf_sha256\":\"22986216df095587c42f8aeb06b239781c68ad8df80321e260556da7fcf5f522\"},"
    "\"partition_table\":[],\"ota\":{\"label\":\"factory\"},"
    "\"board\":{\"type\":\"bread-compact-wifi\",\"ssid\":\"mzy\","
    "\"rssi\":-58,\"channel\":6,\"ip\":\"192.168.124.38\","
    "\"mac\":\"cc:ba:97:20:b4:bc\"}}",
    mac);
}

/* Perform the OTA POST and fill ota_out with ws_url / ws_token.
 * Returns 0 on success, negative errno on failure.
 */

int xiaozhi_ota_fetch(const char *mac, FAR struct xiaozhi_ota_config_s *ota_out)
{
  struct webclient_context ctx;
  char body[OTA_BUF_SIZE];
  struct ota_sink_ctx_s sink;
  char buf[OTA_BUF_SIZE];         /* webclient http scratch */
  char sink_buf[OTA_BUF_SIZE];    /* distinct response-body accumulator */
  const char *headers[2];
  int ret;
  size_t bodylen;
  cJSON *root = NULL;
  cJSON *ws = NULL;
  cJSON *url = NULL;
  cJSON *token = NULL;

  if (mac == NULL || ota_out == NULL)
    {
      return -EINVAL;
    }

  memset(ota_out, 0, sizeof(*ota_out));

  /* Build request body. */
  bodylen = build_ota_body(mac, body, sizeof(body));

  /* Device-Id header = MAC. */
  headers[0] = "Device-Id: ";
  /* The webclient appends header lines as-is; we pass a small array. */
  /* NOTE: webclient treats each "headers[]" entry as a full header line
   * including the trailing CRLF-less content; it appends "\r\n".
   * We build the Device-Id header dynamically below.
   */

  /* The headers array requires fully formed header strings. We can't
   * use a string literal with the MAC embedded, so build it in `buf`
   * side buffer. Reuse a small static-ish local.
   */
  {
    static char device_id_header[64];
    snprintf(device_id_header, sizeof(device_id_header),
             "Device-Id: %s", mac);
    headers[0] = device_id_header;
    headers[1] = "Content-Type: application/json";
  }

  /* Set up sink to accumulate the response body. Use a SEPARATE buffer
   * from ctx.buffer: webclient reads into ctx.buffer and then calls our
   * sink_callback with a slice of that same ctx.buffer; if sink.buf ==
   * ctx.buffer we'd be memcpy-ing a region into itself (corrupting it).
   */
  memset(sink_buf, 0, sizeof(sink_buf));
  sink.buf = sink_buf;
  sink.cap = sizeof(sink_buf);
  sink.len = 0;

  webclient_set_defaults(&ctx);
  ctx.method              = "POST";
  ctx.url                 = OTA_URL;
  ctx.protocol_version    = WEBCLIENT_PROTOCOL_VERSION_HTTP_1_1;
  ctx.buffer              = buf;            /* shared scratch buffer */
  ctx.buflen              = sizeof(buf);
  ctx.headers             = headers;
  ctx.nheaders            = 2;
  ctx.bodylen             = bodylen;
  ctx.timeout_sec         = 15;
  ctx.sink_callback       = ota_sink_callback;
  ctx.sink_callback_arg   = &sink;

  /* Provide the mbedtls TLS backend so the https:// OTA URL works. */
  ctx.tls_ops             = &g_xz_tls_ops;
  ctx.tls_ctx             = NULL;

  /* Provide the request body via the static-body helper. */
  webclient_set_static_body(&ctx, body, bodylen);

  ret = webclient_perform(&ctx);
  if (ret != 0)
    {
      printf("[xiaozhi] OTA webclient_perform failed: %d\n", ret);
      return ret;
    }

  if (ctx.http_status != 200 || sink.len == 0)
    {
      printf("[xiaozhi] OTA unexpected status/body: %u\n", ctx.http_status);
      return -EIO;
    }

  /* Parse JSON: { "websocket": { "url": "...", "token": "..." } } */
  root = cJSON_ParseWithLength(sink.buf, sink.len);
  if (root == NULL)
    {
      printf("[xiaozhi] OTA JSON parse failed\n");
      return -EIO;
    }

  ws    = cJSON_GetObjectItem(root, "websocket");
  url   = cJSON_GetObjectItem(ws, "url");
  token = cJSON_GetObjectItem(ws, "token");

  if (!cJSON_IsString(url) || !cJSON_IsString(token))
    {
      printf("[xiaozhi] OTA: websocket.url/token missing\n");
      cJSON_Delete(root);
      return -EIO;
    }

  strncpy(ota_out->ws_url, url->valuestring, sizeof(ota_out->ws_url) - 1);
  strncpy(ota_out->ws_token, token->valuestring,
          sizeof(ota_out->ws_token) - 1);

  cJSON_Delete(root);
  return 0;
}
