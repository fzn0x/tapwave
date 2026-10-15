// Events between tasks (ARCHITECTURE.md section 5).
//
// Producers on any task push into an EventQueue. The loop task drains it and hands each event
// to the EventBus, which calls every subscriber in order on that one thread, so subscribers
// never need locks.
#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace tapwave {

template <class Event>
class EventBus {
public:
    using Handler = std::function<void(const Event&)>;

    void subscribe(Handler handler) { handlers_.push_back(std::move(handler)); }

    void publish(const Event& event) const {
        for (const Handler& handler : handlers_) {
            handler(event);
        }
    }

    std::size_t subscribers() const { return handlers_.size(); }

private:
    std::vector<Handler> handlers_;
};

template <class Event>
class EventQueue {
public:
    explicit EventQueue(std::size_t capacity) : capacity_(capacity) {}

    // Safe from any task. Returns false and drops the event when the queue is full.
    bool push(Event event) {
        std::scoped_lock lock(mutex_);
        if (queue_.size() >= capacity_) {
            ++dropped_;
            return false;
        }
        queue_.push_back(std::move(event));
        return true;
    }

    std::optional<Event> pop() {
        std::scoped_lock lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        Event event = std::move(queue_.front());
        queue_.pop_front();
        return event;
    }

    // Hands up to `max` queued events to `f`, without holding the lock while `f` runs.
    template <class F>
    std::size_t drain(F&& f, std::size_t max) {
        std::size_t handled = 0;
        while (handled < max) {
            auto event = pop();
            if (!event) {
                break;
            }
            f(*event);
            ++handled;
        }
        return handled;
    }

    std::size_t dropped() const {
        std::scoped_lock lock(mutex_);
        return dropped_;
    }

private:
    mutable std::mutex mutex_;
    std::deque<Event> queue_;
    std::size_t capacity_;
    std::size_t dropped_ = 0;
};

}  // namespace tapwave
