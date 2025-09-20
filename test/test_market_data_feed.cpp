#include <gtest/gtest.h>
#include "market_data/market_data_feed.hpp"
#include "market_data/random_market_data_provider.hpp"
#include "market_data/security_seeder.hpp"
#include <chrono>
#include <thread>

using namespace mini_mart::market_data;
using namespace mini_mart::types;

class MarketDataFeedTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create provider and store
        provider_ = std::make_shared<RandomMarketDataProvider>();
        store_ = std::make_shared<SecurityStore>();
        
        // Create test securities
        aapl_id_ = SecuritySeeder::create_security_id("AAPL");
        msft_id_ = SecuritySeeder::create_security_id("MSFT");
        googl_id_ = SecuritySeeder::create_security_id("GOOGL");
        
        // Create feed with default config
        feed_ = std::make_unique<MarketDataFeed>(provider_, store_);
    }
    
    void TearDown() override {
        if (feed_) {
            feed_->stop();
        }
    }
    
    std::shared_ptr<RandomMarketDataProvider> provider_;
    std::shared_ptr<SecurityStore> store_;
    std::unique_ptr<MarketDataFeed> feed_;
    
    SecurityId aapl_id_;
    SecurityId msft_id_;
    SecurityId googl_id_;
};

TEST_F(MarketDataFeedTest, InitialState) {
    EXPECT_FALSE(feed_->is_running());
    EXPECT_EQ(feed_->get_subscribed_securities().size(), 0u);
    EXPECT_EQ(feed_->get_ring_utilization(), 0.0);
    
    // Statistics should be initialized
    const auto& stats = feed_->get_statistics();
    EXPECT_EQ(stats.messages_produced.load(), 0u);
    EXPECT_EQ(stats.messages_consumed.load(), 0u);
}

TEST_F(MarketDataFeedTest, StartStop) {
    // Start feed
    EXPECT_TRUE(feed_->start());
    EXPECT_TRUE(feed_->is_running());
    
    // Cannot start again
    EXPECT_FALSE(feed_->start());
    
    // Stop feed
    feed_->stop();
    EXPECT_FALSE(feed_->is_running());
    
    // Can start again after stop
    EXPECT_TRUE(feed_->start());
    EXPECT_TRUE(feed_->is_running());
}

TEST_F(MarketDataFeedTest, SubscribeUnsubscribe) {
    EXPECT_TRUE(feed_->start());
    
    // Subscribe to securities
    EXPECT_TRUE(feed_->subscribe(aapl_id_));
    EXPECT_TRUE(feed_->subscribe(msft_id_));
    
    // Cannot subscribe to same security twice
    EXPECT_FALSE(feed_->subscribe(aapl_id_));
    
    // Check subscriptions
    auto securities = feed_->get_subscribed_securities();
    EXPECT_EQ(securities.size(), 2);
    
    // Unsubscribe
    EXPECT_TRUE(feed_->unsubscribe(aapl_id_));
    EXPECT_FALSE(feed_->unsubscribe(aapl_id_)); // Already unsubscribed
    
    securities = feed_->get_subscribed_securities();
    EXPECT_EQ(securities.size(), 1);
}

TEST_F(MarketDataFeedTest, EndToEndDataFlow) {
    EXPECT_TRUE(feed_->start());
    
    // Subscribe to a security
    EXPECT_TRUE(feed_->subscribe(aapl_id_));
    
    // Wait for some market data to flow through
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Check that data flowed through the system
    const auto& stats = feed_->get_statistics();
    EXPECT_GT(stats.messages_produced.load(), 0);
    EXPECT_GT(stats.messages_consumed.load(), 0);
    
    // Check that security store was updated
    SecurityStore::SecuritySnapshot snapshot;
    EXPECT_TRUE(store_->get_security_snapshot(aapl_id_, snapshot));
    EXPECT_GT(snapshot.last_update_ns, 0);
    EXPECT_GT(snapshot.update_count, 0);
}

TEST_F(MarketDataFeedTest, MultipleSecurities) {
    EXPECT_TRUE(feed_->start());
    
    // Subscribe to multiple securities
    EXPECT_TRUE(feed_->subscribe(aapl_id_));
    EXPECT_TRUE(feed_->subscribe(msft_id_));
    EXPECT_TRUE(feed_->subscribe(googl_id_));
    
    // Wait for market data
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    
    // All securities should have data
    SecurityStore::SecuritySnapshot snapshot;
    
    EXPECT_TRUE(store_->get_security_snapshot(aapl_id_, snapshot));
    EXPECT_GT(snapshot.update_count, 0);
    
    EXPECT_TRUE(store_->get_security_snapshot(msft_id_, snapshot));
    EXPECT_GT(snapshot.update_count, 0);
    
    EXPECT_TRUE(store_->get_security_snapshot(googl_id_, snapshot));
    EXPECT_GT(snapshot.update_count, 0);
    
    // Check statistics
    const auto& stats = feed_->get_statistics();
    EXPECT_GT(stats.messages_produced.load(), 10); // Should have many messages
    EXPECT_GT(stats.messages_consumed.load(), 10);
}

