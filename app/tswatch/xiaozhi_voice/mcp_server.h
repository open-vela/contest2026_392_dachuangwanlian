/****************************************************************************
 * apps/examples/xiaozhi_voice/include/mcp_server.h
 *
 * MCP (Model Context Protocol) tool server for Xiaozhi voice assistant.
 * Allows the AI to call device-side tools (stopwatch, clock, alarm, etc.)
 * via the xiaozhi websocket protocol.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include <stdint.h>
#include <stdbool.h>

/* Forward declaration — cJSON struct is opaque here; callers include
 * <netutils/cJSON.h> when they need to call cJSON API functions.
 */

struct cJSON;

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MCP_MAX_TOOLS 16
#define MCP_TOOL_NAME_MAX 64
#define MCP_TOOL_DESC_MAX 128
#define MCP_TOOL_SCHEMA_MAX 512

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Tool argument types - simplified for embedded use. */

enum mcp_arg_type_e
{
  MCP_ARG_NONE = 0,    /* No argument */
  MCP_ARG_STRING,      /* String argument */
  MCP_ARG_INT,         /* Integer argument */
  MCP_ARG_BOOL,        /* Boolean argument */
};

/* Tool argument value. */

union mcp_arg_value_u
{
  const char *s;
  int i;
  bool b;
};

/* Tool callback: called when the AI invokes a tool.
 * args is a cJSON object (may be NULL if no arguments).
 * Must return a cJSON object (caller frees it) or NULL on error.
 */

typedef struct cJSON *(*mcp_tool_cb_t)(const struct cJSON *args);

/* Tool registration entry. */

struct mcp_tool_s
{
  char name[MCP_TOOL_NAME_MAX];
  char description[MCP_TOOL_DESC_MAX];
  char schema[MCP_TOOL_SCHEMA_MAX]; /* JSON input schema string */
  mcp_tool_cb_t callback;
};

/* MCP server context. */

struct mcp_server_s
{
  struct mcp_tool_s tools[MCP_MAX_TOOLS];
  int tool_count;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Initialize the MCP server. Must be called once before any other
 * mcp_server_* functions.
 */

void mcp_server_init(void);

/* Register a tool with the MCP server. Returns 0 on success, -1 if
 * the tool table is full or name is NULL.
 * schema is a JSON string describing the input parameters (may be NULL).
 */

int mcp_server_add_tool(const char *name,
                        const char *description,
                        const char *schema,
                        mcp_tool_cb_t callback);

/* Handle an incoming MCP message from the server. The message is a
 * cJSON object with type="mcp". This function processes tool_calls
 * and sends responses back via the provided send_json callback.
 *
 * msg: the full JSON message (owned by caller, not freed here)
 * send_json_cb: callback to send a JSON message back to the server
 *               int send_json_cb(cJSON *msg) - returns 0 on success
 *
 * Returns 0 on success, -1 on error.
 */

int mcp_server_handle_message(const struct cJSON *msg,
                              int (*send_json_cb)(struct cJSON *));

/* Build the "features" object for the hello handshake. Adds
 * {"mcp": true} to the given hello JSON object.
 */

void mcp_server_add_hello_features(struct cJSON *hello);

#endif /* MCP_SERVER_H */
