#ifndef SPSC_QUEUE_H
#define SPSC_QUEUE_H

#include <array>
#include <atomic>
#include <cstddef>

template <typename T, size_t Capacity> class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

  public:
    SpscQueue() : head(0), tail(0) {}

    bool push(const T& item) {
        size_t currentTail = tail.load(std::memory_order_relaxed);
        size_t nextTail = (currentTail + 1) & (Capacity - 1);
        if (nextTail != head.load(std::memory_order_acquire)) {
            buffer[currentTail] = item;
            tail.store(nextTail, std::memory_order_release);
            return true;
        }
        return false;
    }

    bool push(T&& item) {
        size_t currentTail = tail.load(std::memory_order_relaxed);
        size_t nextTail = (currentTail + 1) & (Capacity - 1);
        if (nextTail != head.load(std::memory_order_acquire)) {
            buffer[currentTail] = std::move(item);
            tail.store(nextTail, std::memory_order_release);
            return true;
        }
        return false;
    }

    bool pop(T& item) {
        size_t currentHead = head.load(std::memory_order_relaxed);
        if (currentHead != tail.load(std::memory_order_acquire)) {
            item = std::move(buffer[currentHead]);
            head.store((currentHead + 1) & (Capacity - 1), std::memory_order_release);
            return true;
        }
        return false;
    }

    bool empty() const {
        return head.load(std::memory_order_acquire) == tail.load(std::memory_order_acquire);
    }

  private:
    std::array<T, Capacity> buffer;
    alignas(128) std::atomic<size_t> head;
    alignas(128) std::atomic<size_t> tail;
};

#endif // SPSC_QUEUE_H
