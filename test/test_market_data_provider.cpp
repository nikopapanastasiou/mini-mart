#include "market_data/random_market_data_provider.hpp"
#include "market_data/security_seeder.hpp"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <set>
#include <thread>

using namespace mini_mart::market_data;
using namespace mini_mart::types;

class MarketDataProviderTest : public ::testing::Test {
protected:
  void SetUp() override {
    config_.base_price = 100.0;
    config_.volatility = 0.01;
    config_.spread_bps = 5.0;
    // Fast updates for testing
    config_.update_interval_us = 50;
    config_.max_quantity = 1000;
    config_.min_quantity = 100;

    provider_ = std::make_unique<RandomMarketDataProvider>(config_);
  }

  void TearDown() override {
    if (provider_->is_running()) {
      provider_->stop();
    }
  }

  RandomMarketDataProvider::Config config_;
  std::unique_ptr<RandomMarketDataProvider> provider_;
};

TEST_F(MarketDataProviderTest, InitialState) {
  EXPECT_FALSE(provider_->is_running());
  EXPECT_TRUE(provider_->get_subscribed_securities().empty());
}

TEST_F(MarketDataProviderTest, StartStop) {
  EXPECT_TRUE(provider_->start());
  EXPECT_TRUE(provider_->is_running());

  // Starting again should fail
  EXPECT_FALSE(provider_->start());

  provider_->stop();
  EXPECT_FALSE(provider_->is_running());

  // Stopping again should be safe
  provider_->stop();
  EXPECT_FALSE(provider_->is_running());
}

TEST_F(MarketDataProviderTest, SubscribeUnsubscribe) {
  auto aapl = SecuritySeeder::create_security_id("AAPL");
  auto msft = SecuritySeeder::create_security_id("MSFT");

  // Subscribe to securities
  EXPECT_TRUE(provider_->subscribe(aapl));
  EXPECT_TRUE(provider_->subscribe(msft));

  // Subscribing again should fail
  EXPECT_FALSE(provider_->subscribe(aapl));

  // Check subscribed securities
  auto securities = provider_->get_subscribed_securities();
  EXPECT_EQ(securities.size(), 2u);

  // Unsubscribe
  EXPECT_TRUE(provider_->unsubscribe(aapl));
  EXPECT_FALSE(provider_->unsubscribe(aapl)); // Already unsubscribed

  securities = provider_->get_subscribed_securities();
  EXPECT_EQ(securities.size(), 1u);
}

TEST_F(MarketDataProviderTest, MarketDataGeneration) {
  std::atomic<int> message_count{0};
  MarketDataL2Message last_message{};

  // Set up callback
  provider_->set_callback([&](const MarketDataL2Message &msg) {
    last_message = msg;
    message_count++;
  });

  // Subscribe to a security
  auto aapl = SecuritySeeder::create_security_id("AAPL");
  EXPECT_TRUE(provider_->subscribe(aapl));

  // Start provider
  EXPECT_TRUE(provider_->start());

  // Wait for some messages
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // CRITICAL: Stop provider before checking results to prevent race condition
  provider_->stop();

  // Should have received messages
  EXPECT_GT(message_count.load(), 0);

  // Validate message structure
  EXPECT_EQ(last_message.header.type,
            static_cast<uint16_t>(MessageType::MARKET_DATA_L2));
  EXPECT_EQ(last_message.header.length, sizeof(MarketDataL2Message));
  EXPECT_EQ(last_message.security_id, aapl);
  EXPECT_GT(last_message.timestamp_ns, 0u);

  // Validate L2 data
  EXPECT_EQ(last_message.num_bid_levels, 5);
  EXPECT_EQ(last_message.num_ask_levels, 5);

  // Check bid levels are in descending order
  for (size_t i = 0; i < 4; ++i) {
    EXPECT_GE(last_message.bids[i].price, last_message.bids[i + 1].price);
  }

  // Check ask levels are in ascending order
  for (size_t i = 0; i < 4; ++i) {
    EXPECT_LE(last_message.asks[i].price, last_message.asks[i + 1].price);
  }

  // Check spread (best ask > best bid)
  EXPECT_GT(last_message.asks[0].price, last_message.bids[0].price);

  // Check quantities are within range
  for (size_t i = 0; i < 5; ++i) {
    EXPECT_GE(last_message.bids[i].quantity, config_.min_quantity);
    EXPECT_LE(last_message.bids[i].quantity, config_.max_quantity);
    EXPECT_GE(last_message.asks[i].quantity, config_.min_quantity);
    EXPECT_LE(last_message.asks[i].quantity, config_.max_quantity);
  }
}

TEST_F(MarketDataProviderTest, EquityPriceRanges) {
  std::atomic<int> message_count{0};
  std::vector<MarketDataL2Message> messages;

  provider_->set_callback([&](const MarketDataL2Message &msg) {
    messages.push_back(msg);
    message_count++;
  });

  // Subscribe to different equities
  auto aapl = SecuritySeeder::create_security_id("AAPL");
  auto googl = SecuritySeeder::create_security_id("GOOGL");

  EXPECT_TRUE(provider_->subscribe(aapl));
  EXPECT_TRUE(provider_->subscribe(googl));

  EXPECT_TRUE(provider_->start());

  // Wait for messages
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // CRITICAL: Stop provider before checking results to prevent race condition
  provider_->stop();

  EXPECT_GT(message_count.load(), 0);

  // Find messages for each security
  MarketDataL2Message *aapl_msg = nullptr;
  MarketDataL2Message *googl_msg = nullptr;

  for (auto &msg : messages) {
    if (msg.security_id == aapl) {
      aapl_msg = &msg;
    } else if (msg.security_id == googl) {
      googl_msg = &msg;
    }
  }

  ASSERT_NE(aapl_msg, nullptr);
  ASSERT_NE(googl_msg, nullptr);

  // AAPL should be around $175, GOOGL around $2800
  double aapl_price = aapl_msg->bids[0].price.dollars();
  double googl_price = googl_msg->bids[0].price.dollars();

  // Prices should be in reasonable ranges (allowing for volatility)
  EXPECT_GT(aapl_price, 100.0);
  EXPECT_LT(aapl_price, 300.0);
  EXPECT_GT(googl_price, 2000.0);
  EXPECT_LT(googl_price, 4000.0);

  // GOOGL should be significantly more expensive than AAPL
  EXPECT_GT(googl_price, aapl_price * 5);
}

