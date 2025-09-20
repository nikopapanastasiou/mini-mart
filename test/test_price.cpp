#include "types/price.hpp"
#include <gtest/gtest.h>
#include <climits>

using namespace mini_mart::types;
using namespace mini_mart::types::price_constants;

class PriceTest : public ::testing::Test {
protected:
    // Test constants
    static constexpr uint64_t RAW_100_DOLLARS = 1000000u;  // $100.00
    static constexpr uint64_t RAW_50_CENTS = 50u;          // $0.0050
    static constexpr double DOUBLE_175_50 = 175.50;        // $175.50
};

// ============================================================================
// CONSTRUCTOR TESTS
// ============================================================================

TEST_F(PriceTest, DefaultConstructor) {
    Price p;
    EXPECT_EQ(p.raw(), 0u);
    EXPECT_EQ(p.dollars(), 0.0);
    EXPECT_TRUE(p.is_zero());
}

TEST_F(PriceTest, RawConstructor) {
    Price p = price_from_raw(RAW_100_DOLLARS);
    EXPECT_EQ(p.raw(), RAW_100_DOLLARS);
    EXPECT_DOUBLE_EQ(p.dollars(), 100.0);
    EXPECT_FALSE(p.is_zero());
}

TEST_F(PriceTest, DoubleConstructor) {
    Price p = price_from_dollars(DOUBLE_175_50);
    EXPECT_EQ(p.raw(), 1755000u);
    EXPECT_DOUBLE_EQ(p.dollars(), 175.50);
}

TEST_F(PriceTest, FactoryFunctions) {
    auto p1 = price_from_raw(RAW_100_DOLLARS);
    auto p2 = price_from_dollars(100.0);
    auto p3 = price_from_cents(RAW_100_DOLLARS);
    
    EXPECT_EQ(p1.raw(), RAW_100_DOLLARS);
    EXPECT_EQ(p2.raw(), RAW_100_DOLLARS);
    EXPECT_EQ(p3.raw(), RAW_100_DOLLARS);
}

TEST_F(PriceTest, Literals) {
    auto p1 = 1000000_cents;
    auto p2 = 100.0_dollars;
    
    EXPECT_EQ(p1.raw(), RAW_100_DOLLARS);
    EXPECT_EQ(p2.raw(), RAW_100_DOLLARS);
}

// ============================================================================
// ARITHMETIC TESTS - ULTRA FAST (NO BOUNDS CHECKING)
// ============================================================================

TEST_F(PriceTest, Addition) {
    Price p1 = price_from_raw(1000000u); // $100.00
    Price p2 = price_from_raw(500000u);  // $50.00
    
    Price result = p1 + p2;
    EXPECT_EQ(result.raw(), 1500000u); // $150.00
    
    // Test with offset
    Price result2 = p1 + 250000u;
    EXPECT_EQ(result2.raw(), 1250000u); // $125.00
}

TEST_F(PriceTest, Subtraction) {
    Price p1 = price_from_raw(1000000u); // $100.00
    Price p2 = price_from_raw(300000u);  // $30.00
    
    Price result = p1 - p2;
    EXPECT_EQ(result.raw(), 700000u); // $70.00
    
    // Test with offset
    Price result2 = p1 - 250000u;
    EXPECT_EQ(result2.raw(), 750000u); // $75.00
}

TEST_F(PriceTest, SubtractionUnderflow) {
    // UNSAFE/FAST: This should underflow to very large number
    Price p1 = price_from_raw(300000u); // $30.00
    Price p2 = price_from_raw(1000000u); // $100.00
    
    Price result = p1 - p2;
    // Should underflow to very large number
    EXPECT_GT(result.raw(), 1000000000000000000ULL);
    
    // This is expected behavior - fail fast!
    // In production, this indicates a logic error
}

TEST_F(PriceTest, Multiplication) {
    Price p = price_from_raw(500000u); // $50.00
    
    Price result = p * 3u;
    EXPECT_EQ(result.raw(), 1500000u); // $150.00
    
    // Test reverse operation
    Price result2 = 2u * p;
    EXPECT_EQ(result2.raw(), 1000000u); // $100.00
}

