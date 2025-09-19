#include <gtest/gtest.h>
#include "market_data/security_store.hpp"
#include "market_data/security_seeder.hpp"
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <random>

using namespace mini_mart::market_data;
using namespace mini_mart::types;

class SecurityStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        store_ = std::make_unique<SecurityStore>();
        
        // Create test securities
        aapl_id_ = SecuritySeeder::create_security_id("AAPL");
        msft_id_ = SecuritySeeder::create_security_id("MSFT");
        googl_id_ = SecuritySeeder::create_security_id("GOOGL");
    }
    
    void TearDown() override {
        store_.reset();
    }
    
    MarketDataL2Message create_test_message(const SecurityId& security_id, 
                                           Price best_bid = 1000000, // $100.00
                                           Price best_ask = 1000500) { // $100.05
        MarketDataL2Message message{};
        
        message.header.type = static_cast<uint16_t>(MessageType::MARKET_DATA_L2);
        message.header.length = sizeof(MarketDataL2Message);
        message.header.seq_no = 1;
        
        message.security_id = security_id;
        message.timestamp_ns = get_current_time_ns();
        
        // Set up bid levels (descending prices)
        message.num_bid_levels = 3;
        message.bids[0] = {best_bid, 1000};      // $100.00, qty 1000
        message.bids[1] = {best_bid - 50, 500};  // $99.995, qty 500
        message.bids[2] = {best_bid - 100, 250}; // $99.99, qty 250
        
        // Set up ask levels (ascending prices)
        message.num_ask_levels = 3;
        message.asks[0] = {best_ask, 800};       // $100.05, qty 800
        message.asks[1] = {best_ask + 50, 400};  // $100.055, qty 400
        message.asks[2] = {best_ask + 100, 200}; // $100.06, qty 200
        
        return message;
    }
    
    uint64_t get_current_time_ns() const {
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    }
    
    std::unique_ptr<SecurityStore> store_;
    SecurityId aapl_id_;
    SecurityId msft_id_;
    SecurityId googl_id_;
};

TEST_F(SecurityStoreTest, InitialState) {
    EXPECT_EQ(store_->size(), 0);
    EXPECT_FALSE(store_->contains(aapl_id_));
    
    SecurityStore::SecuritySnapshot snapshot;
    EXPECT_FALSE(store_->get_security_snapshot(aapl_id_, snapshot));
}

TEST_F(SecurityStoreTest, AddRemoveSecurity) {
    // Add security
    EXPECT_TRUE(store_->add_security(aapl_id_));
    EXPECT_EQ(store_->size(), 1);
    EXPECT_TRUE(store_->contains(aapl_id_));
    
    // Cannot add duplicate
    EXPECT_FALSE(store_->add_security(aapl_id_));
    EXPECT_EQ(store_->size(), 1);
    
    // Remove security
    EXPECT_TRUE(store_->remove_security(aapl_id_));
    EXPECT_EQ(store_->size(), 0);
    EXPECT_FALSE(store_->contains(aapl_id_));
    
    // Cannot remove non-existent
    EXPECT_FALSE(store_->remove_security(aapl_id_));
}

TEST_F(SecurityStoreTest, UpdateFromL2Message) {
    // Add security first
    EXPECT_TRUE(store_->add_security(aapl_id_));
    
    // Create and apply L2 update
    auto message = create_test_message(aapl_id_, 1750000, 1750500); // $175.00/$175.05
    EXPECT_TRUE(store_->update_from_l2(message));
    
    // Verify update
    SecurityStore::SecuritySnapshot snapshot;
    EXPECT_TRUE(store_->get_security_snapshot(aapl_id_, snapshot));
    
    EXPECT_EQ(snapshot.security_id, aapl_id_);
    EXPECT_EQ(snapshot.best_bid, 1750000);
    EXPECT_EQ(snapshot.best_ask, 1750500);
    EXPECT_EQ(snapshot.num_bid_levels, 3);
    EXPECT_EQ(snapshot.num_ask_levels, 3);
    EXPECT_EQ(snapshot.update_count, 1);
    
    // Verify order book levels
    EXPECT_EQ(snapshot.bids[0].price, 1750000);
    EXPECT_EQ(snapshot.bids[0].quantity, 1000);
    EXPECT_EQ(snapshot.asks[0].price, 1750500);
    EXPECT_EQ(snapshot.asks[0].quantity, 800);
}

