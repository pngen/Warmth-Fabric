#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <functional>

namespace wtest {
struct Case { std::string name; std::function<void()> fn; };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
struct Registrar { Registrar(const std::string& n, std::function<void()> f) { registry().push_back({n, f}); } };
inline int& failures() { static int f = 0; return f; }
inline void check(bool cond, const char* expr, const char* file, int line) {
    if (!cond) { std::printf("FAIL %s:%d: %s\n", file, line, expr); ++failures(); }
}
inline int run_all() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    for (auto& c : registry()) { std::printf("[RUN] %s\n", c.name.c_str()); c.fn(); std::fflush(stdout); }
    if (failures() == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures()); return 1;
}
}
#define WTEST(name) static void name(); static wtest::Registrar reg_##name(#name, name); static void name()
#define CHECK(cond) wtest::check((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a,b) do { auto _a=(a); auto _b=(b); if(!(_a==_b)) { std::printf("FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); ++wtest::failures(); } } while(0)
#define RUN_TESTS() return wtest::run_all()
