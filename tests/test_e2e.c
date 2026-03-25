/*
 * EmbedClaw end-to-end tests.
 *
 * Real stack under test:
 *   ec_skill_table → ec_skill → ec_agent → ec_session → ec_api → [mock HTTP]
 *   ec_tool dispatch → hw_register_read/write (POSIX mock registers)
 *
 * Only ec_http_request is replaced (mock_http.c).  Everything else is real.
 *
 * Test flow mirrors production:
 *   1. User input arrives as a string.
 *   2. ec_agent_run_turn() processes it.
 *   3. The mock HTTP returns pre-canned LLM responses.
 *   4. Tools are dispatched against the real mock register bank.
 *   5. Final text response is captured and asserted.
 */

#include "test_runner.h"
#include "mock_http.h"
#include "ec_agent.h"
#include "ec_session.h"
#include "ec_skill.h"
#include "ec_json.h"
#include "ec_config.h"

#include <string.h>
#include <stdio.h>

/* =========================================================================
 * Pre-built mock LLM response JSON strings
 * ========================================================================= */

/* Text response — finish_reason: stop */
#define RESP_TEXT(content) \
    "{\"choices\":[{" \
      "\"message\":{\"role\":\"assistant\",\"content\":\"" content "\"}," \
      "\"finish_reason\":\"stop\"}]}"

/* Single tool call — hw_register_read */
#define RESP_REG_READ(call_id, addr) \
    "{\"choices\":[{" \
      "\"message\":{\"role\":\"assistant\",\"content\":null," \
        "\"tool_calls\":[{" \
          "\"id\":\"" call_id "\"," \
          "\"type\":\"function\"," \
          "\"function\":{" \
            "\"name\":\"hw_register_read\"," \
            "\"arguments\":\"{\\\"address\\\":\\\"" addr "\\\"}\"" \
          "}" \
        "}]}," \
      "\"finish_reason\":\"tool_calls\"}]}"

/* Single tool call — hw_register_write */
#define RESP_REG_WRITE(call_id, addr, val) \
    "{\"choices\":[{" \
      "\"message\":{\"role\":\"assistant\",\"content\":null," \
        "\"tool_calls\":[{" \
          "\"id\":\"" call_id "\"," \
          "\"type\":\"function\"," \
          "\"function\":{" \
            "\"name\":\"hw_register_write\"," \
            "\"arguments\":\"{\\\"address\\\":\\\"" addr "\\\"," \
                            "\\\"value\\\":\\\"" val "\\\"}\"" \
          "}" \
        "}]}," \
      "\"finish_reason\":\"tool_calls\"}]}"

/* Two tool calls in one response — both hw_register_read */
#define RESP_TWO_READS(id0, addr0, id1, addr1) \
    "{\"choices\":[{" \
      "\"message\":{\"role\":\"assistant\",\"content\":null," \
        "\"tool_calls\":[" \
          "{\"id\":\"" id0 "\",\"type\":\"function\"," \
           "\"function\":{\"name\":\"hw_register_read\"," \
                         "\"arguments\":\"{\\\"address\\\":\\\"" addr0 "\\\"}\"}}," \
          "{\"id\":\"" id1 "\",\"type\":\"function\"," \
           "\"function\":{\"name\":\"hw_register_read\"," \
                         "\"arguments\":\"{\\\"address\\\":\\\"" addr1 "\\\"}\"}}"\
        "]}," \
      "\"finish_reason\":\"tool_calls\"}]}"

/* Unknown tool call */
#define RESP_UNKNOWN_TOOL(call_id) \
    "{\"choices\":[{" \
      "\"message\":{\"role\":\"assistant\",\"content\":null," \
        "\"tool_calls\":[{" \
          "\"id\":\"" call_id "\"," \
          "\"type\":\"function\"," \
          "\"function\":{\"name\":\"nonexistent_tool\",\"arguments\":\"{}\"}" \
        "}]}," \
      "\"finish_reason\":\"tool_calls\"}]}"

/* =========================================================================
 * Test fixture
 * ========================================================================= */

static ec_session_t s_session;
static ec_agent_t   s_agent;

static const ec_api_config_t s_cfg = {
    .base_url = "mock-host",
    .port     = 80,
    .api_key  = "test-key",
    .use_tls  = 0,
};

static void setup(void)
{
    mock_http_reset();
    ec_session_init(&s_session, "test system prompt");
    ec_agent_init(&s_agent, &s_cfg, "test-model", &s_session);
}

/* =========================================================================
 * Test 1 — Simple text response, no tools
 *
 * User:  "hello"
 * LLM:   text → "Hi there!"
 * Expect: 1 HTTP call, response == "Hi there!"
 * ========================================================================= */
