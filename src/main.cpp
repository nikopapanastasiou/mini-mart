#include "market_data/market_data_feed.hpp"
#include "market_data/random_market_data_provider.hpp"
#include "market_data/security_seeder.hpp"
#include "common/time_utils.hpp"
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>

// Global pointer for signal handler access
std::unique_ptr<mini_mart::market_data::MarketDataFeed> g_feed;

// Signal handler for graceful shutdown
void signal_handler(int signal) {
  std::cout << "\nReceived signal " << signal << ", shutting down gracefully..."
            << std::endl;
  if (g_feed) {
    g_feed->stop();
  }
}

int main() {

  // HFT STRESS TEST configuration: Simulate wild market activity spikes
  mini_mart::market_data::RandomMarketDataProvider::Config hft_config;
  hft_config.update_interval_us = 50;        // 50 microseconds - aggressive
  hft_config.messages_per_burst = 3;         // 3 messages per security normally
  hft_config.volatility = 0.005;             // Higher volatility for stress
  
  // ACTIVITY SPIKE SIMULATION
  hft_config.enable_activity_spikes = true;  // Enable stress spikes
  hft_config.spike_probability = 10;         // 10% chance of spike per cycle
  hft_config.spike_multiplier = 15;          // 15x message burst during spikes
  hft_config.spike_duration_us = 2000;       // 2ms spike duration

  auto provider =
      std::make_shared<mini_mart::market_data::RandomMarketDataProvider>(
          hft_config);
  auto store = std::make_shared<mini_mart::market_data::SecurityStore>();
  g_feed =
      std::make_unique<mini_mart::market_data::MarketDataFeed>(provider, store);

  // Register signal handlers for graceful shutdown
  std::signal(SIGINT, signal_handler);  // Ctrl+C
  std::signal(SIGTERM, signal_handler); // Termination request

  auto started = g_feed->start();

  if (!started) {
    std::cerr << "Failed to start market data feed" << std::endl;
    return 1;
  }

  g_feed->subscribe(mini_mart::market_data::SecuritySeeder::create_security_id("AAPL"));
  g_feed->subscribe(mini_mart::market_data::SecuritySeeder::create_security_id("MSFT"));
  g_feed->subscribe(mini_mart::market_data::SecuritySeeder::create_security_id("GOOGL"));
  g_feed->subscribe(mini_mart::market_data::SecuritySeeder::create_security_id("TSLA"));
  g_feed->subscribe(mini_mart::market_data::SecuritySeeder::create_security_id("META"));
  g_feed->subscribe(mini_mart::market_data::SecuritySeeder::create_security_id("AMZN"));
  g_feed->subscribe(mini_mart::market_data::SecuritySeeder::create_security_id("NVDA"));
  g_feed->subscribe(mini_mart::market_data::SecuritySeeder::create_security_id("NFLX"));

  while (g_feed->is_running()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    const auto &stats = g_feed->get_statistics();

    std::cout << "Messages produced: " << stats.messages_produced.load()
              << std::endl;
    std::cout << "Messages consumed: " << stats.messages_consumed.load()
              << std::endl;
    std::cout << "Ring full events: " << stats.ring_full_events.load()
              << std::endl;
    std::cout << "Ring empty events: " << stats.ring_empty_events.load()
              << std::endl;
    std::cout << "Consumer yields: " << stats.consumer_yields.load()
              << std::endl;
    std::cout << "Total latency: " << stats.total_latency_ns.load() << " ns"
              << std::endl;
    std::cout << "Max latency: " << stats.max_latency_ns.load() << " ns"
              << std::endl;
  }

  // Clean shutdown
  std::cout << "Market data feed stopped. Goodbye!" << std::endl;
  return 0;
}