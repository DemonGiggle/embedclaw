#ifndef EC_API_H
#define EC_API_H

#include "ec_config.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Configuration
 * ========================================================================= */

typedef struct {
    const char *base_url;    /* e.g. "api.openai.com" or any compatible host */
    uint16_t    port;        /* e.g. 80 or 443 */
    const char *api_key;
    int         use_tls;     /* 1 = HTTPS (not yet implemented) */
} ec_api_config_t;

/* =========================================================================
 * Tool definitions — sent to the LLM in the request
 * ========================================================================= */

typedef struct {
    const char *name;               /* function name, e.g. "hw_register_read" */
    const char *description;        /* human-readable description for the LLM */
    const char *parameters_schema;  /* JSON Schema object string for the arguments */
} ec_api_tool_def_t;

/* =========================================================================
 * Messages
 * ========================================================================= */

/*
 * A single message in the conversation history.
 *
 * Roles:
 *   "system"    — content only
 *   "user"      — content only
 *   "assistant" — content only, OR tool_calls (when num_tool_calls > 0)
 *   "tool"      — content + tool_call_id
 *
 * For "assistant" with tool calls: set content=NULL, tool_calls pointer and
 * num_tool_calls. For "tool" results: set content, tool_call_id, leave
 * tool_calls NULL / num_tool_calls 0.
 */
typedef struct ec_api_tool_call ec_api_tool_call_t;

typedef struct {
    const char              *role;
    const char              *content;          /* NULL when num_tool_calls > 0 */
    const char              *tool_call_id;     /* set for role="tool" */
    const ec_api_tool_call_t *tool_calls;      /* set for assistant+tool_calls */
    int                      num_tool_calls;
} ec_api_message_t;

/* =========================================================================
 * Tool call (returned by the LLM, stored in session)
 * ========================================================================= */

struct ec_api_tool_call {
    char id[EC_CONFIG_TOOL_ID_BUF];
    char name[EC_CONFIG_TOOL_NAME_BUF];
    char arguments[EC_CONFIG_TOOL_ARG_BUF];   /* raw JSON object string */
};

/* =========================================================================
 * Response
 * ========================================================================= */

typedef enum {
    EC_API_RESP_MESSAGE,    /* final text: content field is set */
    EC_API_RESP_TOOL_CALLS, /* tool invocations: tool_calls[] is set */
} ec_api_resp_type_t;

typedef struct {
    ec_api_resp_type_t  type;
    char                content[EC_CONFIG_CONTENT_BUF]; /* if MESSAGE */
    ec_api_tool_call_t  tool_calls[EC_CONFIG_MAX_TOOL_CALLS]; /* if TOOL_CALLS */
    int                 num_tool_calls;
} ec_api_response_t;

/* =========================================================================
 * API function
 * ========================================================================= */

/**
 * Perform a blocking chat completion request.
 *
 * @param config      API endpoint configuration.
 * @param model       Model name, e.g. "gpt-4o".
 * @param messages    Conversation history array.
 * @param num_messages Number of messages.
 * @param tools       Tool definitions to advertise (NULL if none).
 * @param num_tools   Number of tools (0 if none).
 * @param out         Output response struct (caller-provided).
 * @return 0 on success, negative error code on failure.
 */
int ec_api_chat_completion(
    const ec_api_config_t   *config,
    const char              *model,
    const ec_api_message_t  *messages,
    size_t                   num_messages,
    const ec_api_tool_def_t *tools,
    size_t                   num_tools,
    ec_api_response_t       *out
);

/* Error codes */
#define EC_API_ERR_JSON_BUILD  (-10)
#define EC_API_ERR_HTTP        (-11)
#define EC_API_ERR_JSON_PARSE  (-12)
#define EC_API_ERR_API         (-13)
#define EC_API_ERR_OVERFLOW    (-14)

#ifdef __cplusplus
}
#endif

#endif /* EC_API_H */