TEST_F(MarketDataProviderTest, SpreadCalculation) {
  std::atomic<int> message_count{0};
  MarketDataL2Message last_message{};

  provider_->set_callback([&](const MarketDataL2Message &msg) {
    last_message = msg;
    message_count++;
  });

  auto aapl = SecuritySeeder::create_security_id("AAPL");
  EXPECT_TRUE(provider_->subscribe(aapl));
  EXPECT_TRUE(provider_->start());

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // CRITICAL: Stop provider before checking results to prevent race condition
  provider_->stop();

  EXPECT_GT(message_count.load(), 0);

  // Calculate spread
  double best_bid = last_message.bids[0].price.dollars();
  double best_ask = last_message.asks[0].price.dollars();
  double spread = best_ask - best_bid;
  double mid_price = (best_bid + best_ask) / 2.0;
  double spread_bps = (spread / mid_price) * 10000.0;

  // Spread should be close to configured spread (allowing for some variance)
  EXPECT_GT(spread_bps, config_.spread_bps * 0.8);
  EXPECT_LT(spread_bps, config_.spread_bps * 1.2);
}

TEST_F(MarketDataProviderTest, MultipleSecurities) {
  std::atomic<int> message_count{0};
  std::set<SecurityId> seen_securities;

  provider_->set_callback([&](const MarketDataL2Message &msg) {
    seen_securities.insert(msg.security_id);
    message_count++;
  });

  // Subscribe to multiple securities
  auto securities = SecuritySeeder::get_test_securities();
  for (const auto &security : securities) {
    EXPECT_TRUE(provider_->subscribe(security));
  }

  EXPECT_TRUE(provider_->start());

  // Wait for messages from all securities
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  // CRITICAL: Stop provider before checking results to prevent race condition
  provider_->stop();

  // Should have received messages from all securities
  EXPECT_EQ(seen_securities.size(), securities.size());
  EXPECT_GT(message_count.load(), static_cast<int>(securities.size()));
}

TEST_F(MarketDataProviderTest, ThreadSafety) {
  std::atomic<int> message_count{0};

  provider_->set_callback(
      [&](const MarketDataL2Message &) { message_count++; });

  // Start provider
  EXPECT_TRUE(provider_->start());

  // Subscribe/unsubscribe from multiple threads
  std::vector<std::thread> threads;
  std::atomic<bool> stop_flag{false};

  for (int i = 0; i < 3; ++i) {
    threads.emplace_back([&, i]() {
      auto security =
          SecuritySeeder::create_security_id("TEST" + std::to_string(i));
      while (!stop_flag.load()) {
        provider_->subscribe(security);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        provider_->unsubscribe(security);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    });
  }

  // Let threads run for a while
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  stop_flag.store(true);

  for (auto &thread : threads) {
    thread.join();
  }

  // Should not crash and should handle concurrent access gracefully
  EXPECT_TRUE(true); // If we get here, no crashes occurred
}

// Test SecuritySeeder functionality
TEST(SecuritySeederTest, CreateSecurityId) {
  auto aapl = SecuritySeeder::create_security_id("AAPL");
  auto converted = SecuritySeeder::security_id_to_string(aapl);
  EXPECT_EQ(converted, "AAPL");

  // Test truncation
  auto long_symbol = SecuritySeeder::create_security_id("VERYLONGSYMBOL");
  auto truncated = SecuritySeeder::security_id_to_string(long_symbol);
  EXPECT_EQ(truncated, "VERYLONG"); // Should be truncated to 8 chars
}

TEST(SecuritySeederTest, EquityLists) {
  auto equities = SecuritySeeder::get_major_us_equities();
  EXPECT_GT(equities.size(), 10u);

  auto test_securities = SecuritySeeder::get_test_securities();
  EXPECT_EQ(test_securities.size(), 10u); // Should return exactly 10 for testing

  // All test securities should be from equities list
  for (const auto &test_sec : test_securities) {
    bool found = false;
    for (const auto &equity : equities) {
      if (test_sec == equity) {
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found);
  }
}

TEST(SecuritySeederTest, CentralizedPricing) {
  // Test that centralized pricing works correctly
  EXPECT_EQ(SecuritySeeder::get_base_price("AAPL"), 175.0);
  EXPECT_EQ(SecuritySeeder::get_base_price("GOOGL"), 2800.0);
  EXPECT_EQ(SecuritySeeder::get_base_price("UNKNOWN", 999.0),
            999.0); // Default fallback

  // Test equity info structure
  const auto &equity_info = SecuritySeeder::get_equity_info();
  EXPECT_GT(equity_info.size(), 15);

  auto aapl_it = equity_info.find("AAPL");
  ASSERT_NE(aapl_it, equity_info.end());
  EXPECT_EQ(aapl_it->second.symbol, "AAPL");
  EXPECT_EQ(aapl_it->second.name, "Apple Inc.");
  EXPECT_EQ(aapl_it->second.base_price, 175.0);
}
