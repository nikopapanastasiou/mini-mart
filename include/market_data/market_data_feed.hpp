#pragma once

#include "common/spsc_ring.hpp"
#include "market_data/market_data_provider.hpp"
#include "market_data/security_store.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace mini_mart::market_data {

using namespace mini_mart::types;
using namespace mini_mart::common;

/**
 * @brief Lock-free market data feed using SPSC ring buffer
 *
 * This feed connects a market data provider to a security store using
 * a lock-free SPSC (Single Producer Single Consumer) ring buffer pattern:
 *
 * Producer Thread: Receives market data from provider -> pushes to ring buffer
 * Consumer Thread: Pops from ring buffer -> updates security store
 *
 * Key features:
 * - Completely lock-free operation
 * - High-throughput message passing via SPSC ring buffer
 * - Configurable ring buffer size (must be power of 2)
 * - Thread-safe start/stop lifecycle management
 * - Statistics tracking for monitoring performance
 */
class MarketDataFeed {
public:
  // Ring buffer size - must be power of 2 for optimal performance
  static constexpr size_t DEFAULT_RING_SIZE = 1024;

  struct Config {
    uint32_t consumer_yield_us; // Microseconds to yield when ring is empty
    bool enable_statistics;     // Enable performance statistics collection

    Config() : consumer_yield_us(1), enable_statistics(true) {}
  };

  struct Statistics {
    std::atomic<uint64_t> messages_produced{0};
    std::atomic<uint64_t> messages_consumed{0};
    std::atomic<uint64_t> ring_full_events{0};
    std::atomic<uint64_t> ring_empty_events{0};
    std::atomic<uint64_t> consumer_yields{0};
    std::atomic<uint64_t> total_latency_ns{0};
    std::atomic<uint64_t> max_latency_ns{0};

    // Non-copyable, non-movable due to atomics
    Statistics() = default;
    Statistics(const Statistics &) = delete;
    Statistics &operator=(const Statistics &) = delete;
    Statistics(Statistics &&) = delete;
    Statistics &operator=(Statistics &&) = delete;

    // Get average latency in nanoseconds
    double get_average_latency_ns() const {
      uint64_t consumed = messages_consumed.load(std::memory_order_relaxed);
      if (consumed == 0)
        return 0.0;
      return static_cast<double>(
                 total_latency_ns.load(std::memory_order_relaxed)) /
             consumed;
    }

    // Reset all statistics
    void reset() {
      messages_produced.store(0, std::memory_order_relaxed);
      messages_consumed.store(0, std::memory_order_relaxed);
      ring_full_events.store(0, std::memory_order_relaxed);
      ring_empty_events.store(0, std::memory_order_relaxed);
      consumer_yields.store(0, std::memory_order_relaxed);
      total_latency_ns.store(0, std::memory_order_relaxed);
      max_latency_ns.store(0, std::memory_order_relaxed);
    }
  };

  /**
   * @brief Construct market data feed
   * @param provider Market data provider (must be lock-free)
   * @param store Security store (must be lock-free)
   * @param config Feed configuration
   */
  explicit MarketDataFeed(std::shared_ptr<MarketDataProvider> provider,
                          std::shared_ptr<SecurityStore> store,
                          const Config &config = Config())
      : provider_(std::move(provider)), store_(std::move(store)),
        config_(config) {

    // Set up provider callback to push messages to ring buffer
    provider_->set_callback([this](const MarketDataL2Message &message) {
      this->on_market_data_received(message);
    });
  }

  ~MarketDataFeed() { stop(); }

  // Non-copyable, non-movable
  MarketDataFeed(const MarketDataFeed &) = delete;
  MarketDataFeed &operator=(const MarketDataFeed &) = delete;
  MarketDataFeed(MarketDataFeed &&) = delete;
  MarketDataFeed &operator=(MarketDataFeed &&) = delete;

  /**
   * @brief Start the market data feed
   * @return true if started successfully, false if already running
   */
  bool start() {
    if (running_.load(std::memory_order_acquire)) {
      return false; // Already running
    }

    // Reset statistics
    if (config_.enable_statistics) {
      stats_.reset();
    }

    // Start provider first
    if (!provider_->start()) {
      return false;
    }

    // Start our threads
    running_.store(true, std::memory_order_release);
    consumer_thread_ = std::thread(&MarketDataFeed::consumer_thread_func, this);

    return true;
  }

  /**
   * @brief Stop the market data feed
   */
  void stop() {
    if (!running_.load(std::memory_order_acquire)) {
      return; // Already stopped
    }

    // Signal threads to stop
    running_.store(false, std::memory_order_release);

    // Stop provider
    provider_->stop();

    // Wait for consumer thread to finish
    if (consumer_thread_.joinable()) {
      consumer_thread_.join();
    }
  }

