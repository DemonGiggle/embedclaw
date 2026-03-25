#include "mock_http.h"
#include "ec_http.h"   /* the interface we're implementing */

#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------- */

typedef struct {
    char body[MOCK_HTTP_REQ_CAPTURE];
    int  status_code;
} mock_resp_t;

static mock_resp_t s_responses[MOCK_HTTP_MAX_RESPONSES];
static int s_resp_head = 0;
static int s_resp_count = 0;

/* Captured request bodies, one per call */
static char s_req_bodies[MOCK_HTTP_MAX_RESPONSES][MOCK_HTTP_REQ_CAPTURE];
static int  s_call_count = 0;

/* -------------------------------------------------------------------------
 * Configuration helpers
 * ------------------------------------------------------------------------- */

void mock_http_reset(void)
{
    memset(s_responses, 0, sizeof(s_responses));
    memset(s_req_bodies, 0, sizeof(s_req_bodies));
    s_resp_head  = 0;
    s_resp_count = 0;
    s_call_count = 0;
}

void mock_http_queue(const char *body, int status_code)
{
    if (s_resp_count >= MOCK_HTTP_MAX_RESPONSES) return;
    int slot = (s_resp_head + s_resp_count) % MOCK_HTTP_MAX_RESPONSES;
    strncpy(s_responses[slot].body, body, MOCK_HTTP_REQ_CAPTURE - 1);
    s_responses[slot].status_code = status_code;
    s_resp_count++;
}

/* -------------------------------------------------------------------------
 * Inspection helpers
 * ------------------------------------------------------------------------- */

int mock_http_call_count(void)
{
    return s_call_count;
}

const char *mock_http_req_body(int n)
{
    if (n < 0 || n >= s_call_count) return NULL;
    return s_req_bodies[n];
}

/* -------------------------------------------------------------------------
 * ec_http_request implementation (replaces ec_http.c at link time)
 * ------------------------------------------------------------------------- */

int ec_http_request(const ec_http_request_t *req,
                    ec_http_response_t *resp,
                    char *resp_buf, size_t resp_buf_size)
{
    /* Capture the outgoing request body */
    if (s_call_count < MOCK_HTTP_MAX_RESPONSES && req->body && req->body_len) {
        size_t cap = req->body_len < MOCK_HTTP_REQ_CAPTURE - 1
                   ? req->body_len : MOCK_HTTP_REQ_CAPTURE - 1;
        memcpy(s_req_bodies[s_call_count], req->body, cap);
        s_req_bodies[s_call_count][cap] = '\0';
    }
    s_call_count++;

    /* Dequeue next response */
    if (s_resp_count == 0) {
        fprintf(stderr, "[mock_http] No response queued for call %d!\n",
                s_call_count);
        return EC_HTTP_ERR_RECV;
    }

    mock_resp_t *r = &s_responses[s_resp_head];
    s_resp_head  = (s_resp_head + 1) % MOCK_HTTP_MAX_RESPONSES;
    s_resp_count--;

    /* Copy body into caller-provided buffer */
    size_t body_len = strlen(r->body);
    if (body_len >= resp_buf_size) {
        fprintf(stderr, "[mock_http] Response body too large for buffer\n");
        return EC_HTTP_ERR_OVERFLOW;
    }
    memcpy(resp_buf, r->body, body_len + 1);

    resp->status_code = r->status_code;
    resp->body        = resp_buf;
    resp->body_len    = body_len;
    return 0;
}
