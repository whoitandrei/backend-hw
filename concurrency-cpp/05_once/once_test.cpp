#include "once.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "internal/testing/testing.hpp"

namespace once {
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

TEST(TestRunsOnlyOnce) {
    Once o;
    int calls = 0;

    for (int i = 0; i < 10; i++) {
        o.Do([&calls] { calls++; });
    }

    if (calls != 1) {
        t.Fatalf("функция вызвана %d раз", calls);
    }
}

TEST(TestDone) {
    Once o;

    if (o.Done()) {
        t.Fatal("до первого Do работа не сделана");
    }

    o.Do([] {});

    if (!o.Done()) {
        t.Fatal("после Do работа сделана");
    }
}

TEST(TestConcurrentCallsRunItOnce) {
    Once o;
    std::atomic<int> calls{0};

    std::vector<std::future<void>> workers;
    for (int i = 0; i < 100; i++) {
        workers.push_back(finished([&] {
            o.Do([&calls] {
                calls.fetch_add(1);
                std::this_thread::sleep_for(10ms);
            });
        }));
    }

    auto done = finished([&workers] { waitAll(workers); });
    if (!ready(done, 10s)) {
        t.Fatal("часть потоков зависла в Do");
    }

    if (calls.load() != 1) {
        t.Fatalf("функция вызвана %d раз", calls.load());
    }
}

TEST(TestDoWaitsForTheWinner) {
    Once o;
    std::atomic<int> value{0};

    auto first = finished([&o, &value] {
        o.Do([&value] {
            std::this_thread::sleep_for(100ms);
            value.store(1);
        });
    });

    std::this_thread::sleep_for(10ms);

    auto second = finished([&o, &value, &t] {
        o.Do([] {});
        if (value.load() != 1) {
            t.Error("Do вернулся до того, как работа была закончена");
        }
    });

    if (!ready(first, 5s)) {
        t.Fatal("первый Do завис");
    }
    if (!ready(second, 5s)) {
        t.Fatal("второй Do завис");
    }
}

TEST(TestExceptionCountsAsDone) {
    Once o;
    int calls = 0;

    try {
        o.Do([&calls] {
            calls++;
            throw std::runtime_error("упало");
        });
    } catch (...) {
    }

    o.Do([&calls] { calls++; });

    if (calls != 1) {
        t.Fatalf("после исключения функция вызвана ещё раз: всего %d", calls);
    }
}

TEST(TestStress) {
    for (int round = 0; round < 300; round++) {
        Once o;
        std::atomic<int> calls{0};

        std::vector<std::future<void>> workers;
        for (int i = 0; i < 16; i++) {
            workers.push_back(finished([&] { o.Do([&calls] { calls.fetch_add(1); }); }));
        }
        waitAll(workers);

        if (calls.load() != 1) {
            t.Fatalf("раунд %d: вызовов %d", round, calls.load());
        }
    }
}

}  // namespace once