  /**
   * @brief Check if the feed is running
   */
  bool is_running() const { return running_.load(std::memory_order_acquire); }

  /**
   * @brief Subscribe to a security (delegates to provider)
   * @param security_id Security to subscribe to
   * @return true if subscription successful
   */
  bool subscribe(const SecurityId &security_id) {
    // Add to store first
    if (!store_->add_security(security_id)) {
      return false; // Already exists or store full
    }

    // Subscribe to provider
    if (!provider_->subscribe(security_id)) {
      // Rollback store addition
      store_->remove_security(security_id);
      return false;
    }

    return true;
  }

  /**
   * @brief Unsubscribe from a security (delegates to provider)
   * @param security_id Security to unsubscribe from
   * @return true if unsubscription successful
   */
  bool unsubscribe(const SecurityId &security_id) {
    bool provider_result = provider_->unsubscribe(security_id);
    bool store_result = store_->remove_security(security_id);
    return provider_result && store_result;
  }

  /**
   * @brief Get current statistics
   */
  const Statistics &get_statistics() const { return stats_; }

  /**
   * @brief Get ring buffer utilization (0.0 to 1.0)
   */
  double get_ring_utilization() const {
    return static_cast<double>(ring_buffer_.size()) /
           ring_buffer_.get_capacity();
  }

  /**
   * @brief Get subscribed securities
   */
  std::vector<SecurityId> get_subscribed_securities() const {
    return provider_->get_subscribed_securities();
  }

private:
  /**
   * @brief Callback when market data is received from provider (producer)
   */
  void on_market_data_received(const MarketDataL2Message &message) {
    if (!running_.load(std::memory_order_acquire)) {
      return; // Feed is stopping
    }

    // Add timestamp for latency measurement
    MarketDataL2Message timestamped_message = message;
    if (config_.enable_statistics) {
      timestamped_message.timestamp_ns = get_current_time_ns();
    }

    // Try to push to ring buffer (non-blocking)
    if (ring_buffer_.try_push(std::move(timestamped_message))) {
      if (config_.enable_statistics) {
        stats_.messages_produced.fetch_add(1, std::memory_order_relaxed);
      }
    } else {
      // Ring buffer is full - this indicates backpressure
      if (config_.enable_statistics) {
        stats_.ring_full_events.fetch_add(1, std::memory_order_relaxed);
      }
      // In HFT systems, we might want to drop the message rather than block
      // This maintains deterministic latency at the cost of some data loss
    }
  }

  /**
   * @brief Consumer thread function - pops from ring and updates store
   */
  void consumer_thread_func() {
    MarketDataL2Message message;

    while (running_.load(std::memory_order_acquire)) {
      // Try to pop a message from the ring buffer
      if (ring_buffer_.try_pop(message)) {
        // Update the security store
        bool updated = store_->update_from_l2(message);

        if (config_.enable_statistics && updated) {
          // Only count messages that were successfully processed by the store
          stats_.messages_consumed.fetch_add(1, std::memory_order_relaxed);

          // Calculate end-to-end latency
          uint64_t current_time = get_current_time_ns();
          uint64_t latency = current_time - message.timestamp_ns;

          stats_.total_latency_ns.fetch_add(latency, std::memory_order_relaxed);

          // Update max latency (simple approach, may have race conditions but
          // acceptable for monitoring)
          uint64_t current_max =
              stats_.max_latency_ns.load(std::memory_order_relaxed);
          while (latency > current_max) {
            if (stats_.max_latency_ns.compare_exchange_weak(
                    current_max, latency, std::memory_order_relaxed)) {
              break;
            }
          }
        }
      } else {
        // Ring buffer is empty
        if (config_.enable_statistics) {
          stats_.ring_empty_events.fetch_add(1, std::memory_order_relaxed);
        }

        // Yield CPU briefly to avoid busy waiting
        if (config_.consumer_yield_us > 0) {
          std::this_thread::sleep_for(
              std::chrono::microseconds(config_.consumer_yield_us));
          if (config_.enable_statistics) {
            stats_.consumer_yields.fetch_add(1, std::memory_order_relaxed);
          }
        } else {
          // Just yield to scheduler
          std::this_thread::yield();
        }
      }
    }
  }

  /**
   * @brief Get current time in nanoseconds
   */
  uint64_t get_current_time_ns() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration)
        .count();
  }

  // Dependencies
  std::shared_ptr<MarketDataProvider> provider_;
  std::shared_ptr<SecurityStore> store_;
  Config config_;

  // SPSC ring buffer for message passing (fixed size for now)
  SpscRing<MarketDataL2Message, DEFAULT_RING_SIZE> ring_buffer_;

  // Thread management
  std::atomic<bool> running_{false};
  std::thread consumer_thread_;

  // Statistics
  mutable Statistics stats_;
};

} // namespace mini_mart::market_data
