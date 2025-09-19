#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>

namespace mini_mart::common {
template <typename T, size_t N> class SpscRing {

  static_assert(N > 0, "N must be greater than 0");
  static_assert((N & (N - 1)) == 0, "N must be a power of 2");
  static_assert(std::is_nothrow_constructible_v<T>,
                "T must be nothrow constructible");

#if defined(__cpp_lib_hardware_interference_size)
  static constexpr size_t CACHELINE_SIZE =
      std::hardware_destructive_interference_size;
#else
  static constexpr size_t CACHELINE_SIZE = 64;
#endif

  static constexpr size_t STORAGE_ALIGN = std::max(alignof(T), CACHELINE_SIZE);
  static constexpr size_t CAPACITY = N;
  static constexpr size_t MASK = N - 1;
  static constexpr size_t STRIDE = sizeof(T);

public:
  explicit SpscRing() : head(0), tail(0) {}

  SpscRing(const SpscRing &other) = delete;
  SpscRing &operator=(const SpscRing &other) = delete;

  ~SpscRing() {
    T tmp;
    while (this->try_pop(tmp)) {
    }
  }

  size_t size() const {
    const size_t head_val = head.load(std::memory_order_relaxed);
    const size_t tail_val = tail.load(std::memory_order_relaxed);

    return tail_val - head_val;
  }

  bool empty() const {
    const size_t head_val = head.load(std::memory_order_relaxed);
    const size_t tail_val = tail.load(std::memory_order_relaxed);

    return head_val == tail_val;
  }

  bool full() const {
    const size_t head_val = head.load(std::memory_order_relaxed);
    const size_t tail_val = tail.load(std::memory_order_relaxed);

    return (tail_val - head_val) == CAPACITY;
  }

  static inline constexpr size_t get_capacity() { return CAPACITY; }

  template <typename... Args> bool try_emplace(Args &&...args) {
    return this->emplace_impl(std::forward<Args>(args)...);
  }

  bool try_push(const T &value) { return this->try_emplace(value); }

  bool try_push(T &&value) { return this->try_emplace(std::move(value)); }

  bool try_pop(T &out) {
    const size_t head_val = head.load(std::memory_order_relaxed);
    const size_t tail_val = tail.load(std::memory_order_acquire);
    if (head_val == tail_val) {
      return false;
    }

    void *data_at = static_cast<void *>(buffer_at(head_val & MASK));
    T *elem = std::launder(reinterpret_cast<T *>(data_at));

    if constexpr (std::is_move_assignable_v<T>) {
      out = std::move(*elem);
    } else {
      static_assert(std::is_move_constructible_v<T>,
                    "T must be move-constructible");
      std::destroy_at(std::addressof(out));
      std::construct_at(std::addressof(out), std::move(*elem));
    }

    elem->~T();
    head.store(head_val + 1, std::memory_order_release);
    return true;
  }

private:
  alignas(CACHELINE_SIZE) std::atomic<size_t> head;
  alignas(CACHELINE_SIZE) std::atomic<size_t> tail;
  alignas(STORAGE_ALIGN) std::byte buffer[CAPACITY * STRIDE];

  template <typename... Args> bool emplace_impl(Args &&...args) {
    const size_t head_val = head.load(std::memory_order_relaxed);
    const size_t tail_val = tail.load(std::memory_order_relaxed);

    if ((tail_val - head_val) == CAPACITY) {
      return false;
    }

    void *data_at = static_cast<void *>(buffer_at(tail_val & MASK));
    ::new (data_at) T(std::forward<Args>(args)...);

    tail.store(tail_val + 1, std::memory_order_release);
    return true;
  }

  T *slot_ptr(size_t index) noexcept {
    auto *p = reinterpret_cast<T *>(&buffer[index]);
    return std::launder(p);
  }

  std::byte *buffer_at(size_t index) noexcept {
    return &buffer[index * STRIDE];
  }
};
} // namespace mini_mart::common
