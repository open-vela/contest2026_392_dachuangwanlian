/****************************************************************************
 * apps/examples/xiaozhi_voice/xiaozhi_ota.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_XIAOZHI_VOICE_XIAOZHI_OTA_H
#define __APPS_EXAMPLES_XIAOZHI_VOICE_XIAOZHI_OTA_H

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct xiaozhi_ota_config_s
{
  char ws_url[128];    /* e.g. "wss://api.tenclass.net/xiaozhi/v1/" */
  char ws_token[64];   /* e.g. "test-token" */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: xiaozhi_ota_fetch
 *
 * Description:
 *   POST the OTA request to the xiaozhi server and parse out the WebSocket
 *   url and token from the response.
 *
 *   URL: https://api.tenclass.net/xiaozhi/ota/
 *   Header: Device-Id: <mac>
 *   Body: JSON describing the (mock) device.
 *
 * Input Parameters:
 *   mac     - Device MAC string ("xx:xx:xx:xx:xx:xx")
 *   ota_out - Receives ws_url and ws_token.
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 ****************************************************************************/

int xiaozhi_ota_fetch(const char *mac,
                      FAR struct xiaozhi_ota_config_s *ota_out);

#endif /* __APPS_EXAMPLES_XIAOZHI_VOICE_XIAOZHI_OTA_H */
