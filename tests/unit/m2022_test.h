/*
 * Minimal assertion harness for unit tests. Deliberately tiny: a failing check
 * prints file:line and the expression, the test binary exits non-zero, CTest
 * reports it. No framework to learn, nothing to link.
 */
#ifndef M2022_TEST_H
#define M2022_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int m2022_test_failures = 0;
static int m2022_test_checks = 0;

#define CHECK(expr)                                                                        \
    do {                                                                                   \
        m2022_test_checks++;                                                               \
        if (!(expr)) {                                                                     \
            m2022_test_failures++;                                                         \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr);       \
        }                                                                                  \
    } while (0)

#define CHECK_EQ_INT(a, b)                                                                 \
    do {                                                                                   \
        long long m2022_a = (long long)(a), m2022_b = (long long)(b);                      \
        m2022_test_checks++;                                                               \
        if (m2022_a != m2022_b) {                                                          \
            m2022_test_failures++;                                                         \
            fprintf(stderr, "%s:%d: CHECK_EQ_INT failed: %s == %lld, %s == %lld\n",        \
                    __FILE__, __LINE__, #a, m2022_a, #b, m2022_b);                         \
        }                                                                                  \
    } while (0)

#define CHECK_EQ_STR(a, b)                                                                 \
    do {                                                                                   \
        const char *m2022_a = (a), *m2022_b = (b);                                         \
        m2022_test_checks++;                                                               \
        if (!m2022_a || !m2022_b || strcmp(m2022_a, m2022_b) != 0) {                       \
            m2022_test_failures++;                                                         \
            fprintf(stderr, "%s:%d: CHECK_EQ_STR failed: \"%s\" vs \"%s\"\n", __FILE__,    \
                    __LINE__, m2022_a ? m2022_a : "(null)", m2022_b ? m2022_b : "(null)"); \
        }                                                                                  \
    } while (0)

#define CHECK_MEM_EQ(a, b, n)                                                              \
    do {                                                                                   \
        m2022_test_checks++;                                                               \
        if (memcmp((a), (b), (n)) != 0) {                                                  \
            m2022_test_failures++;                                                         \
            fprintf(stderr, "%s:%d: CHECK_MEM_EQ failed: %s vs %s (%zu bytes)\n",          \
                    __FILE__, __LINE__, #a, #b, (size_t)(n));                              \
        }                                                                                  \
    } while (0)

#define TEST_MAIN_END()                                                                    \
    do {                                                                                   \
        fprintf(stderr, "%s: %d checks, %d failures\n", __FILE__, m2022_test_checks,       \
                m2022_test_failures);                                                      \
        return m2022_test_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;                     \
    } while (0)

#endif /* M2022_TEST_H */
