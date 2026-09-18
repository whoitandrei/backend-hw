#include "waitgroup.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <thread>
#include <utility>
#include <vector>

#include "internal/testing/testing.hpp"

namespace waitgroup {
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

TEST(TestWaitOnZeroReturnsAtOnce) {
    WaitGroup wg;

    auto waiting = finished([&wg] { wg.Wait(); });
    if (!ready(waiting, 1s)) {
        t.Fatal("Wait на нулевом счётчике не должен блокировать");
    }
}

TEST(TestWaitsForEveryone) {
    WaitGroup wg;
    std::atomic<int> done{0};

    const int workers = 20;
    wg.Add(workers);

    std::vector<std::future<void>> threads;
    for (int i = 0; i < workers; i++) {
        threads.push_back(finished([&] {
            std::this_thread::sleep_for(20ms);
            done.fetch_add(1);
            wg.Done();
        }));
    }

    auto waiting = finished([&wg] { wg.Wait(); });
    if (!ready(waiting, 5s)) {
        t.Fatal("Wait не дождался");
    }

    if (const int got = done.load(); got != workers) {
        t.Fatalf("Wait вернулся, когда закончили только %d из %d", got, workers);
    }

    waitAll(threads);
}

TEST(TestManyWaiters) {
    WaitGroup wg;
    wg.Add(1);

    std::vector<std::future<void>> waiters;
    for (int i = 0; i < 30; i++) {
        waiters.push_back(finished([&wg] { wg.Wait(); }));
    }

    std::this_thread::sleep_for(50ms);
    wg.Done();

    auto done = finished([&waiters] { waitAll(waiters); });
    if (!ready(done, 5s)) {
        t.Fatal("не все ожидающие проснулись");
    }
}

TEST(TestWaitBlocksUntilDone) {
    WaitGroup wg;
    wg.Add(1);

    auto waiting = finished([&wg] { wg.Wait(); });
    if (ready(waiting, 50ms)) {
        t.Fatal("Wait вернулся раньше времени");
    }

    wg.Done();

    if (!ready(waiting, 2s)) {
        t.Fatal("Wait не проснулся после Done");
    }
}

TEST(TestReuse) {
    WaitGroup wg;

    for (int round = 0; round < 5; round++) {
        wg.Add(4);

        std::vector<std::future<void>> workers;
        for (int i = 0; i < 4; i++) {
            workers.push_back(finished([&wg] { wg.Done(); }));
        }

        auto waiting = finished([&wg] { wg.Wait(); });
        if (!ready(waiting, 2s)) {
            t.Fatalf("раунд %d: Wait завис", round);
        }

        waitAll(workers);
    }
}

TEST(TestNegativeCounterThrows) {
    WaitGroup wg;
    try {
        wg.Done();
    } catch (...) {
        return;
    }
    t.Fatal("уход счётчика в минус должен бросать исключение");
}

TEST(TestStress) {
    for (int round = 0; round < 200; round++) {
        WaitGroup wg;
        std::atomic<int> counter{0};

        wg.Add(8);

        std::vector<std::future<void>> workers;
        for (int i = 0; i < 8; i++) {
            workers.push_back(finished([&] {
                counter.fetch_add(1);
                wg.Done();
            }));
        }

        auto waiting = finished([&wg] { wg.Wait(); });
        if (!ready(waiting, 5s)) {
            t.Fatalf("раунд %d завис", round);
        }

        if (counter.load() != 8) {
            t.Fatalf("раунд %d: счётчик %d", round, counter.load());
        }

        waitAll(workers);
    }
}

}  // namespace waitgroup
