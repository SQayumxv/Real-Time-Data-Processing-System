#pragma once
#include <array>
#include <algorithm>
#include <atomic>
#include <cstddef>

template<class T, std::size_t Capacity> class SpscBuffer {
public:
    bool push(const T& value) {
        const auto write = write_.load(std::memory_order_relaxed);
        if (write - read_.load(std::memory_order_acquire) == Capacity) return false;
        values_[write % Capacity] = value;
        write_.store(write + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& value) {
        const auto read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) return false;
        value = values_[read % Capacity];
        read_.store(read + 1, std::memory_order_release);
        return true;
    }
    std::size_t size() const {
        const auto read = read_.load(std::memory_order_acquire);
        const auto write = write_.load(std::memory_order_acquire);
        return std::min(Capacity, write - read);
    }
private:
    std::array<T, Capacity> values_{};
    std::atomic<std::size_t> write_{0};
    std::atomic<std::size_t> read_{0};
};
