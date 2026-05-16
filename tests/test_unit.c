#include "test_runner.h"
#include "ec_json.h"
#include "ec_session.h"

#include <string.h>

static int test_json_writer_escapes_and_composes(void)
{
    char buf[256];
    ec_json_writer_t w;

    ec_json_writer_init(&w, buf, sizeof(buf));
    ec_json_obj_start(&w);
    ec_json_add_string(&w, "text", "quote: \" slash: \\ newline:\n");
    ec_json_add_int(&w, "count", 3);
    ec_json_array_start(&w, "items");
    ec_json_array_obj_start(&w);
    ec_json_add_string(&w, "name", "uart0");
    ec_json_obj_end(&w);
    ec_json_array_end(&w);
    ec_json_obj_end(&w);

    ASSERT(ec_json_writer_finish(&w) > 0, "writer should finish");
    ASSERT_STR(buf, "\"text\":\"quote: \\\" slash: \\\\ newline:\\n\"",
               "writer should escape special characters");
    ASSERT_STR(buf, "\"count\":3", "writer should include integer fields");
    ASSERT_STR(buf, "\"items\":[{\"name\":\"uart0\"}]",
               "writer should compose arrays of objects");
    return 1;
}

static int test_json_writer_reports_overflow(void)
{
    char buf[24];
    ec_json_writer_t w;

    ec_json_writer_init(&w, buf, sizeof(buf));
    ec_json_obj_start(&w);
    ec_json_add_string(&w, "too_long", "abcdefghijklmnopqrstuvwxyz");
    ec_json_obj_end(&w);

    ASSERT_EQ(ec_json_writer_finish(&w), -1, "small buffer should overflow");
    return 1;
}

static int test_json_find_nested_strings_and_bounds(void)
{
    const char json[] =
        "{\"choices\":[{\"message\":{\"content\":\"hello\","
        "\"tool_calls\":[{\"function\":{\"name\":\"hw_register_read\"}}]}}]}";
    char out[32];

    ASSERT_EQ(ec_json_find_string(json, strlen(json),
                                  "choices[0].message.content",
                                  out, sizeof(out)), 5,
              "nested string lookup should return length");
    ASSERT_EQ(strcmp(out, "hello"), 0, "nested string lookup should copy value");

    ASSERT(ec_json_find_string(json, strlen(json),
                               "choices[0].message.tool_calls[0].function.name",
                               out, sizeof(out)) > 0,
           "array path lookup should succeed");
    ASSERT_EQ(strcmp(out, "hw_register_read"), 0,
              "array path lookup should copy tool name");

    ASSERT(ec_json_find_string(json, strlen(json),
                               "choices[1].message.content",
                               out, sizeof(out)) < 0,
           "out-of-range array lookup should fail");
    memset(out, 'x', sizeof(out));
    ASSERT_EQ(ec_json_find_string(json, strlen(json),
                                  "choices[0].message.content",
                                  out, 4), 5,
              "undersized output buffer should still report full value length");
    ASSERT_EQ(strcmp(out, "hel"), 0,
              "undersized output buffer should contain a safe truncated string");
    return 1;
}

static int test_session_message_view_and_reset(void)
{
    ec_session_t session;
    ec_model_tool_call_t call;
    size_t count = 0;

    memset(&call, 0, sizeof(call));
    strcpy(call.id, "call_1");
    strcpy(call.name, "hw_register_read");
    strcpy(call.arguments, "{\"address\":\"0x40000000\"}");

    ec_session_init(&session, "system prompt");
    ASSERT_EQ(ec_session_append(&session, "user", "hello"), 0,
              "user append should succeed");
    ASSERT_EQ(ec_session_append_tool_calls(&session, &call, 1), 0,
              "assistant tool-call append should succeed");
    ASSERT_EQ(ec_session_append_tool_result(&session, "call_1",
                                            "{\"value\":\"0x00000000\"}"), 0,
              "tool result append should succeed");

    const ec_model_message_t *messages = ec_session_messages(&session, &count);
    ASSERT_EQ(count, 4, "message view should include system plus three entries");
    ASSERT_EQ(strcmp(messages[0].role, "system"), 0, "first message is system");
    ASSERT_EQ(strcmp(messages[1].role, "user"), 0, "second message is user");
    ASSERT_EQ(strcmp(messages[2].role, "assistant"), 0,
              "third message is assistant tool call");
    ASSERT_EQ(messages[2].num_tool_calls, 1, "assistant view should expose tool call");
    ASSERT_EQ(strcmp(messages[2].tool_calls[0].id, "call_1"), 0,
              "tool call id should be preserved");
    ASSERT_EQ(strcmp(messages[3].role, "tool"), 0, "fourth message is tool result");
    ASSERT_EQ(strcmp(messages[3].tool_call_id, "call_1"), 0,
              "tool result call id should be preserved");

    ec_session_reset(&session);
    messages = ec_session_messages(&session, &count);
    ASSERT_EQ(count, 1, "reset should retain only system prompt");
    ASSERT_EQ(strcmp(messages[0].content, "system prompt"), 0,
              "system prompt should survive reset");
    return 1;
}

static int test_session_rejects_overflow(void)
{
    ec_session_t session;
    ec_session_init(&session, "system prompt");

    for (int i = 0; i < EC_CONFIG_MAX_HISTORY; i++) {
        ASSERT_EQ(ec_session_append(&session, "user", "x"), 0,
                  "append before max history should succeed");
    }
    ASSERT_EQ(ec_session_append(&session, "user", "overflow"), -1,
              "append past max history should fail");
    return 1;
}

int main(void)
{
    printf("=== EmbedClaw Unit Tests ===\n\n");

    RUN_TEST(test_json_writer_escapes_and_composes);
    RUN_TEST(test_json_writer_reports_overflow);
    RUN_TEST(test_json_find_nested_strings_and_bounds);
    RUN_TEST(test_session_message_view_and_reset);
    RUN_TEST(test_session_rejects_overflow);

    PRINT_RESULTS();
    return (_tests_pass == _tests_run) ? 0 : 1;
}