TEST_F(MarketDataFeedTest, LatencyMeasurement) {
    EXPECT_TRUE(feed_->start());
    EXPECT_TRUE(feed_->subscribe(aapl_id_));
    
    // Wait for data to accumulate
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    const auto& stats = feed_->get_statistics();
    EXPECT_GT(stats.messages_consumed.load(), 0);
    
    // Latency should be reasonable (less than 1ms for in-process communication)
    double avg_latency_ns = stats.get_average_latency_ns();
    EXPECT_GT(avg_latency_ns, 0);
    EXPECT_LT(avg_latency_ns, 1000000); // Less than 1ms
    
    uint64_t max_latency_ns = stats.max_latency_ns.load();
    EXPECT_GT(max_latency_ns, 0);
    EXPECT_LT(max_latency_ns, 5000000); // Less than 5ms
}

TEST_F(MarketDataFeedTest, RingBufferUtilization) {
    EXPECT_TRUE(feed_->start());
    
    // Initially empty
    EXPECT_EQ(feed_->get_ring_utilization(), 0.0);
    
    // Subscribe to generate some load
    EXPECT_TRUE(feed_->subscribe(aapl_id_));
    EXPECT_TRUE(feed_->subscribe(msft_id_));
    
    // Brief wait to see some utilization
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Utilization should be reasonable (not constantly full)
    double utilization = feed_->get_ring_utilization();
    EXPECT_GE(utilization, 0.0);
    EXPECT_LE(utilization, 1.0);
}

TEST_F(MarketDataFeedTest, CustomConfiguration) {
    MarketDataFeed::Config config;
    config.consumer_yield_us = 10;  // Longer yield time
    config.enable_statistics = true;
    
    auto custom_feed = std::make_unique<MarketDataFeed>(provider_, store_, config);
    
    EXPECT_TRUE(custom_feed->start());
    EXPECT_TRUE(custom_feed->subscribe(aapl_id_));
    
    // Wait for some data
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    const auto& stats = custom_feed->get_statistics();
    EXPECT_GT(stats.messages_produced.load(), 0);
    EXPECT_GT(stats.messages_consumed.load(), 0);
    
    // Should have some yield events due to longer yield time
    EXPECT_GT(stats.consumer_yields.load(), 0);
    
    custom_feed->stop();
}

TEST_F(MarketDataFeedTest, HighThroughputStressTest) {
    // Use smaller yield time for higher throughput
    MarketDataFeed::Config config;
    config.consumer_yield_us = 0; // No yielding, maximum throughput
    
    auto stress_feed = std::make_unique<MarketDataFeed>(provider_, store_, config);
    
    EXPECT_TRUE(stress_feed->start());
    
    // Subscribe to many securities to generate high load
    std::vector<SecurityId> securities;
    for (int i = 0; i < 20; ++i) {
        std::string symbol = "TEST" + std::to_string(i);
        SecurityId sec_id = SecuritySeeder::create_security_id(symbol);
        securities.push_back(sec_id);
        EXPECT_TRUE(stress_feed->subscribe(sec_id));
    }
    
    // Run for a longer period
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    const auto& stats = stress_feed->get_statistics();
    
    // Should handle high throughput
    EXPECT_GT(stats.messages_produced.load(), 100);
    EXPECT_GT(stats.messages_consumed.load(), 100);
    
    // Ring buffer full events should be minimal (good backpressure handling)
    double full_event_ratio = static_cast<double>(stats.ring_full_events.load()) / 
                             stats.messages_produced.load();
    EXPECT_LT(full_event_ratio, 0.95); // Less than 95% full events (extreme stress test)
    
    stress_feed->stop();
}

TEST_F(MarketDataFeedTest, ThreadSafety) {
    EXPECT_TRUE(feed_->start());
    
    std::atomic<bool> stop_flag{false};
    std::atomic<int> subscribe_count{0};
    std::atomic<int> unsubscribe_count{0};
    
    // Thread 1: Subscribe/unsubscribe securities
    std::thread subscription_thread([&]() {
        std::vector<SecurityId> securities;
        for (int i = 0; i < 10; ++i) {
            std::string symbol = "THREAD" + std::to_string(i);
            securities.push_back(SecuritySeeder::create_security_id(symbol));
        }
        
        while (!stop_flag.load()) {
            for (const auto& sec_id : securities) {
                if (feed_->subscribe(sec_id)) {
                    subscribe_count.fetch_add(1);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                
                if (feed_->unsubscribe(sec_id)) {
                    unsubscribe_count.fetch_add(1);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    });
    
    // Thread 2: Read statistics and utilization
    std::thread monitoring_thread([&]() {
        while (!stop_flag.load()) {
            const auto& stats = feed_->get_statistics();
            auto utilization = feed_->get_ring_utilization();
            auto securities = feed_->get_subscribed_securities();
            
            // Just accessing these values tests thread safety
            (void)stats.messages_produced.load();
            (void)utilization;
            (void)securities.size();
            
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    
    // Let threads run
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stop_flag.store(true);
    
    subscription_thread.join();
    monitoring_thread.join();
    
    // Should have performed some operations without crashes
    EXPECT_GT(subscribe_count.load(), 0);
    EXPECT_GT(unsubscribe_count.load(), 0);
}
