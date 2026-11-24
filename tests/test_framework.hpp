#pragma once

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace faset_test {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

inline int g_assert_failures = 0;
inline bool g_current_failed = false;

struct Registrar {
    Registrar(const char* name, void (*fn)()) {
        registry().push_back({name, fn});
    }
};

inline int run_all() {
    int failed_tests = 0;
    const auto& cases = registry();
    std::printf("[faset_tests] Running %zu test(s)...\n", cases.size());
    for (const auto& tc : cases) {
        g_current_failed = false;
        tc.fn();
        if (g_current_failed) {
            std::printf("[  FAIL ] %s\n", tc.name);
            ++failed_tests;
        } else {
            std::printf("[  PASS ] %s\n", tc.name);
        }
    }
    std::printf("[faset_tests] Summary: %zu total, %zu passed, %d failed (%d assertion failure(s))\n",
                cases.size(), cases.size() - static_cast<size_t>(failed_tests), failed_tests, g_assert_failures);
    return failed_tests == 0 ? 0 : 1;
}

}  // namespace faset_test

#define TEST(name) \
    static void faset_test_fn_##name(); \
    static ::faset_test::Registrar faset_test_reg_##name(#name, &faset_test_fn_##name); \
    static void faset_test_fn_##name()

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "    CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++::faset_test::g_assert_failures; \
            ::faset_test::g_current_failed = true; \
        } \
    } while (0)

#define CHECK_EQ(a, b) \
    do { \
        auto _va = (a); \
        auto _vb = (b); \
        if (!(_va == _vb)) { \
            std::fprintf(stderr, "    CHECK_EQ failed at %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); \
            ++::faset_test::g_assert_failures; \
            ::faset_test::g_current_failed = true; \
        } \
    } while (0)

#define CHECK_NEAR(a, b, eps) \
    do { \
        double _va = static_cast<double>(a); \
        double _vb = static_cast<double>(b); \
        double _ve = static_cast<double>(eps); \
        if (!(std::fabs(_va - _vb) <= _ve)) { \
            std::fprintf(stderr, "    CHECK_NEAR failed at %s:%d: |%.8g - %.8g| > %.8g (%s vs %s)\n", \
                         __FILE__, __LINE__, _va, _vb, _ve, #a, #b); \
            ++::faset_test::g_assert_failures; \
            ::faset_test::g_current_failed = true; \
        } \
    } while (0)
