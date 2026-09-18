#include "spinlock.hpp"

#include <chrono>
#include <functional>
#include <future>
#include <utility>
#include <vector>

#include "internal/testing/testing.hpp"

namespace spinlock {
namespace {

using namespace std::chrono_literals;

std::future<void> finished(std::function<void()> f) {
    return std::async(std::launch::async, std::move(f));
}

bool ready(std::future<void>& f, std::chrono::nanoseconds d) {
    return f.wait_for(d) == std::future_status::ready;
}

void waitAll(std::vector<std::future<void>>& fs) {
    for (auto& f : fs) {
        f.get();
    }
}

template <typename F>
void each(testing::T& t, F f) {
    t.Helper();
    t.Run("Spinlock", [&f](testing::T& t) {
        Spinlock l;
        f(t, l);
    });
    t.Run("TTAS", [&f](testing::T& t) {
        TTAS l;
        f(t, l);
    });
}

}  // namespace

TEST(TestLockUnlock) {
    each(t, [](testing::T& t, auto& l) {
        l.Lock();
        l.Unlock();
        l.Lock();
        l.Unlock();
    });
}

TEST(TestMutualExclusion) {
    each(t, [](testing::T& t, auto& l) {
        const int threads = 4, iterations = 5000;
        int counter = 0;

        std::vector<std::future<void>> workers;
        for (int i = 0; i < threads; i++) {
            workers.push_back(finished([&] {
                for (int j = 0; j < iterations; j++) {
                    l.Lock();
                    counter++;
                    l.Unlock();
                }
            }));
        }
        waitAll(workers);

        if (const int want = threads * iterations; counter != want) {
            t.Fatalf("счётчик %d, ожидалось %d", counter, want);
        }
    });
}

TEST(TestOnlyOneInside) {
    each(t, [](testing::T& t, auto& l) {
        int inside = 0;
        bool bad = false;

        std::vector<std::future<void>> workers;
        for (int i = 0; i < 4; i++) {
            workers.push_back(finished([&] {
                for (int j = 0; j < 2000; j++) {
                    l.Lock();
                    inside++;
                    if (inside != 1) {
                        bad = true;
                    }
                    inside--;
                    l.Unlock();
                }
            }));
        }
        waitAll(workers);

        if (bad) {
            t.Fatal("в критической секции оказалось больше одного потока");
        }
    });
}

TEST(TestTryLock) {
    each(t, [](testing::T& t, auto& l) {
        if (!l.TryLock()) {
            t.Fatal("TryLock на свободном замке должен получиться");
        }

        auto got = std::async(std::launch::async, [&l] { return l.TryLock(); });

        if (got.wait_for(1s) != std::future_status::ready) {
            t.Fatal("TryLock заблокировался, а не должен");
        }
        if (got.get()) {
            t.Fatal("TryLock на занятом замке должен вернуть false");
        }

        l.Unlock();
        if (!l.TryLock()) {
            t.Fatal("после Unlock замок снова свободен");
        }
        l.Unlock();
    });
}

TEST(TestLockWaitsForUnlock) {
    each(t, [](testing::T& t, auto& l) {
        l.Lock();

        auto second = finished([&l] {
            l.Lock();
            l.Unlock();
        });

        if (ready(second, 50ms)) {
            t.Fatal("второй Lock прошёл, пока замок занят");
        }

        l.Unlock();

        if (!ready(second, 2s)) {
            t.Fatal("второй Lock не проснулся после Unlock");
        }
    });
}

TEST(TestUnlockWithoutLockThrows) {
    each(t, [](testing::T& t, auto& l) {
        try {
            l.Unlock();
        } catch (...) {
            return;
        }
        t.Fatal("Unlock без Lock должен бросать исключение");
    });
}

BENCHMARK(BenchmarkUncontended) {
    Spinlock l;
    for (long i = 0; i < b.N(); i++) {
        l.Lock();
        l.Unlock();
    }
}

BENCHMARK(BenchmarkContended) {
    Spinlock l;
    b.RunParallel([&l](testing::PB& pb) {
        while (pb.Next()) {
            l.Lock();
            l.Unlock();
        }
    });
}

}  // namespace spinlock
