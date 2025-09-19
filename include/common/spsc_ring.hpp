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
  static constexpr size_t cachelineSize =
      std::hardware_destructive_interference_size;
#else
  static constexpr size_t cachelineSize = 64;
#endif

  static constexpr size_t storageAlign = std::max(alignof(T), cachelineSize);
  static constexpr size_t capacity = N;
  static constexpr size_t mask = N - 1;
  static constexpr size_t stride = sizeof(T);

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
    const size_t head = this->head.load(std::memory_order_relaxed);
    const size_t tail = this->tail.load(std::memory_order_relaxed);

    return tail - head;
  }

  bool empty() const {
    const size_t head = this->head.load(std::memory_order_relaxed);
    const size_t tail = this->tail.load(std::memory_order_relaxed);

    return head == tail;
  }

  bool full() const {
    const size_t head = this->head.load(std::memory_order_relaxed);
    const size_t tail = this->tail.load(std::memory_order_relaxed);

    return (tail - head) == capacity;
  }

  static inline constexpr size_t get_capacity() { return capacity; }

  template <typename... Args> bool try_emplace(Args &&...args) {
    return this->emplace_impl(std::forward<Args>(args)...);
  }

  bool try_push(const T &value) { return this->try_emplace(value); }

  bool try_push(T &&value) { return this->try_emplace(std::move(value)); }

  bool try_pop(T &out) {
    const size_t head = this->head.load(std::memory_order_relaxed);
    const size_t tail = this->tail.load(std::memory_order_acquire);
    if (head == tail) {
      return false; // empty
    }

    void *data_at = static_cast<void *>(this->buffer_at(head & mask));

    // Pointer to the live T in the slot
    T *elem = std::launder(reinterpret_cast<T *>(data_at));

    if constexpr (std::is_move_assignable_v<T>) {
      out = std::move(*elem);
    } else {
      static_assert(std::is_move_constructible_v<T>,
                    "T must be move-constructible");
      std::destroy_at(std::addressof(out));
      std::construct_at(std::addressof(out), std::move(*elem));
    }

    // Destroy the object that lived in the slot
    elem->~T();

    // Publish the freed slot so producer can reuse it
    this->head.store(head + 1, std::memory_order_release);
    return true;
  }

private:
  alignas(cachelineSize) std::atomic<size_t> head;
  alignas(cachelineSize) std::atomic<size_t> tail;
  alignas(storageAlign) std::byte buffer[capacity * stride];

  template <typename... Args> bool emplace_impl(Args &&...args) {
    const size_t head = this->head.load(std::memory_order_relaxed);
    const size_t tail = this->tail.load(std::memory_order_relaxed);

    if ((tail - head) == capacity) {
      return false;
    }

    void *data_at = static_cast<void *>(this->buffer_at(tail & mask));
    ::new (data_at) T(std::forward<Args>(args)...);

    this->tail.store(tail + 1, std::memory_order_release);
    return true;
  }

  T *slot_ptr(size_t index) noexcept {
    auto *p = reinterpret_cast<T *>(&buffer[index]);
    return std::launder(p);
  }

  std::byte *buffer_at(size_t index) noexcept {
    return &buffer[index * stride];
  }
};
} // namespace mini_mart::common
