#include "rwmutex.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <thread>
#include <utility>
#include <vector>

#include "internal/testing/testing.hpp"

namespace rwmutex {
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

TEST(TestWriterExcludesEveryone) {
    RWMutex rw;
    rw.Lock();

    auto reader = finished([&rw] {
        rw.RLock();
        rw.RUnlock();
    });
    auto second = finished([&rw] {
        rw.Lock();
        rw.Unlock();
    });

    if (ready(reader, 50ms)) {
        t.Fatal("читатель прошёл, пока держит писатель");
    }
    if (ready(second, 0ms)) {
        t.Fatal("второй писатель прошёл, пока держит первый");
    }

    rw.Unlock();

    if (!ready(reader, 2s)) {
        t.Fatal("читатель не проснулся");
    }
    if (!ready(second, 2s)) {
        t.Fatal("писатель не проснулся");
    }
}

TEST(TestReadersGoTogether) {
    RWMutex rw;
    const int readers = 8;

    std::atomic<int> inside{0}, peak{0};

    std::vector<std::future<void>> workers;
    for (int i = 0; i < readers; i++) {
        workers.push_back(finished([&] {
            rw.RLock();

            const int now = inside.fetch_add(1) + 1;
            int old = peak.load();
            while (now > old && !peak.compare_exchange_weak(old, now)) {
            }
            std::this_thread::sleep_for(30ms);
            inside.fetch_sub(1);

            rw.RUnlock();
        }));
    }

    auto done = finished([&workers] { waitAll(workers); });
    if (!ready(done, 5s)) {
        t.Fatal("читатели зависли");
    }

    if (peak.load() < 2) {
        t.Fatalf("читатели шли по одному (пик %d) — смысл RWMutex теряется", peak.load());
    }
}

TEST(TestWriterWaitsForReaders) {
    RWMutex rw;
    rw.RLock();
    rw.RLock();

    auto writer = finished([&rw] {
        rw.Lock();
        rw.Unlock();
    });

    if (ready(writer, 50ms)) {
        t.Fatal("писатель прошёл при живых читателях");
    }

    rw.RUnlock();

    if (ready(writer, 50ms)) {
        t.Fatal("писатель прошёл, пока остался один читатель");
    }

    rw.RUnlock();

    if (!ready(writer, 2s)) {
        t.Fatal("писатель не проснулся после последнего RUnlock");
    }
}

TEST(TestNoTornState) {
    RWMutex rw;
    int shared = 0;
    std::atomic<int> bad{0};

    std::vector<std::future<void>> workers;

    for (int i = 0; i < 4; i++) {
        workers.push_back(finished([&] {
            for (int j = 0; j < 5000; j++) {
                rw.Lock();
                shared++;
                shared++;
                rw.Unlock();
            }
        }));
    }

    for (int i = 0; i < 8; i++) {
        workers.push_back(finished([&] {
            for (int j = 0; j < 5000; j++) {
                rw.RLock();
                if (shared % 2 != 0) {
                    bad.store(1);
                }
                rw.RUnlock();
            }
        }));
    }

    auto done = finished([&workers] { waitAll(workers); });
    if (!ready(done, 30s)) {
        t.Fatal("нагрузочный тест завис");
    }

    if (bad.load() != 0) {
        t.Fatal("читатель увидел состояние в середине записи");
    }
    if (shared != 4 * 5000 * 2) {
        t.Fatalf("записи потерялись: %d", shared);
    }
}

TEST(TestUnlockWithoutLockThrows) {
    t.Run("Unlock", [](testing::T& t) {
        RWMutex rw;
        try {
            rw.Unlock();
        } catch (...) {
            return;
        }
        t.Fatal("Unlock без Lock должен бросать исключение");
    });

    t.Run("RUnlock", [](testing::T& t) {
        RWMutex rw;
        try {
            rw.RUnlock();
        } catch (...) {
            return;
        }
        t.Fatal("RUnlock без RLock должен бросать исключение");
    });
}

BENCHMARK(BenchmarkReadHeavy) {
    RWMutex rw;
    b.RunParallel([&rw](testing::PB& pb) {
        while (pb.Next()) {
            rw.RLock();
            rw.RUnlock();
        }
    });
}

}  // namespace rwmutex