TEST_F(SecurityStoreTest, UpdateNonExistentSecurity) {
    auto message = create_test_message(aapl_id_);
    EXPECT_FALSE(store_->update_from_l2(message));
}

TEST_F(SecurityStoreTest, SnapshotCalculations) {
    store_->add_security(aapl_id_);
    
    auto message = create_test_message(aapl_id_, 1000000, 1001000); // $100.00/$100.10
    store_->update_from_l2(message);
    
    SecurityStore::SecuritySnapshot snapshot;
    EXPECT_TRUE(store_->get_security_snapshot(aapl_id_, snapshot));
    
    // Test mid price calculation
    Price expected_mid = (1000000 + 1001000) / 2; // $100.05
    EXPECT_EQ(snapshot.get_mid_price(), expected_mid);
    
    // Test spread calculation (should be ~10 basis points)
    double spread_bps = snapshot.get_spread_bps();
    EXPECT_NEAR(spread_bps, 10.0, 0.1); // 10 bps with small tolerance
    
    // Test price conversion
    double bid_price = SecurityStore::SecuritySnapshot::price_to_double(snapshot.best_bid);
    EXPECT_NEAR(bid_price, 100.0, 0.0001);
}

TEST_F(SecurityStoreTest, MultipleSecurities) {
    // Add multiple securities
    EXPECT_TRUE(store_->add_security(aapl_id_));
    EXPECT_TRUE(store_->add_security(msft_id_));
    EXPECT_TRUE(store_->add_security(googl_id_));
    EXPECT_EQ(store_->size(), 3);
    
    // Update each with different prices
    auto aapl_msg = create_test_message(aapl_id_, 1750000, 1750500);
    auto msft_msg = create_test_message(msft_id_, 3500000, 3500500);
    auto googl_msg = create_test_message(googl_id_, 28000000, 28005000);
    
    EXPECT_TRUE(store_->update_from_l2(aapl_msg));
    EXPECT_TRUE(store_->update_from_l2(msft_msg));
    EXPECT_TRUE(store_->update_from_l2(googl_msg));
    
    // Verify each security independently
    SecurityStore::SecuritySnapshot snapshot;
    
    EXPECT_TRUE(store_->get_security_snapshot(aapl_id_, snapshot));
    EXPECT_EQ(snapshot.best_bid, 1750000);
    
    EXPECT_TRUE(store_->get_security_snapshot(msft_id_, snapshot));
    EXPECT_EQ(snapshot.best_bid, 3500000);
    
    EXPECT_TRUE(store_->get_security_snapshot(googl_id_, snapshot));
    EXPECT_EQ(snapshot.best_bid, 28000000);
    
    // Test get_all_securities
    auto all_securities = store_->get_all_securities();
    EXPECT_EQ(all_securities.size(), 3);
}

TEST_F(SecurityStoreTest, ClearStore) {
    store_->add_security(aapl_id_);
    store_->add_security(msft_id_);
    EXPECT_EQ(store_->size(), 2);
    
    store_->clear();
    EXPECT_EQ(store_->size(), 0);
    EXPECT_FALSE(store_->contains(aapl_id_));
    EXPECT_FALSE(store_->contains(msft_id_));
}

TEST_F(SecurityStoreTest, HighFrequencyUpdates) {
    store_->add_security(aapl_id_);
    
    const int num_updates = 10000;
    Price base_price = 1750000; // $175.00
    
    // Apply many rapid updates
    for (int i = 0; i < num_updates; ++i) {
        Price bid = base_price + (i % 100) - 50; // Small price variations
        Price ask = bid + 500; // 5 cent spread
        
        auto message = create_test_message(aapl_id_, bid, ask);
        EXPECT_TRUE(store_->update_from_l2(message));
    }
    
    // Verify final state
    SecurityStore::SecuritySnapshot snapshot;
    EXPECT_TRUE(store_->get_security_snapshot(aapl_id_, snapshot));
    EXPECT_EQ(snapshot.update_count, num_updates);
    
    // Final price should be from last update
    Price expected_final_bid = base_price + ((num_updates - 1) % 100) - 50;
    EXPECT_EQ(snapshot.best_bid, expected_final_bid);
}

