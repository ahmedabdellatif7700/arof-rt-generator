// Minimal test harness: TEST(name) { CHECK(...); CHECK_NEAR(a, b, tol); }, one runner per file.
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace check {

struct Case {
    const char* name;
    std::function<void()> fn;
};
inline std::vector<Case>& cases() {
    static std::vector<Case> c;
    return c;
}
inline int& failures() {
    static int f = 0;
    return f;
}
struct Register {
    Register(const char* n, std::function<void()> f) { cases().push_back({n, std::move(f)}); }
};

inline void fail(const char* file, int line, const std::string& what) {
    std::printf("  FAIL %s:%d: %s\n", file, line, what.c_str());
    ++failures();
}

}  // namespace check

#define CHECK_CAT2(a, b) a##b
#define CHECK_CAT(a, b) CHECK_CAT2(a, b)
#define TEST(name)                                                              \
    static void name();                                                         \
    static check::Register CHECK_CAT(reg_, name)(#name, name);                  \
    static void name()

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) check::fail(__FILE__, __LINE__, #cond);                    \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                   \
    do {                                                                        \
        const double va_ = (a), vb_ = (b);                                      \
        if (!(std::fabs(va_ - vb_) <= (tol)))                                   \
            check::fail(__FILE__, __LINE__,                                     \
                        std::string(#a " = ") + std::to_string(va_) + ", " #b " = " + \
                            std::to_string(vb_) + ", tol " + std::to_string(tol)); \
    } while (0)

#define TEST_MAIN()                                                             \
    int main() {                                                                \
        for (auto& c : check::cases()) {                                        \
            const int before = check::failures();                               \
            c.fn();                                                             \
            std::printf("%s %s\n", check::failures() == before ? "ok  " : "FAIL", c.name); \
        }                                                                       \
        return check::failures() ? 1 : 0;                                       \
    }
