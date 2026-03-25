#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include <stdio.h>

/*
 * Minimal test framework — no external dependencies.
 *
 * Test functions have signature:  int test_xxx(void)
 * Return 1 = pass, 0 = fail.
 *
 * Usage:
 *   RUN_TEST(test_foo);   // in main()
 */

static int _tests_run  = 0;
static int _tests_pass = 0;

#define ASSERT(cond, msg)                                            \
    do {                                                             \
        if (!(cond)) {                                               \
            printf("  FAIL  %s:%d  %s\n", __FILE__, __LINE__, msg); \
            return 0;                                                \
        }                                                            \
    } while (0)

#define ASSERT_EQ(a, b, msg)   ASSERT((a) == (b),  msg)
#define ASSERT_STR(s, sub, msg) ASSERT(strstr((s),(sub)) != NULL, msg)

#define RUN_TEST(fn)                                         \
    do {                                                     \
        printf("[ RUN  ] " #fn "\n");                        \
        _tests_run++;                                        \
        int _r = fn();                                       \
        if (_r) { _tests_pass++; printf("[ PASS ]\n"); }    \
        else             { printf("[ FAIL ]\n"); }           \
    } while (0)

#define PRINT_RESULTS()                                              \
    printf("\n%d/%d tests passed\n", _tests_pass, _tests_run)

#endif /* TEST_RUNNER_H */
