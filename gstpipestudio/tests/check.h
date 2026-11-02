// check.h – the smallest test harness that does the job: CHECK, CHECK_EQ and a summary line.
#pragma once

#include <cstdio>
#include <sstream>
#include <string>

inline int g_checks = 0;
inline int g_failures = 0;

#define CHECK(condition)                                                              \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(condition)) {                                                           \
            ++g_failures;                                                             \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);          \
        }                                                                             \
    } while (0)

// Prints both values on a mismatch. Needs operator<< for the type.
#define CHECK_EQ(actual, expected)                                                            \
    do {                                                                                      \
        ++g_checks;                                                                           \
        auto&& a_ = (actual);                                                                 \
        auto&& e_ = (expected);                                                               \
        if (!(a_ == e_)) {                                                                    \
            ++g_failures;                                                                     \
            std::ostringstream sa_, se_;                                                      \
            sa_ << a_;                                                                        \
            se_ << e_;                                                                        \
            std::printf("FAIL %s:%d: %s == %s\n    got [%s]\n    want [%s]\n", __FILE__, __LINE__, \
                        #actual, #expected, sa_.str().c_str(), se_.str().c_str());            \
        }                                                                                     \
    } while (0)

inline int finish(const char* name) {
    std::printf("%s: %d checks, %d failures\n", name, g_checks, g_failures);
    return g_failures ? 1 : 0;
}