TEST_F(PriceTest, Division) {
    Price p = price_from_raw(1500000u); // $150.00
    
    Price result = p / 3u;
    EXPECT_EQ(result.raw(), 500000u); // $50.00
    
    // Test integer division truncation
    Price p2 = price_from_raw(1500001u); // $150.0001
    Price result2 = p2 / 3u;
    EXPECT_EQ(result2.raw(), 500000u); // Truncates to $50.00
}

// ============================================================================
// ASSIGNMENT OPERATOR TESTS
// ============================================================================

TEST_F(PriceTest, AssignmentOperators) {
    Price p = price_from_raw(1000000u); // $100.00
    
    p += price_from_raw(500000u); // +$50.00
    EXPECT_EQ(p.raw(), 1500000u);
    
    p -= price_from_raw(300000u); // -$30.00
    EXPECT_EQ(p.raw(), 1200000u);
    
    p *= 2u;
    EXPECT_EQ(p.raw(), 2400000u);
    
    p /= 3u;
    EXPECT_EQ(p.raw(), 800000u);
    
    // Test with raw offsets
    p += 200000u;
    EXPECT_EQ(p.raw(), 1000000u);
    
    p -= 250000u;
    EXPECT_EQ(p.raw(), 750000u);
}

// ============================================================================
// COMPARISON TESTS
// ============================================================================

TEST_F(PriceTest, Comparisons) {
    Price p1 = price_from_raw(1000000u); // $100.00
    Price p2 = price_from_raw(1000000u); // $100.00
    Price p3 = price_from_raw(500000u);  // $50.00
    
    // Equality
    EXPECT_TRUE(p1 == p2);
    EXPECT_FALSE(p1 == p3);
    EXPECT_FALSE(p1 != p2);
    EXPECT_TRUE(p1 != p3);
    
    // Ordering
    EXPECT_TRUE(p1 > p3);
    EXPECT_TRUE(p1 >= p3);
    EXPECT_TRUE(p1 >= p2);
    EXPECT_TRUE(p3 < p1);
    EXPECT_TRUE(p3 <= p1);
    EXPECT_TRUE(p2 <= p1);
}

TEST_F(PriceTest, ComparisonWithRaw) {
    Price p = price_from_raw(1000000u); // $100.00
    
    EXPECT_TRUE(p == 1000000u);
    EXPECT_FALSE(p == 500000u);
    EXPECT_TRUE(p > 500000u);
    EXPECT_TRUE(p >= 1000000u);
    EXPECT_FALSE(p < 500000u);
}

// ============================================================================
// CONVERSION TESTS
// ============================================================================

TEST_F(PriceTest, Conversions) {
    Price p = price_from_raw(1755000u); // $175.50
    
    EXPECT_EQ(p.raw(), 1755000u);
    EXPECT_DOUBLE_EQ(p.dollars(), 175.50);
    EXPECT_EQ(static_cast<uint64_t>(p), 1755000u);
}

TEST_F(PriceTest, PrecisionTest) {
    // Test 4 decimal place precision
    Price p = price_from_raw(12345u); // $1.2345
    EXPECT_DOUBLE_EQ(p.dollars(), 1.2345);
    
    Price p2 = price_from_raw(1u); // $0.0001
    EXPECT_DOUBLE_EQ(p2.dollars(), 0.0001);
}

// ============================================================================
// UTILITY METHOD TESTS
// ============================================================================

TEST_F(PriceTest, IsZero) {
    EXPECT_TRUE(price_from_raw(0u).is_zero());
    EXPECT_TRUE(ZERO.is_zero());
    EXPECT_FALSE(price_from_raw(1u).is_zero());
    EXPECT_FALSE(ONE_CENT.is_zero());
}