static int test_simple_text_response(void)
{
    setup();
    mock_http_queue(RESP_TEXT("Hi there!"), 200);

    char response[EC_CONFIG_CONTENT_BUF];
    int rc = ec_agent_run_turn(&s_agent, "hello", response, sizeof(response));

    ASSERT_EQ(rc, 0, "run_turn should succeed");
    ASSERT_EQ(mock_http_call_count(), 1, "exactly one HTTP call");
    ASSERT_STR(response, "Hi there!", "response contains expected text");
    return 1;
}

/* =========================================================================
 * Test 2 — Single register read
 *
 * User:  "read register 0x40000000"
 * LLM 1: tool_call → hw_register_read(0x40000000)
 * Tool:  reads mock register → 0x00000000 (initial value)
 * LLM 2: text → "The value is 0x00000000."
 * Expect: 2 HTTP calls, tool was dispatched, response contains value
 * ========================================================================= */
static int test_single_register_read(void)
{
    setup();
    mock_http_queue(RESP_REG_READ("call_001", "0x40000000"), 200);
    mock_http_queue(RESP_TEXT("The value is 0x00000000."), 200);

    char response[EC_CONFIG_CONTENT_BUF];
    int rc = ec_agent_run_turn(&s_agent, "read register 0x40000000",
                               response, sizeof(response));

    ASSERT_EQ(rc, 0, "run_turn should succeed");
    ASSERT_EQ(mock_http_call_count(), 2, "two HTTP calls (tool loop + final)");
    ASSERT_STR(response, "0x00000000", "response contains register value");

    /* Verify the second request included the tool result in messages */
    const char *req2 = mock_http_req_body(1);
    ASSERT(req2 != NULL, "second request body captured");
    ASSERT_STR(req2, "\"role\":\"tool\"", "second request includes tool result");
    ASSERT_STR(req2, "hw_register_read", "second request references tool name");
    return 1;
}

/* =========================================================================
 * Test 3 — Register write then read across two turns
 *
 * Turn 1: write 0xDEADBEEF to 0x40000004
 *   LLM 1: tool_call → hw_register_write(0x40000004, 0xDEADBEEF)
 *   LLM 2: text → "Written."
 * Turn 2: read 0x40000004
 *   LLM 3: tool_call → hw_register_read(0x40000004)
 *   Tool:  must return 0xDEADBEEF (written in turn 1)
 *   LLM 4: text → "The value is 0xdeadbeef."
 * Expect: 4 total HTTP calls, read returns written value
 * ========================================================================= */
static int test_write_then_read(void)
{
    setup();

    /* Turn 1: write */
    mock_http_queue(RESP_REG_WRITE("call_w1", "0x40000004", "0xdeadbeef"), 200);
    mock_http_queue(RESP_TEXT("Written successfully."), 200);

    char response[EC_CONFIG_CONTENT_BUF];
    int rc = ec_agent_run_turn(&s_agent,
                               "write 0xdeadbeef to register 0x40000004",
                               response, sizeof(response));
    ASSERT_EQ(rc, 0, "write turn should succeed");
    ASSERT_EQ(mock_http_call_count(), 2, "two HTTP calls for write turn");

    /* Turn 2: read — tool must return the previously written value */
    mock_http_queue(RESP_REG_READ("call_r1", "0x40000004"), 200);
    mock_http_queue(RESP_TEXT("The value is 0xdeadbeef."), 200);

    rc = ec_agent_run_turn(&s_agent,
                           "what is the value at register 0x40000004?",
                           response, sizeof(response));
    ASSERT_EQ(rc, 0, "read turn should succeed");
    ASSERT_EQ(mock_http_call_count(), 4, "four total HTTP calls");

    /* The tool result in the third request should contain 0xdeadbeef */
    const char *req3 = mock_http_req_body(2);
    ASSERT(req3 != NULL, "third request body captured");
    ASSERT_STR(req3, "deadbeef", "third request carries correct written value");

    ASSERT_STR(response, "0xdeadbeef", "final response mentions written value");
    return 1;
}

/* =========================================================================
 * Test 4 — Two tool calls in a single LLM response
 *
 * LLM returns two hw_register_read calls at different addresses in one reply.
 * Both must be dispatched before the follow-up request is sent.
 * Expect: 2 HTTP calls total, second request contains two tool results
 * ========================================================================= */
static int test_two_tool_calls_one_response(void)
{
    setup();
    mock_http_queue(
        RESP_TWO_READS("call_a", "0x40000000", "call_b", "0x40000008"),
        200);
    mock_http_queue(RESP_TEXT("Both registers read."), 200);

    char response[EC_CONFIG_CONTENT_BUF];
    int rc = ec_agent_run_turn(&s_agent, "read registers at 0x40000000 and 0x40000008",
                               response, sizeof(response));

    ASSERT_EQ(rc, 0, "run_turn should succeed");
    ASSERT_EQ(mock_http_call_count(), 2, "two HTTP calls");

    /* Second request must carry both tool results */
    const char *req2 = mock_http_req_body(1);
    ASSERT(req2 != NULL, "second request body captured");

    /* Count occurrences of "role":"tool" */
    int tool_result_count = 0;
    const char *p = req2;
    while ((p = strstr(p, "\"role\":\"tool\"")) != NULL) {
        tool_result_count++;
        p++;
    }
    ASSERT_EQ(tool_result_count, 2, "second request contains two tool results");
    return 1;
}

