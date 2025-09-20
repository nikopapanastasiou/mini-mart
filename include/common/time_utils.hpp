#pragma once

#include <chrono>
#include <cstdint>

namespace mini_mart::common {

// High-performance time utilities for HFT systems
namespace time_utils {

    // Get current time in nanoseconds (hot path optimized)
    inline uint64_t now_ns() noexcept {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()
            ).count()
        );
    }
    
    // Get current time in microseconds
    inline uint64_t now_us() noexcept {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()
            ).count()
        );
    }
    
    // Convert nanoseconds to microseconds
    constexpr uint64_t ns_to_us(uint64_t ns) noexcept {
        return ns / 1000;
    }
    
    // Convert microseconds to nanoseconds
    constexpr uint64_t us_to_ns(uint64_t us) noexcept {
        return us * 1000;
    }

} // namespace time_utils

} // namespace mini_mart::common
