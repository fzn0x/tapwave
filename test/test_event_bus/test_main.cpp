#include "doctest_main.hpp"

#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "core/event_bus.hpp"

using namespace tapwave;

namespace {

struct Scanned {
    std::string uid;
};
struct Removed {
    std::string uid;
};
using Event = std::variant<Scanned, Removed>;

}  // namespace

TEST_CASE("the bus calls every subscriber in order") {
    EventBus<Event> bus;
    std::vector<std::string> log;
    bus.subscribe([&](const Event& e) {
        if (const auto* s = std::get_if<Scanned>(&e)) {
            log.push_back("first:" + s->uid);
        }
    });
    bus.subscribe([&](const Event& e) {
        if (const auto* r = std::get_if<Removed>(&e)) {
            log.push_back("second:" + r->uid);
        } else {
            log.push_back("second:scan");
        }
    });
    CHECK(bus.subscribers() == 2);
    bus.publish(Scanned{"04-A2"});
    bus.publish(Removed{"04-A2"});
    CHECK(log == std::vector<std::string>{"first:04-A2", "second:scan", "second:04-A2"});
}

TEST_CASE("the queue keeps order and drops when full") {
    EventQueue<int> queue(3);
    CHECK(queue.push(1));
    CHECK(queue.push(2));
    CHECK(queue.push(3));
    CHECK_FALSE(queue.push(4));
    CHECK(queue.dropped() == 1);
    CHECK(queue.pop() == 1);
    CHECK(queue.push(5));
    std::vector<int> seen;
    CHECK(queue.drain([&](int v) { seen.push_back(v); }, 10) == 3);
    CHECK(seen == std::vector<int>{2, 3, 5});
    CHECK_FALSE(queue.pop());
}

TEST_CASE("drain stops at the limit") {
    EventQueue<int> queue(10);
    for (int i = 0; i < 5; ++i) {
        queue.push(i);
    }
    int count = 0;
    CHECK(queue.drain([&](int) { ++count; }, 2) == 2);
    CHECK(count == 2);
    CHECK(queue.pop() == 2);
}

TEST_CASE("producers on other threads") {
    EventQueue<int> queue(10000);
    std::vector<std::thread> producers;
    for (int t = 0; t < 4; ++t) {
        producers.emplace_back([&queue, t] {
            for (int i = 0; i < 1000; ++i) {
                queue.push(t * 1000 + i);
            }
        });
    }
    for (auto& p : producers) {
        p.join();
    }
    long sum = 0;
    const std::size_t handled = queue.drain([&](int v) { sum += v; }, 100000);
    CHECK(handled == 4000);
    CHECK(sum == (3999L * 4000L) / 2);
    CHECK(queue.dropped() == 0);
}
