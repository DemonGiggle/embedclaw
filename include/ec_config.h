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
#define EC_CONFIG_MODEL        "gpt-4"

/* Buffer sizes */
#define EC_CONFIG_REPLY_BUF    4096

#endif /* EC_CONFIG_H */
