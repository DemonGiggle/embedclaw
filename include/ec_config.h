#ifndef EC_CONFIG_H
#define EC_CONFIG_H

/*
 * EmbedClaw compile-time configuration.
 *
 * On FreeRTOS these are the actual values used at runtime.
 * On POSIX they serve as defaults that can be overridden via environment
 * variables (EC_API_KEY, EC_API_HOST, EC_API_PORT, EC_MODEL).
 *
 * Edit these before building for your target.
 */

/* API endpoint */
#define EC_CONFIG_API_HOST     "api.openai.com"
#define EC_CONFIG_API_PORT     80
#define EC_CONFIG_API_KEY      "sk-CHANGE-ME"
#define EC_CONFIG_USE_TLS      0

/* Model */
#define EC_CONFIG_MODEL        "gpt-4o"

/* HTTP layer buffers */
#define EC_CONFIG_REQUEST_BUF       4096   /* outgoing JSON request body */
#define EC_CONFIG_RESPONSE_BUF      8192   /* raw HTTP response body */

/* API response buffers */
#define EC_CONFIG_CONTENT_BUF       2048   /* extracted LLM text content */
#define EC_CONFIG_TOOL_ID_BUF       64     /* tool_call id field */
#define EC_CONFIG_TOOL_NAME_BUF     64     /* tool function name */
#define EC_CONFIG_TOOL_ARG_BUF      256    /* tool call arguments JSON */
#define EC_CONFIG_MAX_TOOL_CALLS    4      /* max tool_calls in one LLM response */

/* Session layer */
#define EC_CONFIG_SESSION_CONTENT_BUF  256 /* per-message content buffer */
#define EC_CONFIG_MAX_HISTORY          16  /* max messages in conversation history */

/* Tool framework */
#define EC_CONFIG_MAX_TOOLS            16  /* max registered tools */

/* Agent loop */
#define EC_CONFIG_MAX_AGENT_ITERS      8   /* max tool-call iterations per turn */

/* I/O layer */
#define EC_CONFIG_IO_LINE_BUF          256 /* user input line buffer */
#define EC_CONFIG_TELNET_PORT          2323

/* Legacy alias — kept for any code that still references it */
#define EC_CONFIG_REPLY_BUF  EC_CONFIG_CONTENT_BUF

#endif /* EC_CONFIG_H */
