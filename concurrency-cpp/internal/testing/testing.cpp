#include "internal/testing/testing.hpp"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <vector>

namespace testing {
namespace {

using clock_type = std::chrono::steady_clock;

struct testCase {
    std::string name;
    TestFunc fn;
};

struct benchCase {
    std::string name;
    BenchFunc fn;
};

std::vector<testCase>& tests() {
    static std::vector<testCase> v;
    return v;
}

std::vector<benchCase>& benchmarks() {
    static std::vector<benchCase> v;
    return v;
}

std::mutex outMu;
bool verbose = false;

// Сторож следит за тем, сколько идёт текущий тест. Зависшая реализация
// держит рабочие потоки вечно, дожидаться их бессмысленно — сторож печатает,
// на чём всё встало, и убивает процесс.
std::atomic<long long> deadline{0};
std::atomic<const char*> running{nullptr};
std::atomic<bool> failing{false};
double limitPerTest = 120;

void setDeadline(double after) {
    deadline.store((clock_type::now() + std::chrono::duration<double>(after))
                       .time_since_epoch()
                       .count());
}

void watch(const char* name) {
    running.store(name);
    failing.store(false);
    setDeadline(limitPerTest);
}

void unwatch() { deadline.store(0); }

void grace() {
    if (!failing.exchange(true)) {
        setDeadline(5);
    }
}

void resume() {
    failing.store(false);
    setDeadline(limitPerTest);
}

void startWatchdog() {
    std::thread([] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            const long long at = deadline.load();
            if (at == 0 || clock_type::now().time_since_epoch().count() <= at) {
                continue;
            }

            const char* name = running.load();
            name = name != nullptr ? name : "тест";
            if (failing.load()) {
                std::fprintf(stderr, "паника: %s не завершился после ошибки — "
                                     "рабочие потоки остались внутри примитива\n",
                             name);
            } else {
                std::fprintf(stderr, "паника: %s не уложился в %.0fs — реализация зависла\n",
                             name, limitPerTest);
            }
            std::fflush(stderr);
            std::fflush(stdout);
            std::_Exit(2);
        }
    }).detach();
}

std::string format(const char* f, va_list ap) {
    va_list copy;
    va_copy(copy, ap);
    const int n = std::vsnprintf(nullptr, 0, f, copy);
    va_end(copy);
    if (n < 0) {
        return f;
    }

    std::string s(static_cast<size_t>(n), '\0');
    std::vsnprintf(s.data(), static_cast<size_t>(n) + 1, f, ap);
    return s;
}

double seconds(clock_type::time_point start) {
    return std::chrono::duration<double>(clock_type::now() - start).count();
}

}  // namespace

void registerTest(const char* name, TestFunc fn) { tests().push_back({name, fn}); }

void registerBenchmark(const char* name, BenchFunc fn) { benchmarks().push_back({name, fn}); }

void T::mark(const std::string& msg) {
    std::lock_guard<std::mutex> g(outMu);
    for (T* p = this; p != nullptr; p = p->parent_) {
        p->failed_.store(true);
    }
    std::fprintf(stderr, "    %s: %s\n", name_.c_str(), msg.c_str());
    std::fflush(stderr);
    grace();
}

void T::Fatal(const std::string& msg) {
    mark(msg);
    throw FatalError{msg};
}

void T::Fatalf(const char* f, ...) {
    va_list ap;
    va_start(ap, f);
    const std::string msg = format(f, ap);
    va_end(ap);
    Fatal(msg);
}

void T::Error(const std::string& msg) { mark(msg); }

void T::Errorf(const char* f, ...) {
    va_list ap;
    va_start(ap, f);
    const std::string msg = format(f, ap);
    va_end(ap);
    Error(msg);
}

void T::Run(const std::string& name, const std::function<void(T&)>& f) {
    T sub(name_ + "/" + name, this);

    if (verbose) {
        std::lock_guard<std::mutex> g(outMu);
        std::printf("=== RUN   %s\n", sub.name_.c_str());
        std::fflush(stdout);
    }

    const auto start = clock_type::now();
    try {
        f(sub);
    } catch (const FatalError&) {
    } catch (const std::exception& e) {
        sub.mark(std::string("исключение: ") + e.what());
    } catch (...) {
        sub.mark("неизвестное исключение");
    }
    const double took = seconds(start);

    std::lock_guard<std::mutex> g(outMu);
    if (sub.failed_.load()) {
        std::printf("    --- FAIL: %s (%.2fs)\n", sub.name_.c_str(), took);
    } else if (verbose) {
        std::printf("    --- PASS: %s (%.2fs)\n", sub.name_.c_str(), took);
    }
    std::fflush(stdout);
    resume();
}

void B::RunParallel(const std::function<void(PB&)>& body) {
    unsigned procs = std::thread::hardware_concurrency();
    if (procs == 0) {
        procs = 1;
    }

    const long per = (n_ + procs - 1) / static_cast<long>(procs);
    long left = n_;

    std::vector<std::thread> workers;
    for (unsigned i = 0; i < procs; i++) {
        const long share = std::min(per, left);
        left -= share;
        workers.emplace_back([&body, share] {
            PB pb(share);
            body(pb);
        });
    }
    for (auto& w : workers) {
        w.join();
    }
}

