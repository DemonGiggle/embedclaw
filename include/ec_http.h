#ifndef EC_HTTP_H
#define EC_HTTP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *method;       /* "GET", "POST" */
    const char *host;
    uint16_t    port;
    const char *path;         /* e.g. "/v1/chat/completions" */
    const char *headers;      /* extra headers, each ending with \r\n, or NULL */
    const char *body;
    size_t      body_len;
} ec_http_request_t;

typedef struct {
    int         status_code;
    char       *body;         /* points into the caller-provided buffer */
    size_t      body_len;
} ec_http_response_t;

/**
 * Perform an HTTP request.
 *
 * @param req       Request parameters.
 * @param resp      Output response (body points into resp_buf).
 * @param resp_buf  Buffer for storing the response body.
 * @param resp_buf_size  Size of resp_buf.
 * @return 0 on success, negative error code on failure.
 */
int ec_http_request(const ec_http_request_t *req,
                    ec_http_response_t *resp,
                    char *resp_buf, size_t resp_buf_size);

/* Error codes */
#define EC_HTTP_ERR_CONNECT   (-1)
#define EC_HTTP_ERR_SEND      (-2)
#define EC_HTTP_ERR_RECV      (-3)
#define EC_HTTP_ERR_PARSE     (-4)
#define EC_HTTP_ERR_OVERFLOW  (-5)
#define EC_HTTP_ERR_TIMEOUT   (-6)

#ifdef __cplusplus
}
#endif

#endif /* EC_HTTP_H */