/* =========================================================================
 * Test 5 — Max iteration limit
 *
 * The mock always returns a tool call.  The agent must stop after
 * EC_CONFIG_MAX_AGENT_ITERS iterations and return EC_AGENT_ERR_MAX_ITERS.
 * ========================================================================= */
static int test_max_iterations(void)
{
    setup();

    /* Queue more responses than the limit */
    for (int i = 0; i < EC_CONFIG_MAX_AGENT_ITERS + 2; i++) {
        mock_http_queue(RESP_REG_READ("call_inf", "0x40000000"), 200);
    }

    char response[EC_CONFIG_CONTENT_BUF];
    int rc = ec_agent_run_turn(&s_agent, "loop forever", response, sizeof(response));

    ASSERT_EQ(rc, EC_AGENT_ERR_MAX_ITERS, "should hit max iteration limit");
    ASSERT_EQ(mock_http_call_count(), EC_CONFIG_MAX_AGENT_ITERS,
              "HTTP calls == max iterations");
    return 1;
}

/* =========================================================================
 * Test 6 — Unknown tool (graceful degradation)
 *
 * LLM calls a tool that is not registered.
 * ec_tool_dispatch returns a JSON error string.
 * The agent feeds that error back to the LLM and the LLM responds with text.
 * Expect: agent completes successfully (rc == 0), no crash.
 * ========================================================================= */
static int test_unknown_tool(void)
{
    setup();
    mock_http_queue(RESP_UNKNOWN_TOOL("call_bad"), 200);
    mock_http_queue(RESP_TEXT("I could not use that tool."), 200);

    char response[EC_CONFIG_CONTENT_BUF];
    int rc = ec_agent_run_turn(&s_agent, "do something", response, sizeof(response));

    ASSERT_EQ(rc, 0, "agent should complete despite unknown tool");
    ASSERT_EQ(mock_http_call_count(), 2, "two HTTP calls");

    /* Second request must contain a tool result with an error */
    const char *req2 = mock_http_req_body(1);
    ASSERT(req2 != NULL, "second request body captured");
    ASSERT_STR(req2, "\"role\":\"tool\"", "error fed back as tool result");
    ASSERT_STR(req2, "unknown tool", "error message forwarded to LLM");
    return 1;
}

/* =========================================================================
 * Test 7 — Session persistence across turns
 *
 * Turn 1: "my name is Alice"  → LLM: "Hello Alice."
 * Turn 2: "what is my name?"  → LLM: "Your name is Alice."
 *
 * The second HTTP request must include the full turn-1 history so the LLM
 * can answer correctly.  We verify by inspecting the request body.
 * ========================================================================= */
static int test_session_persistence(void)
{
    setup();

    /* Turn 1 */
    mock_http_queue(RESP_TEXT("Hello Alice."), 200);
    char response[EC_CONFIG_CONTENT_BUF];
    int rc = ec_agent_run_turn(&s_agent, "my name is Alice", response, sizeof(response));
    ASSERT_EQ(rc, 0, "turn 1 should succeed");

    /* Turn 2 */
    mock_http_queue(RESP_TEXT("Your name is Alice."), 200);
    rc = ec_agent_run_turn(&s_agent, "what is my name?", response, sizeof(response));
    ASSERT_EQ(rc, 0, "turn 2 should succeed");

    /* The second request body must contain the first user message */
    const char *req2 = mock_http_req_body(1);
    ASSERT(req2 != NULL, "second request body captured");
    ASSERT_STR(req2, "my name is Alice",
               "second request carries first user message in history");
    ASSERT_STR(req2, "Hello Alice",
               "second request carries first assistant reply in history");
    ASSERT_STR(req2, "what is my name",
               "second request contains new user message");
    return 1;
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void)
{
    /* Initialise skills once — registers tools into the global registry */
    ec_skill_init();

    printf("=== EmbedClaw End-to-End Tests ===\n\n");

    RUN_TEST(test_simple_text_response);
    RUN_TEST(test_single_register_read);
    RUN_TEST(test_write_then_read);
    RUN_TEST(test_two_tool_calls_one_response);
    RUN_TEST(test_max_iterations);
    RUN_TEST(test_unknown_tool);
    RUN_TEST(test_session_persistence);

    PRINT_RESULTS();
    return (_tests_pass == _tests_run) ? 0 : 1;
}
