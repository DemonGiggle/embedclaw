#include "ec_api.h"
#include "ec_http.h"
#include "ec_json.h"

#include <stdio.h>
#include <string.h>

/* Max size for the JSON request body */
#define EC_API_REQ_BUF_SIZE  4096
/* Max size for the HTTP response */
#define EC_API_RESP_BUF_SIZE 8192

int ec_api_chat_completion(
    const ec_api_config_t  *config,
    const char             *model,
    const ec_api_message_t *messages,
    size_t                  num_messages,
    char                   *out_buf,
    size_t                  out_buf_size)
{
    /* Build JSON request body */
    char req_json[EC_API_REQ_BUF_SIZE];
    ec_json_writer_t jw;
    ec_json_writer_init(&jw, req_json, sizeof(req_json));

    ec_json_obj_start(&jw);
    ec_json_add_string(&jw, "model", model);
    ec_json_array_start(&jw, "messages");

    for (size_t i = 0; i < num_messages; i++) {
        ec_json_array_obj_start(&jw);
        ec_json_add_string(&jw, "role", messages[i].role);
        ec_json_add_string(&jw, "content", messages[i].content);
        ec_json_obj_end(&jw);
    }

    ec_json_array_end(&jw);
    ec_json_obj_end(&jw);

    int json_len = ec_json_writer_finish(&jw);
    if (json_len < 0) {
        return EC_API_ERR_JSON_BUILD;
    }

    /* Build extra headers */
    char extra_headers[512];
    snprintf(extra_headers, sizeof(extra_headers),
        "Content-Type: application/json\r\n"
        "Authorization: Bearer %s\r\n",
        config->api_key);

    /* Make HTTP request */
    ec_http_request_t req = {
        .method  = "POST",
        .host    = config->base_url,
        .port    = config->port,
        .path    = "/v1/chat/completions",
        .headers = extra_headers,
        .body    = req_json,
        .body_len = (size_t)json_len,
    };

    char resp_buf[EC_API_RESP_BUF_SIZE];
    ec_http_response_t resp;
    int rc = ec_http_request(&req, &resp, resp_buf, sizeof(resp_buf));
    if (rc != 0) {
        return EC_API_ERR_HTTP;
    }

    if (resp.status_code < 200 || resp.status_code >= 300) {
        /* Copy error body to out_buf for debugging */
        size_t copy_len = resp.body_len < out_buf_size - 1
                        ? resp.body_len : out_buf_size - 1;
        memcpy(out_buf, resp.body, copy_len);
        out_buf[copy_len] = '\0';
        return EC_API_ERR_API;
    }

    /* Parse the response: extract choices[0].message.content */
    int content_len = ec_json_find_string(
        resp.body, resp.body_len,
        "choices[0].message.content",
        out_buf, out_buf_size);

    if (content_len < 0) {
        return EC_API_ERR_JSON_PARSE;
    }

    return 0;
}
