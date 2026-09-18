#include "mutex.hpp"

#include <chrono>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "internal/testing/testing.hpp"

namespace mutex {
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

}  // namespace

TEST(TestLockUnlock) {
    Mutex m;
    m.Lock();
    m.Unlock();
    m.Lock();
    m.Unlock();
}

TEST(TestMutualExclusion) {
    const int threads = 16, iterations = 20000;
    Mutex m;
    int counter = 0;

    std::vector<std::future<void>> workers;
    for (int i = 0; i < threads; i++) {
        workers.push_back(finished([&] {
            for (int j = 0; j < iterations; j++) {
                m.Lock();
                counter++;
                m.Unlock();
            }
        }));
    }
    waitAll(workers);

    if (const int want = threads * iterations; counter != want) {
        t.Fatalf("счётчик %d, ожидалось %d", counter, want);
    }
}

TEST(TestOnlyOneInside) {
    Mutex m;
    int inside = 0;
    bool bad = false;

    std::vector<std::future<void>> workers;
    for (int i = 0; i < 16; i++) {
        workers.push_back(finished([&] {
            for (int j = 0; j < 3000; j++) {
                m.Lock();
                inside++;
                if (inside != 1) {
                    bad = true;
                }
                inside--;
                m.Unlock();
            }
        }));
    }
    waitAll(workers);

    if (bad) {
        t.Fatal("в критической секции оказалось больше одного потока");
    }
}

TEST(TestLockWaitsForUnlock) {
    Mutex m;
    m.Lock();

    auto second = finished([&m] {
        m.Lock();
        m.Unlock();
    });

    if (ready(second, 50ms)) {
        t.Fatal("второй Lock прошёл, пока мьютекс занят");
    }

    m.Unlock();

    if (!ready(second, 2s)) {
        t.Fatal("второй Lock не проснулся после Unlock");
    }
}

TEST(TestEveryWaiterWakesUp) {
    Mutex m;
    const int waiters = 50;

    m.Lock();

    std::vector<std::future<void>> workers;
    for (int i = 0; i < waiters; i++) {
        workers.push_back(finished([&m] {
            m.Lock();
            std::this_thread::sleep_for(1ms);
            m.Unlock();
        }));
    }

    std::this_thread::sleep_for(50ms);
    m.Unlock();

    auto done = finished([&workers] { waitAll(workers); });
    if (!ready(done, 10s)) {
        t.Fatal("часть потоков осталась спать навсегда");
    }
}

TEST(TestTryLock) {
    Mutex m;

    if (!m.TryLock()) {
        t.Fatal("TryLock на свободном мьютексе должен получиться");
    }

    auto got = std::async(std::launch::async, [&m] { return m.TryLock(); });

    if (got.wait_for(1s) != std::future_status::ready) {
        t.Fatal("TryLock заблокировался, а не должен");
    }
    if (got.get()) {
        t.Fatal("TryLock на занятом мьютексе должен вернуть false");
    }

    m.Unlock();
    if (!m.TryLock()) {
        t.Fatal("после Unlock мьютекс свободен");
    }
    m.Unlock();
}

TEST(TestUnlockWithoutLockThrows) {
    Mutex m;
    try {
        m.Unlock();
    } catch (...) {
        return;
    }
    t.Fatal("Unlock без Lock должен бросать исключение");
}

TEST(TestHandoffUnderLoad) {
    Mutex m;
    const auto deadline = std::chrono::steady_clock::now() + 300ms;
    int passes[8] = {};

    std::vector<std::future<void>> workers;
    for (int i = 0; i < 8; i++) {
        workers.push_back(finished([&m, &passes, deadline, i] {
            while (std::chrono::steady_clock::now() < deadline) {
                m.Lock();
                passes[i]++;
                m.Unlock();
            }
        }));
    }

    auto done = finished([&workers] { waitAll(workers); });
    if (!ready(done, 5s)) {
        t.Fatal("поток завис");
    }

    for (int i = 0; i < 8; i++) {
        if (passes[i] == 0) {
            t.Fatal("поток не получил мьютекс ни разу");
        }
    }
}

BENCHMARK(BenchmarkUncontended) {
    Mutex m;
    for (long i = 0; i < b.N(); i++) {
        m.Lock();
        m.Unlock();
    }
}

BENCHMARK(BenchmarkContended) {
    Mutex m;
    b.RunParallel([&m](testing::PB& pb) {
        while (pb.Next()) {
            m.Lock();
            m.Unlock();
        }
    });
}

BENCHMARK(BenchmarkStdlibContended) {
    std::mutex m;
    b.RunParallel([&m](testing::PB& pb) {
        while (pb.Next()) {
            m.lock();
            m.unlock();
        }
    });
}

}  // namespace mutex
