#ifndef EC_API_H
#define EC_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *base_url;    /* e.g. "api.openai.com" or any compatible endpoint */
    uint16_t    port;        /* e.g. 443 or 80 */
    const char *api_key;
    int         use_tls;     /* 1 = HTTPS (not yet implemented) */
} ec_api_config_t;

typedef struct {
    const char *role;        /* "system", "user", "assistant" */
    const char *content;
} ec_api_message_t;

/**
 * Perform a blocking chat completion request.
 * Works with any OpenAI-compatible API endpoint.
 *
 * @param config      API endpoint configuration.
 * @param model       Model name, e.g. "gpt-4".
 * @param messages    Array of messages.
 * @param num_messages Number of messages.
 * @param out_buf     Buffer to receive the assistant's reply (null-terminated).
 * @param out_buf_size Size of out_buf.
 * @return 0 on success, negative error code on failure.
 */
int ec_api_chat_completion(
    const ec_api_config_t  *config,
    const char             *model,
    const ec_api_message_t *messages,
    size_t                  num_messages,
    char                   *out_buf,
    size_t                  out_buf_size
);

/* Error codes */
#define EC_API_ERR_JSON_BUILD  (-10)
#define EC_API_ERR_HTTP        (-11)
#define EC_API_ERR_JSON_PARSE  (-12)
#define EC_API_ERR_API         (-13)

#ifdef __cplusplus
}
#endif

#endif /* EC_API_H */
