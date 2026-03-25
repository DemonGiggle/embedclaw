#ifndef MOCK_HTTP_H
#define MOCK_HTTP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Mock HTTP layer for testing.
 *
 * Replaces ec_http.c at link time.  Tests pre-load a FIFO of canned
 * LLM responses; each call to ec_http_request() dequeues the next one.
 * The mock also captures the raw request body of every call so tests
 * can inspect what the agent actually sent to the LLM.
 */

#define MOCK_HTTP_MAX_RESPONSES  32
#define MOCK_HTTP_REQ_CAPTURE    8192  /* bytes */

/* -------------------------------------------------------------------------
 * Configuration helpers — call from tests
 * ------------------------------------------------------------------------- */

/** Remove all queued responses and clear capture buffers. */
void mock_http_reset(void);

/** Enqueue one pre-canned response body (must be valid JSON). */
void mock_http_queue(const char *body, int status_code);

/* -------------------------------------------------------------------------
 * Inspection helpers — call after the agent runs
 * ------------------------------------------------------------------------- */

/** Number of times ec_http_request has been called since last reset. */
int mock_http_call_count(void);

/**
 * Raw JSON body sent in the Nth call (0-based).
 * Returns NULL if n is out of range.
 */
const char *mock_http_req_body(int n);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_HTTP_H */
