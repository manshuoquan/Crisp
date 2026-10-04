// Test helpers: a tiny assertion-based test harness.
// Hand-written, dependency-free (project rule: no mock frameworks).

#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace test {

/// Simple registry of named test cases; run all at program exit via main().
class CtTestRegistry {
public:
    using Case = std::pair<std::string, std::function<void()>>;

    static CtTestRegistry& instance() {
        static CtTestRegistry reg;
        return reg;
    }

    void add(std::string name, std::function<void()> fn) {
        m_cases.emplace_back(std::move(name), std::move(fn));
    }

    int runAll() {
        int failures = 0;
        for (auto& [name, fn] : m_cases) {
            std::printf("[ RUN  ] %s\n", name.c_str());
            const int before = g_assertFailures;
            try {
                fn();
            } catch (const std::exception& e) {
                std::printf("[ ERROR] %s: %s\n", name.c_str(), e.what());
                ++failures;
                continue;
            }
            if (g_assertFailures == before) {
                std::printf("[  OK  ] %s\n", name.c_str());
            } else {
                std::printf("[ FAIL ] %s\n", name.c_str());
                ++failures;
            }
        }
        return failures;
    }

    static inline int g_assertFailures = 0;
    static inline const char* g_currentFile = "";
    static inline int g_currentLine = 0;

private:
    std::vector<Case> m_cases;
};

/// Registers a test case at static-init time (before main).
struct CtTestRegistrar {
    CtTestRegistrar(std::string name, std::function<void()> fn) {
        CtTestRegistry::instance().add(std::move(name), std::move(fn));
    }
};

} // namespace test

// Helper: register a test.
#define TEST_CASE(name)                                                    \
    static void name();                                                    \
    static ::test::CtTestRegistrar name##_registrar(#name, name);          \
    static void name()

// Assertion macros: record failure and continue (fail fast per test).
#define EXPECT_TRUE(cond)                                                          \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::printf("  EXPECT_TRUE failed at %s:%d: %s\n", __FILE__, __LINE__, \
                        #cond);                                                    \
            ++::test::CtTestRegistry::g_assertFailures;                            \
        }                                                                          \
    } while (0)

#define EXPECT_EQ(a, b)                                                            \
    do {                                                                           \
        auto va = (a);                                                             \
        auto vb = (b);                                                             \
        if (!(va == vb)) {                                                         \
            std::printf("  EXPECT_EQ failed at %s:%d: %s == %s\n", __FILE__,       \
                        __LINE__, #a, #b);                                         \
            ++::test::CtTestRegistry::g_assertFailures;                            \
        }                                                                          \
    } while (0)

#define EXPECT_NE(a, b)                                                            \
    do {                                                                           \
        auto va = (a);                                                             \
        auto vb = (b);                                                             \
        if (va == vb) {                                                            \
            std::printf("  EXPECT_NE failed at %s:%d: %s != %s\n", __FILE__,       \
                        __LINE__, #a, #b);                                         \
            ++::test::CtTestRegistry::g_assertFailures;                            \
        }                                                                          \
    } while (0)