TEST_F(SecurityStoreTest, ConcurrentReads) {
    store_->add_security(aapl_id_);
    
    // Start with initial data
    auto message = create_test_message(aapl_id_, 1750000, 1750500);
    store_->update_from_l2(message);
    
    std::atomic<bool> stop_flag{false};
    std::atomic<int> read_count{0};
    std::atomic<int> error_count{0};
    
    // Start multiple reader threads
    std::vector<std::thread> readers;
    const int num_readers = 4;
    
    for (int i = 0; i < num_readers; ++i) {
        readers.emplace_back([&]() {
            SecurityStore::SecuritySnapshot snapshot;
            while (!stop_flag.load()) {
                if (store_->get_security_snapshot(aapl_id_, snapshot)) {
                    // Verify data consistency
                    if (snapshot.best_bid > snapshot.best_ask && snapshot.best_ask > 0) {
                        error_count.fetch_add(1);
                    }
                    read_count.fetch_add(1);
                } else {
                    error_count.fetch_add(1);
                }
                std::this_thread::yield();
            }
        });
    }
    
    // Let readers run for a short time
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Apply updates while readers are running
    for (int i = 0; i < 1000; ++i) {
        Price bid = 1750000 + (i % 50);
        Price ask = bid + 500;
        auto update_msg = create_test_message(aapl_id_, bid, ask);
        store_->update_from_l2(update_msg);
    }
    
    // Stop readers
    stop_flag.store(true);
    for (auto& reader : readers) {
        reader.join();
    }
    
    // Should have many successful reads and no errors
    EXPECT_GT(read_count.load(), 100);
    EXPECT_EQ(error_count.load(), 0);
}

TEST_F(SecurityStoreTest, OrderBookLevels) {
    store_->add_security(aapl_id_);
    
    // Create message with full 5-level order book
    MarketDataL2Message message{};
    message.security_id = aapl_id_;
    message.timestamp_ns = get_current_time_ns();
    
    // 5 bid levels (descending)
    message.num_bid_levels = 5;
    for (int i = 0; i < 5; ++i) {
        message.bids[i].price = 1750000 - (i * 100); // $175.00, $174.99, etc.
        message.bids[i].quantity = 1000 + (i * 100);
    }
    
    // 5 ask levels (ascending)
    message.num_ask_levels = 5;
    for (int i = 0; i < 5; ++i) {
        message.asks[i].price = 1750500 + (i * 100); // $175.05, $175.06, etc.
        message.asks[i].quantity = 800 + (i * 50);
    }
    
    EXPECT_TRUE(store_->update_from_l2(message));
    
    // Verify all levels
    SecurityStore::SecuritySnapshot snapshot;
    EXPECT_TRUE(store_->get_security_snapshot(aapl_id_, snapshot));
    
    EXPECT_EQ(snapshot.num_bid_levels, 5);
    EXPECT_EQ(snapshot.num_ask_levels, 5);
    
    // Check bid levels are in descending order
    for (int i = 0; i < 4; ++i) {
        EXPECT_GT(snapshot.bids[i].price, snapshot.bids[i + 1].price);
    }
    
    // Check ask levels are in ascending order
    for (int i = 0; i < 4; ++i) {
        EXPECT_LT(snapshot.asks[i].price, snapshot.asks[i + 1].price);
    }
    
    // Verify specific values
    EXPECT_EQ(snapshot.bids[0].price, 1750000);
    EXPECT_EQ(snapshot.bids[0].quantity, 1000);
    EXPECT_EQ(snapshot.asks[0].price, 1750500);
    EXPECT_EQ(snapshot.asks[0].quantity, 800);
}

TEST_F(SecurityStoreTest, EmptyOrderBook) {
    store_->add_security(aapl_id_);
    
    // Create message with no order book levels
    MarketDataL2Message message{};
    message.security_id = aapl_id_;
    message.timestamp_ns = get_current_time_ns();
    message.num_bid_levels = 0;
    message.num_ask_levels = 0;
    
    EXPECT_TRUE(store_->update_from_l2(message));
    
    SecurityStore::SecuritySnapshot snapshot;
    EXPECT_TRUE(store_->get_security_snapshot(aapl_id_, snapshot));
    
    EXPECT_EQ(snapshot.num_bid_levels, 0);
    EXPECT_EQ(snapshot.num_ask_levels, 0);
    EXPECT_EQ(snapshot.best_bid, 0);
    EXPECT_EQ(snapshot.best_ask, 0);
    
    // Mid price should return last trade price (0 in this case)
    EXPECT_EQ(snapshot.get_mid_price(), 0);
    EXPECT_EQ(snapshot.get_spread_bps(), 0.0);
}

