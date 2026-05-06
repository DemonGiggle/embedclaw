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
#define EC_CONFIG_API_PORT     443
#define EC_CONFIG_API_KEY      "sk-CHANGE-ME"
#ifndef EC_CONFIG_USE_TLS
#define EC_CONFIG_USE_TLS      1
#endif

/* Model */
#define EC_CONFIG_MODEL        "gpt-4o"

/* HTTP layer buffers */
#define EC_CONFIG_REQUEST_BUF       8192   /* outgoing JSON request body */
#define EC_CONFIG_RESPONSE_BUF      8192   /* raw HTTP response body */

/* API response buffers */
#define EC_CONFIG_CONTENT_BUF       2048   /* extracted LLM text content */
#define EC_CONFIG_TOOL_ID_BUF       64     /* tool_call id field */
#define EC_CONFIG_TOOL_NAME_BUF     64     /* tool function name */
#define EC_CONFIG_TOOL_ARG_BUF      256    /* tool call arguments JSON */
#define EC_CONFIG_MAX_TOOL_CALLS    4      /* max tool_calls in one LLM response */

/* Session layer
 * MAX_HISTORY must be large enough to hold a full agentic turn without
 * eviction: 1 (user) + MAX_AGENT_ITERS * (1 + MAX_TOOL_CALLS) + headroom.
 * With defaults: 1 + 8*(1+4) = 41 minimum; 64 gives two full turns of room. */
#define EC_CONFIG_SESSION_CONTENT_BUF  512 /* per-message content buffer */
#define EC_CONFIG_MAX_HISTORY          64  /* max messages in conversation history */

/* Tool framework */
#define EC_CONFIG_MAX_TOOLS            16  /* max registered tools */
#define EC_CONFIG_TOOL_RESULT_BUF      4096 /* per-tool result buffer */

/* Agent loop */
#define EC_CONFIG_MAX_AGENT_ITERS      8   /* max tool-call iterations per turn */

/* I/O layer */
#define EC_CONFIG_IO_LINE_BUF          256  /* user input line buffer */
#define EC_CONFIG_TELNET_PORT          2323
#define EC_CONFIG_UART_RX_TIMEOUT_MS   100  /* FreeRTOS UART read poll timeout */
#define EC_CONFIG_UART_TX_TIMEOUT_MS   1000 /* FreeRTOS UART write timeout */

/* Skill layer */
#define EC_CONFIG_SYSTEM_PROMPT_BUF    2048 /* combined system prompt buffer */
#define EC_CONFIG_MAX_SKILLS           16   /* max registered skills */

/* Web browsing skill */
#define EC_CONFIG_BRAVE_API_HOST       "api.search.brave.com"
#define EC_CONFIG_BRAVE_API_PORT       443
#define EC_CONFIG_BRAVE_API_KEY        "BSA-CHANGE-ME"
#define EC_CONFIG_WEB_FETCH_MAX        4096 /* max bytes returned by web_fetch */
#define EC_CONFIG_WEB_SEARCH_COUNT     5    /* results per search */

/* Debug logging (FreeRTOS: set to 1 to enable; POSIX: use EC_DEBUG=1 env) */
#ifndef EC_CONFIG_DEBUG_LOG
#define EC_CONFIG_DEBUG_LOG    0
#endif

/* Legacy alias — kept for any code that still references it */
#define EC_CONFIG_REPLY_BUF  EC_CONFIG_CONTENT_BUF

#endif /* EC_CONFIG_H */
