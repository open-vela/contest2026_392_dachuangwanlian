/****************************************************************************
 * apps/examples/xiaozhi_voice/mcp_server.c
 *
 * MCP (Model Context Protocol) tool server implementation.
 * Manages tool registration and dispatch for xiaozhi voice assistant.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <netutils/cJSON.h>

#include "mcp_server.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct mcp_server_s g_mcp;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void mcp_server_init(void)
{
  memset(&g_mcp, 0, sizeof(g_mcp));
}

int mcp_server_add_tool(const char *name,
                        const char *description,
                        const char *schema,
                        mcp_tool_cb_t callback)
{
  if (name == NULL || callback == NULL)
    {
      return -1;
    }

  if (g_mcp.tool_count >= MCP_MAX_TOOLS)
    {
      printf("[mcp] ERROR: tool table full, cannot add '%s'\n", name);
      return -1;
    }

  struct mcp_tool_s *tool = &g_mcp.tools[g_mcp.tool_count];
  strncpy(tool->name, name, MCP_TOOL_NAME_MAX - 1);
  tool->name[MCP_TOOL_NAME_MAX - 1] = '\0';
  strncpy(tool->description, description, MCP_TOOL_DESC_MAX - 1);
  tool->description[MCP_TOOL_DESC_MAX - 1] = '\0';
  if (schema != NULL)
    {
      strncpy(tool->schema, schema, MCP_TOOL_SCHEMA_MAX - 1);
      tool->schema[MCP_TOOL_SCHEMA_MAX - 1] = '\0';
    }
  else
    {
      tool->schema[0] = '\0';
    }

  tool->callback = callback;

  g_mcp.tool_count++;
  return 0;
}

static struct mcp_tool_s *find_tool(const char *name)
{
  int i;
  for (i = 0; i < g_mcp.tool_count; i++)
    {
      if (strcmp(g_mcp.tools[i].name, name) == 0)
        {
          return &g_mcp.tools[i];
        }
    }

  return NULL;
}

int mcp_server_handle_message(const cJSON *msg,
                              int (*send_json_cb)(cJSON *))
{
  if (msg == NULL || send_json_cb == NULL)
    {
      return -1;
    }

  /* Server sends: {"type":"mcp","payload":{...},"session_id":"..."} */

  cJSON *payload = cJSON_GetObjectItem(msg, "payload");
  if (!cJSON_IsObject(payload))
    {
      return -1;
    }

  /* Handle JSON-RPC method dispatch inside payload. */

  cJSON *method = cJSON_GetObjectItem(payload, "method");
  cJSON *id_item = cJSON_GetObjectItem(payload, "id");

  if (cJSON_IsString(method))
    {
      const char *method_name = method->valuestring;

      if (strcmp(method_name, "initialize") == 0)
        {
          /* Respond with server capabilities (JSON-RPC result). */

          cJSON *resp = cJSON_CreateObject();
          cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
          if (id_item != NULL)
            {
              cJSON_AddItemReferenceToObject(resp, "id", id_item);
            }

          cJSON *result = cJSON_CreateObject();
          cJSON_AddStringToObject(result, "protocolVersion", "2024-11-05");

          cJSON *caps = cJSON_CreateObject();
          cJSON *tools_caps = cJSON_CreateObject();
          cJSON_AddBoolToObject(tools_caps, "listChanged", false);
          cJSON_AddItemToObject(caps, "tools", tools_caps);
          cJSON_AddItemToObject(result, "capabilities", caps);

          cJSON *server_info = cJSON_CreateObject();
          cJSON_AddStringToObject(server_info, "name", "xiaozhi-device");
          cJSON_AddStringToObject(server_info, "version", "1.0.0");
          cJSON_AddItemToObject(result, "serverInfo", server_info);

          cJSON_AddItemToObject(resp, "result", result);

          int ret = send_json_cb(resp);
          cJSON_Delete(resp);

          return ret;
        }
      else if (strcmp(method_name, "tools/list") == 0)
        {
          /* Respond with the list of registered tools. */

          cJSON *resp = cJSON_CreateObject();
          cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
          if (id_item != NULL)
            {
              cJSON_AddItemReferenceToObject(resp, "id", id_item);
            }

          cJSON *result = cJSON_CreateObject();
          cJSON *tools_arr = cJSON_CreateArray();

          int t;
          for (t = 0; t < g_mcp.tool_count; t++)
            {
              cJSON *tool_obj = cJSON_CreateObject();
              cJSON_AddStringToObject(tool_obj, "name",
                                      g_mcp.tools[t].name);
              cJSON_AddStringToObject(tool_obj, "description",
                                      g_mcp.tools[t].description);

              /* Parse and add input schema. */

              cJSON *schema = NULL;
              if (g_mcp.tools[t].schema[0] != '\0')
                {
                  schema = cJSON_Parse(g_mcp.tools[t].schema);
                }

              if (schema == NULL)
                {
                  schema = cJSON_CreateObject();
                  cJSON_AddStringToObject(schema, "type", "object");
                }

              cJSON_AddItemToObject(tool_obj, "inputSchema", schema);
              cJSON_AddItemToArray(tools_arr, tool_obj);
            }

          cJSON_AddItemToObject(result, "tools", tools_arr);
          cJSON_AddItemToObject(resp, "result", result);

          int ret = send_json_cb(resp);
          cJSON_Delete(resp);
          return ret;
        }
      else if (strcmp(method_name, "tools/call") == 0)
        {
          /* MCP tools/call: params contains name and arguments. */

          cJSON *params = cJSON_GetObjectItem(payload, "params");
          cJSON *name_item = cJSON_GetObjectItem(params, "name");
          cJSON *args_item = cJSON_GetObjectItem(params, "arguments");

          const char *tool_name = cJSON_IsString(name_item) ?
                                  name_item->valuestring : "";

          struct mcp_tool_s *tool = find_tool(tool_name);
          cJSON *tool_result = NULL;

          if (tool != NULL)
            {
              tool_result = tool->callback(args_item);
            }

          /* Build MCP tools/call response with content array. */

          cJSON *resp = cJSON_CreateObject();
          cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
          if (id_item != NULL)
            {
              cJSON_AddItemReferenceToObject(resp, "id", id_item);
            }

          cJSON *result = cJSON_CreateObject();
          cJSON *content = cJSON_CreateArray();
          cJSON *content_entry = cJSON_CreateObject();
          cJSON_AddStringToObject(content_entry, "type", "text");

          if (tool_result != NULL)
            {
              /* If it's a string, use its value directly; otherwise print. */

              const char *text = cJSON_IsString(tool_result) ?
                                 tool_result->valuestring : NULL;
              if (text != NULL)
                {
                  cJSON_AddStringToObject(content_entry, "text", text);
                }
              else
                {
                  char *result_str = cJSON_PrintUnformatted(tool_result);
                  cJSON_AddStringToObject(content_entry, "text",
                                          result_str ? result_str : "null");
                  free(result_str);
                }
            }
          else
            {
              cJSON_AddStringToObject(content_entry, "text", "null");
            }

          cJSON_AddItemToArray(content, content_entry);
          cJSON_AddItemToObject(result, "content", content);
          cJSON_AddBoolToObject(result, "isError", (tool == NULL));

          cJSON_AddItemToObject(resp, "result", result);

          int ret = send_json_cb(resp);
          cJSON_Delete(resp);

          if (tool_result != NULL)
            {
              cJSON_Delete(tool_result);
            }

          return ret;
        }
      else if (strcmp(method_name, "notifications/tools/list_changed") == 0)
        {
          return 0;
        }
    }

  return -1;
}

void mcp_server_add_hello_features(cJSON *hello)
{
  if (hello == NULL)
    {
      return;
    }

  cJSON *features = cJSON_GetObjectItem(hello, "features");
  if (features == NULL)
    {
      features = cJSON_CreateObject();
      cJSON_AddItemToObject(hello, "features", features);
    }

  cJSON_AddBoolToObject(features, "mcp", true);
}