TEST_F(PriceTest, AbsDiff) {
    Price p1 = price_from_raw(1000000u); // $100.00
    Price p2 = price_from_raw(750000u);  // $75.00
    
    EXPECT_EQ(p1.abs_diff(p2).raw(), 250000u); // $25.00
    EXPECT_EQ(p2.abs_diff(p1).raw(), 250000u); // $25.00
    
    EXPECT_EQ(p1.abs_diff(p1).raw(), 0u); // $0.00
}

// ============================================================================
// CONSTANT TESTS
// ============================================================================

TEST_F(PriceTest, Constants) {
    EXPECT_EQ(ZERO.raw(), 0u);
    EXPECT_EQ(ONE_CENT.raw(), 1u);
    EXPECT_EQ(ONE_DOLLAR.raw(), 10000u);
    EXPECT_EQ(MAX_PRICE.raw(), UINT64_MAX);
    
    EXPECT_DOUBLE_EQ(ONE_DOLLAR.dollars(), 1.0);
    EXPECT_DOUBLE_EQ(ONE_CENT.dollars(), 0.0001);
}

// ============================================================================
// PERFORMANCE/EDGE CASE TESTS
// ============================================================================

TEST_F(PriceTest, MaxValueOperations) {
    Price max_price = price_from_raw(UINT64_MAX);
    
    // These should not crash, but may overflow (expected in unsafe mode)
    Price result1 = max_price + price_from_raw(1u); // Overflow expected
    EXPECT_EQ(result1.raw(), 0u); // Wraps around
    
    Price result2 = ZERO - price_from_raw(1u); // Underflow expected  
    EXPECT_EQ(result2.raw(), UINT64_MAX); // Wraps around
}

TEST_F(PriceTest, HighFrequencyOperations) {
    // Simulate rapid price updates in HFT scenario
    Price base_price = price_from_raw(1750000u); // $175.00
    
    for (int i = 0; i < 10000; ++i) {
        Price offset = price_from_raw(static_cast<uint64_t>(i % 100));
        Price bid = base_price - offset;
        Price ask = base_price + offset;
        
        EXPECT_LE(bid.raw(), base_price.raw());
        EXPECT_GE(ask.raw(), base_price.raw());
        
        // Test mid-price calculation
        Price mid = (bid + ask) / 2u;
        EXPECT_EQ(mid.raw(), base_price.raw());
    }
}

TEST_F(PriceTest, SpreadCalculations) {
    Price bid = price_from_raw(1750000u);  // $175.00
    Price ask = price_from_raw(1750500u);  // $175.05
    
    Price spread = ask - bid;
    EXPECT_EQ(spread.raw(), 500u); // 5 cents
    
    Price mid = (bid + ask) / 2u;
    EXPECT_EQ(mid.raw(), 1750250u); // $175.025
    
    // Spread in basis points: (spread / mid) * 10000
    double spread_bps = (spread.dollars() / mid.dollars()) * 10000.0;
    EXPECT_NEAR(spread_bps, 2.857, 0.001); // ~2.86 bps
}

// ============================================================================
// CONSTEXPR TESTS (COMPILE-TIME EVALUATION)
// ============================================================================

TEST_F(PriceTest, ConstexprOperations) {
    // These should all be evaluated at compile time
    constexpr Price p1 = price_from_raw(1000000u);
    constexpr Price p2 = price_from_raw(500000u);
    constexpr Price sum = p1 + p2;
    constexpr Price diff = p1 - p2;
    constexpr Price product = p1 * 2u;
    constexpr Price quotient = p1 / 2u;
    
    static_assert(sum.raw() == 1500000u);
    static_assert(diff.raw() == 500000u);
    static_assert(product.raw() == 2000000u);
    static_assert(quotient.raw() == 500000u);
    
    // Verify they work at runtime too
    EXPECT_EQ(sum.raw(), 1500000u);
    EXPECT_EQ(diff.raw(), 500000u);
    EXPECT_EQ(product.raw(), 2000000u);
    EXPECT_EQ(quotient.raw(), 500000u);
}