namespace {

bool runTest(const testCase& tc) {
    T t(tc.name);
    watch(tc.name.c_str());

    if (verbose) {
        std::lock_guard<std::mutex> g(outMu);
        std::printf("=== RUN   %s\n", tc.name.c_str());
        std::fflush(stdout);
    }

    const auto start = clock_type::now();
    try {
        tc.fn(t);
    } catch (const FatalError&) {
    } catch (const std::exception& e) {
        t.Error(std::string("исключение: ") + e.what());
    } catch (...) {
        t.Error("неизвестное исключение");
    }
    const double took = seconds(start);
    unwatch();

    std::lock_guard<std::mutex> g(outMu);
    if (t.Failed()) {
        std::printf("--- FAIL: %s (%.2fs)\n", tc.name.c_str(), took);
    } else if (verbose) {
        std::printf("--- PASS: %s (%.2fs)\n", tc.name.c_str(), took);
    }
    std::fflush(stdout);

    return !t.Failed();
}

void runBenchmark(const benchCase& bc) {
    const long limit = 200000000;
    long n = 1;
    double took = 0;

    watch(bc.name.c_str());

    for (;;) {
        B b(n);
        const auto start = clock_type::now();
        bc.fn(b);
        took = seconds(start);

        if (took >= 1.0 || n >= limit) {
            break;
        }

        long next = took > 0 ? static_cast<long>(n * (1.2 / took)) : n * 100;
        next = std::max(next, n + 1);
        next = std::min(next, n * 100);
        n = std::min(next, limit);
    }

    unwatch();

    std::printf("%-32s %12ld %12.2f ns/op\n", bc.name.c_str(), n, took * 1e9 / double(n));
    std::fflush(stdout);
}

double parseDuration(const std::string& s) {
    char* end = nullptr;
    const double value = std::strtod(s.c_str(), &end);
    if (end == nullptr || *end == '\0' || std::strcmp(end, "s") == 0) {
        return value;
    }
    if (std::strcmp(end, "ms") == 0) {
        return value / 1000;
    }
    if (std::strcmp(end, "m") == 0) {
        return value * 60;
    }
    if (std::strcmp(end, "h") == 0) {
        return value * 3600;
    }
    return value;
}

bool flag(const char* arg, const char* name, std::string* value) {
    const std::string prefix = std::string("-") + name + "=";
    if (std::strncmp(arg, prefix.c_str(), prefix.size()) == 0) {
        *value = arg + prefix.size();
        return true;
    }
    const std::string longPrefix = "-" + prefix;
    if (std::strncmp(arg, longPrefix.c_str(), longPrefix.size()) == 0) {
        *value = arg + longPrefix.size();
        return true;
    }
    return false;
}

int runMain(int argc, char** argv) {
    std::string run = ".";
    std::string bench;
    std::string value;
    long count = 1;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--v") == 0) {
            verbose = true;
        } else if (flag(argv[i], "run", &value)) {
            run = value;
        } else if (flag(argv[i], "bench", &value)) {
            bench = value;
        } else if (flag(argv[i], "count", &value)) {
            count = std::strtol(value.c_str(), nullptr, 10);
        } else if (flag(argv[i], "timeout", &value)) {
            limitPerTest = parseDuration(value);
        } else {
            std::fprintf(stderr, "неизвестный флаг: %s\n", argv[i]);
            std::fprintf(stderr, "флаги: -run=РЕГЭКСП -bench=РЕГЭКСП -count=N -timeout=2m -v\n");
            return 2;
        }
    }

    if (limitPerTest > 0) {
        startWatchdog();
    }

    const std::regex runPattern(run);
    const std::regex benchPattern(bench.empty() ? "$^" : bench);

    bool ok = true;
    const auto start = clock_type::now();

    for (long i = 0; i < count; i++) {
        for (const auto& tc : tests()) {
            if (!std::regex_search(tc.name, runPattern)) {
                continue;
            }
            if (!runTest(tc)) {
                ok = false;
            }
        }
    }

    if (!bench.empty()) {
        for (const auto& bc : benchmarks()) {
            if (!std::regex_search(bc.name, benchPattern)) {
                continue;
            }
            runBenchmark(bc);
        }
    }

    const char* name = std::strrchr(argv[0], '/');
    name = name != nullptr ? name + 1 : argv[0];

    if (ok) {
        std::printf("PASS\nok  \t%s\t%.2fs\n", name, seconds(start));
    } else {
        std::printf("FAIL\nFAIL\t%s\t%.2fs\n", name, seconds(start));
    }
    std::fflush(stdout);

    return ok ? 0 : 1;
}

}  // namespace
}  // namespace testing

int main(int argc, char** argv) { return testing::runMain(argc, argv); }