TEST_F(SecurityStoreTest, TrulyLockFreeValidation) {
    // This test validates that the store is truly lock-free by:
    // 1. Adding securities from multiple threads simultaneously
    // 2. Updating from one thread while reading from others
    // 3. No deadlocks or blocking should occur
    
    std::atomic<bool> start_flag{false};
    std::atomic<int> add_success_count{0};
    std::atomic<int> update_count{0};
    std::atomic<int> read_count{0};
    std::atomic<int> error_count{0};
    
    // Create multiple securities to add
    std::vector<SecurityId> test_securities;
    for (int i = 0; i < 50; ++i) {
        std::string symbol = "TEST" + std::to_string(i);
        test_securities.push_back(SecuritySeeder::create_security_id(symbol));
    }
    
    // Thread 1: Add securities
    std::thread adder([&]() {
        while (!start_flag.load()) std::this_thread::yield();
        
        for (const auto& sec_id : test_securities) {
            if (store_->add_security(sec_id)) {
                add_success_count.fetch_add(1);
            }
            std::this_thread::yield();
        }
    });
    
    // Thread 2: Update securities
    std::thread updater([&]() {
        while (!start_flag.load()) std::this_thread::yield();
        
        for (int i = 0; i < 1000; ++i) {
            // Try to update a random security
            if (!test_securities.empty()) {
                auto& sec_id = test_securities[i % test_securities.size()];
                auto message = create_test_message(sec_id, 1000000 + i, 1000500 + i);
                if (store_->update_from_l2(message)) {
                    update_count.fetch_add(1);
                }
            }
            std::this_thread::yield();
        }
    });
    
    // Thread 3: Read securities
    std::thread reader([&]() {
        while (!start_flag.load()) std::this_thread::yield();
        
        for (int i = 0; i < 2000; ++i) {
            // Try to read all securities
            auto all_securities = store_->get_all_securities();
            read_count.fetch_add(1);
            
            // Try to get snapshots
            for (const auto& sec_id : all_securities) {
                SecurityStore::SecuritySnapshot snapshot;
                if (!store_->get_security_snapshot(sec_id, snapshot)) {
                    error_count.fetch_add(1);
                }
            }
            std::this_thread::yield();
        }
    });
    
    // Start all threads simultaneously
    start_flag.store(true);
    
    // Wait for completion
    adder.join();
    updater.join();
    reader.join();
    
    // Validate results - no errors should occur in a truly lock-free system
    EXPECT_EQ(error_count.load(), 0);
    EXPECT_GT(add_success_count.load(), 0);
    EXPECT_GT(read_count.load(), 1000);
    
    // Final state should be consistent
    EXPECT_EQ(store_->size(), add_success_count.load());
}

TEST_F(SecurityStoreTest, MaxCapacityHandling) {
    // Test that the store handles reaching maximum capacity gracefully
    std::vector<SecurityId> securities;
    
    // Add securities up to the limit
    for (size_t i = 0; i < SecurityStore::MAX_SECURITIES; ++i) {
        std::string symbol = "SEC" + std::to_string(i);
        SecurityId sec_id = SecuritySeeder::create_security_id(symbol);
        securities.push_back(sec_id);
        
        EXPECT_TRUE(store_->add_security(sec_id));
        EXPECT_EQ(store_->size(), i + 1);
    }
    
    // Adding one more should fail
    SecurityId overflow_sec = SecuritySeeder::create_security_id("OVERFLOW");
    EXPECT_FALSE(store_->add_security(overflow_sec));
    EXPECT_EQ(store_->size(), SecurityStore::MAX_SECURITIES);
    
    // Remove one and try adding again
    EXPECT_TRUE(store_->remove_security(securities[0]));
    EXPECT_EQ(store_->size(), SecurityStore::MAX_SECURITIES - 1);
    
    EXPECT_TRUE(store_->add_security(overflow_sec));
    EXPECT_EQ(store_->size(), SecurityStore::MAX_SECURITIES);
}
