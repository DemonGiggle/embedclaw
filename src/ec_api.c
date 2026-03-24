#include "ec_api.h"
#include "ec_http.h"
#include "ec_json.h"

#include <stdio.h>
#include <string.h>

/*
 * Static buffers — single-threaded, so one request at a time is safe.
 * Keeps large allocations out of the stack (important on FreeRTOS).
 */
static char s_req_buf[EC_CONFIG_REQUEST_BUF];
static char s_resp_buf[EC_CONFIG_RESPONSE_BUF];

/* -------------------------------------------------------------------------
 * Request builder
 * ------------------------------------------------------------------------- */

static int build_request(
    const char              *model,
    const ec_api_message_t  *messages,
    size_t                   num_messages,
    const ec_api_tool_def_t *tools,
    size_t                   num_tools)
{
    ec_json_writer_t jw;
    ec_json_writer_init(&jw, s_req_buf, sizeof(s_req_buf));

    ec_json_obj_start(&jw);
    ec_json_add_string(&jw, "model", model);

    /* messages array */
    ec_json_array_start(&jw, "messages");
    for (size_t i = 0; i < num_messages; i++) {
        const ec_api_message_t *m = &messages[i];
        ec_json_array_obj_start(&jw);
        ec_json_add_string(&jw, "role", m->role);

        if (m->num_tool_calls > 0) {
            /* assistant message carrying tool_calls; content is null */
            ec_json_add_raw(&jw, "content", "null");
            ec_json_array_start(&jw, "tool_calls");
            for (int j = 0; j < m->num_tool_calls; j++) {
                const ec_api_tool_call_t *tc = &m->tool_calls[j];
                ec_json_array_obj_start(&jw);
                ec_json_add_string(&jw, "id", tc->id);
                ec_json_add_string(&jw, "type", "function");
                ec_json_key(&jw, "function");
                ec_json_obj_start(&jw);
                ec_json_add_string(&jw, "name", tc->name);
                ec_json_add_string(&jw, "arguments", tc->arguments);
                ec_json_obj_end(&jw);
                ec_json_obj_end(&jw);
            }
            ec_json_array_end(&jw);
        } else if (m->tool_call_id) {
            /* tool result message */
            ec_json_add_string(&jw, "tool_call_id", m->tool_call_id);
            ec_json_add_string(&jw, "content", m->content ? m->content : "");
        } else {
            ec_json_add_string(&jw, "content", m->content ? m->content : "");
        }
        ec_json_obj_end(&jw);
    }
    ec_json_array_end(&jw);

    /* tools array (omitted when empty) */
    if (tools && num_tools > 0) {
        ec_json_array_start(&jw, "tools");
        for (size_t i = 0; i < num_tools; i++) {
            const ec_api_tool_def_t *t = &tools[i];
            ec_json_array_obj_start(&jw);
            ec_json_add_string(&jw, "type", "function");
            ec_json_key(&jw, "function");
            ec_json_obj_start(&jw);
            ec_json_add_string(&jw, "name", t->name);
            ec_json_add_string(&jw, "description", t->description);
            ec_json_add_raw(&jw, "parameters", t->parameters_schema);
            ec_json_obj_end(&jw);
            ec_json_obj_end(&jw);
        }
        ec_json_array_end(&jw);
    }

    ec_json_obj_end(&jw);
    return ec_json_writer_finish(&jw);
}

/* -------------------------------------------------------------------------
 * Response parser
 * ------------------------------------------------------------------------- */

static int parse_response(const char *body, size_t body_len,
                          ec_api_response_t *out)
{
    /* Determine response type via finish_reason */
    char finish_reason[32];
    int rc = ec_json_find_string(body, body_len,
                                 "choices[0].finish_reason",
                                 finish_reason, sizeof(finish_reason));
    if (rc < 0) return EC_API_ERR_JSON_PARSE;

    if (strcmp(finish_reason, "tool_calls") == 0) {
        out->type = EC_API_RESP_TOOL_CALLS;
        out->num_tool_calls = 0;

        for (int i = 0; i < EC_CONFIG_MAX_TOOL_CALLS; i++) {
            char path[64];
            ec_api_tool_call_t *tc = &out->tool_calls[i];

            snprintf(path, sizeof(path),
                     "choices[0].message.tool_calls[%d].id", i);
            if (ec_json_find_string(body, body_len, path,
                                    tc->id, sizeof(tc->id)) < 0)
                break;

            snprintf(path, sizeof(path),
                     "choices[0].message.tool_calls[%d].function.name", i);
            if (ec_json_find_string(body, body_len, path,
                                    tc->name, sizeof(tc->name)) < 0)
                break;

            snprintf(path, sizeof(path),
                     "choices[0].message.tool_calls[%d].function.arguments", i);
            if (ec_json_find_string(body, body_len, path,
                                    tc->arguments, sizeof(tc->arguments)) < 0)
                break;

            out->num_tool_calls = i + 1;
        }

        if (out->num_tool_calls == 0) return EC_API_ERR_JSON_PARSE;
    } else {
        /* Regular text response */
        out->type = EC_API_RESP_MESSAGE;
        rc = ec_json_find_string(body, body_len,
                                 "choices[0].message.content",
                                 out->content, sizeof(out->content));
        if (rc < 0) return EC_API_ERR_JSON_PARSE;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

int ec_api_chat_completion(
    const ec_api_config_t   *config,
    const char              *model,
    const ec_api_message_t  *messages,
    size_t                   num_messages,
    const ec_api_tool_def_t *tools,
    size_t                   num_tools,
    ec_api_response_t       *out)
{
    int json_len = build_request(model, messages, num_messages, tools, num_tools);
    if (json_len < 0) return EC_API_ERR_JSON_BUILD;

    char extra_headers[256];
    snprintf(extra_headers, sizeof(extra_headers),
        "Content-Type: application/json\r\n"
        "Authorization: Bearer %s\r\n",
        config->api_key);

    ec_http_request_t req = {
        .method   = "POST",
        .host     = config->base_url,
        .port     = config->port,
        .path     = "/v1/chat/completions",
        .headers  = extra_headers,
        .body     = s_req_buf,
        .body_len = (size_t)json_len,
    };

    ec_http_response_t resp;
    int rc = ec_http_request(&req, &resp, s_resp_buf, sizeof(s_resp_buf));
    if (rc != 0) return EC_API_ERR_HTTP;

    if (resp.status_code < 200 || resp.status_code >= 300) {
        /* Copy error body into content for debugging */
        size_t copy = resp.body_len < EC_CONFIG_CONTENT_BUF - 1
                    ? resp.body_len : EC_CONFIG_CONTENT_BUF - 1;
        memcpy(out->content, resp.body, copy);
        out->content[copy] = '\0';
        out->type = EC_API_RESP_MESSAGE;
        return EC_API_ERR_API;
    }

    return parse_response(resp.body, resp.body_len, out);
}
