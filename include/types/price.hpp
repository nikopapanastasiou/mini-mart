#pragma once

#include <cstdint>

namespace mini_mart::types {

// Ultra-fast Price type for USD securities (4 decimal places)
// UNSAFE/FAST: No underflow protection - fail fast for logic errors
class Price {
    uint64_t value_;
    
public:
    // Constructors
    constexpr Price() noexcept : value_(0) {}
    constexpr Price(uint64_t raw) noexcept : value_(raw) {}
    constexpr Price(double dollars) noexcept : value_(static_cast<uint64_t>(dollars * 10000.0)) {}
    
    // ULTRA-FAST arithmetic - no bounds checking, no branches
    constexpr Price operator+(Price rhs) const noexcept { return Price{value_ + rhs.value_}; }
    constexpr Price operator-(Price rhs) const noexcept { return Price{value_ - rhs.value_}; }
    constexpr Price operator+(uint64_t offset) const noexcept { return Price{value_ + offset}; }
    constexpr Price operator-(uint64_t offset) const noexcept { return Price{value_ - offset}; }
    
    // ULTRA-FAST assignment operators
    constexpr Price& operator+=(Price rhs) noexcept { value_ += rhs.value_; return *this; }
    constexpr Price& operator-=(Price rhs) noexcept { value_ -= rhs.value_; return *this; }
    constexpr Price& operator+=(uint64_t offset) noexcept { value_ += offset; return *this; }
    constexpr Price& operator-=(uint64_t offset) noexcept { value_ -= offset; return *this; }
    
    // Multiplication and division for calculations
    constexpr Price operator*(uint64_t multiplier) const noexcept { return Price{value_ * multiplier}; }
    constexpr Price operator/(uint64_t divisor) const noexcept { return Price{value_ / divisor}; }
    constexpr Price& operator*=(uint64_t multiplier) noexcept { value_ *= multiplier; return *this; }
    constexpr Price& operator/=(uint64_t divisor) noexcept { value_ /= divisor; return *this; }
    
    // Comparison operators (zero-cost)
    constexpr bool operator==(Price rhs) const noexcept { return value_ == rhs.value_; }
    constexpr bool operator!=(Price rhs) const noexcept { return value_ != rhs.value_; }
    constexpr bool operator<(Price rhs) const noexcept { return value_ < rhs.value_; }
    constexpr bool operator<=(Price rhs) const noexcept { return value_ <= rhs.value_; }
    constexpr bool operator>(Price rhs) const noexcept { return value_ > rhs.value_; }
    constexpr bool operator>=(Price rhs) const noexcept { return value_ >= rhs.value_; }
    
    // Comparison with raw values (for literals)
    constexpr bool operator==(uint64_t rhs) const noexcept { return value_ == rhs; }
    constexpr bool operator!=(uint64_t rhs) const noexcept { return value_ != rhs; }
    constexpr bool operator<(uint64_t rhs) const noexcept { return value_ < rhs; }
    constexpr bool operator<=(uint64_t rhs) const noexcept { return value_ <= rhs; }
    constexpr bool operator>(uint64_t rhs) const noexcept { return value_ > rhs; }
    constexpr bool operator>=(uint64_t rhs) const noexcept { return value_ >= rhs; }
    
    // Conversions (inlined)
    constexpr uint64_t raw() const noexcept { return value_; }
    constexpr double dollars() const noexcept { return static_cast<double>(value_) / 10000.0; }
    
    // Explicit conversion to uint64_t for backward compatibility
    constexpr explicit operator uint64_t() const noexcept { return value_; }
    
    // Utility methods
    constexpr bool is_zero() const noexcept { return value_ == 0; }
    constexpr Price abs_diff(Price other) const noexcept { 
        return value_ >= other.value_ ? Price{value_ - other.value_} : Price{other.value_ - value_}; 
    }
};

// Free functions for reverse operations (uint64_t op Price)
constexpr Price operator+(uint64_t lhs, Price rhs) noexcept { return Price{lhs} + rhs; }
constexpr Price operator-(uint64_t lhs, Price rhs) noexcept { return Price{lhs} - rhs; }
constexpr Price operator*(uint64_t lhs, Price rhs) noexcept { return rhs * lhs; }

// Factory functions to avoid constructor ambiguity
constexpr Price price_from_raw(uint64_t raw) noexcept { return Price{raw}; }
constexpr Price price_from_dollars(double dollars) noexcept { return Price{dollars}; }
constexpr Price price_from_cents(uint64_t cents) noexcept { return Price{cents}; }

// Literals for clean syntax
constexpr Price operator""_cents(unsigned long long cents) { return Price{static_cast<uint64_t>(cents)}; }
constexpr Price operator""_dollars(long double dollars) { return Price{static_cast<double>(dollars)}; }

// Common price constants
namespace price_constants {
    constexpr Price ZERO = price_from_raw(0u);
    constexpr Price ONE_CENT = price_from_raw(1u);
    constexpr Price ONE_DOLLAR = price_from_raw(10000u);
    constexpr Price MAX_PRICE = price_from_raw(UINT64_MAX);
}

} // namespace mini_mart::types
