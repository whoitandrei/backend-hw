#include "semaphore.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <thread>
#include <utility>
#include <vector>

#include "internal/testing/testing.hpp"

namespace semaphore {
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

TEST(TestAcquireRelease) {
    Semaphore s(2);

    if (s.Available() != 2) {
        t.Fatalf("Available вернул %d, ожидалось 2", s.Available());
    }

    s.Acquire();
    s.Acquire();

    if (s.Available() != 0) {
        t.Fatalf("Available вернул %d, ожидалось 0", s.Available());
    }

    s.Release();
    s.Release();

    if (s.Available() != 2) {
        t.Fatalf("Available вернул %d, ожидалось 2", s.Available());
    }
}

TEST(TestBlocksWhenEmpty) {
    Semaphore s(1);
    s.Acquire();

    auto second = finished([&s] {
        s.Acquire();
        s.Release();
    });

    if (ready(second, 50ms)) {
        t.Fatal("Acquire прошёл, хотя разрешений нет");
    }

    s.Release();

    if (!ready(second, 2s)) {
        t.Fatal("Acquire не проснулся после Release");
    }
}

TEST(TestNeverMoreThanLimit) {
    const int limit = 3;
    Semaphore s(limit);

    std::atomic<int> inside{0}, peak{0};

    std::vector<std::future<void>> workers;
    for (int i = 0; i < 40; i++) {
        workers.push_back(finished([&] {
            s.Acquire();

            const int now = inside.fetch_add(1) + 1;
            int old = peak.load();
            while (now > old && !peak.compare_exchange_weak(old, now)) {
            }
            std::this_thread::sleep_for(1ms);
            inside.fetch_sub(1);

            s.Release();
        }));
    }

    auto done = finished([&workers] { waitAll(workers); });
    if (!ready(done, 10s)) {
        t.Fatal("потоки не дождались разрешений");
    }

    if (peak.load() > limit) {
        t.Fatalf("одновременно внутри было %d, лимит %d", peak.load(), limit);
    }
    if (peak.load() < 2) {
        t.Fatalf("параллелизма не случилось вовсе: пик %d", peak.load());
    }
}

TEST(TestTryAcquire) {
    Semaphore s(1);

    if (!s.TryAcquire()) {
        t.Fatal("TryAcquire при свободном разрешении должен получиться");
    }
    if (s.TryAcquire()) {
        t.Fatal("TryAcquire без разрешений должен вернуть false");
    }

    s.Release();

    if (!s.TryAcquire()) {
        t.Fatal("после Release разрешение снова доступно");
    }
}

TEST(TestZeroPermits) {
    Semaphore s(0);

    if (s.TryAcquire()) {
        t.Fatal("у семафора на ноль разрешений брать нечего");
    }

    auto waiting = finished([&s] { s.Acquire(); });
    if (ready(waiting, 50ms)) {
        t.Fatal("Acquire прошёл на пустом семафоре");
    }

    s.Release();

    if (!ready(waiting, 2s)) {
        t.Fatal("Acquire не проснулся");
    }
}

TEST(TestAllWaitersWakeUp) {
    Semaphore s(0);
    const int waiters = 30;

    std::vector<std::future<void>> workers;
    for (int i = 0; i < waiters; i++) {
        workers.push_back(finished([&s] { s.Acquire(); }));
    }

    std::this_thread::sleep_for(50ms);
    for (int i = 0; i < waiters; i++) {
        s.Release();
    }

    auto done = finished([&workers] { waitAll(workers); });
    if (!ready(done, 10s)) {
        t.Fatal("не все ждущие проснулись");
    }
}

BENCHMARK(BenchmarkAcquireRelease) {
    Semaphore s(1);
    for (long i = 0; i < b.N(); i++) {
        s.Acquire();
        s.Release();
    }
}

}  // namespace semaphore
