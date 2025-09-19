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

// Lock-free market data feed using SPSC ring buffer
class MarketDataFeed {
public:
  static constexpr size_t DEFAULT_RING_SIZE = 1024;

  struct Config {
    uint32_t consumer_yield_us;
    bool enable_statistics;

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

    Statistics() = default;
    Statistics(const Statistics &) = delete;
    Statistics &operator=(const Statistics &) = delete;
    Statistics(Statistics &&) = delete;
    Statistics &operator=(Statistics &&) = delete;

    double get_average_latency_ns() const {
      uint64_t consumed = messages_consumed.load(std::memory_order_relaxed);
      if (consumed == 0)
        return 0.0;
      return static_cast<double>(
                 total_latency_ns.load(std::memory_order_relaxed)) /
             consumed;
    }

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

  explicit MarketDataFeed(std::shared_ptr<MarketDataProvider> provider,
                          std::shared_ptr<SecurityStore> store,
                          const Config &config = Config())
      : provider_(std::move(provider)), store_(std::move(store)),
        config_(config) {
    provider_->set_callback([this](const MarketDataL2Message &message) {
      this->on_market_data_received(message);
    });
  }

  ~MarketDataFeed() { stop(); }

  MarketDataFeed(const MarketDataFeed &) = delete;
  MarketDataFeed &operator=(const MarketDataFeed &) = delete;
  MarketDataFeed(MarketDataFeed &&) = delete;
  MarketDataFeed &operator=(MarketDataFeed &&) = delete;

  bool start() {
    if (running_.load(std::memory_order_acquire)) {
      return false;
    }

    if (config_.enable_statistics) {
      stats_.reset();
    }

    if (!provider_->start()) {
      return false;
    }

    running_.store(true, std::memory_order_release);
    consumer_thread_ = std::thread(&MarketDataFeed::consumer_thread_func, this);

    return true;
  }

  void stop() {
    if (!running_.load(std::memory_order_acquire)) {
      return;
    }

    running_.store(false, std::memory_order_release);
    provider_->stop();

    if (consumer_thread_.joinable()) {
      consumer_thread_.join();
    }
  }

  bool is_running() const { return running_.load(std::memory_order_acquire); }

  bool subscribe(const SecurityId &security_id) {
    if (!store_->add_security(security_id)) {
      return false;
    }

    if (!provider_->subscribe(security_id)) {
      store_->remove_security(security_id);
      return false;
    }

    return true;
  }

  bool unsubscribe(const SecurityId &security_id) {
    bool provider_result = provider_->unsubscribe(security_id);
    bool store_result = store_->remove_security(security_id);
    return provider_result && store_result;
  }

  const Statistics &get_statistics() const { return stats_; }

  double get_ring_utilization() const {
    return static_cast<double>(ring_buffer_.size()) /
           ring_buffer_.get_capacity();
  }

  std::vector<SecurityId> get_subscribed_securities() const {
    return provider_->get_subscribed_securities();
  }

private:
  void on_market_data_received(const MarketDataL2Message &message) {
    if (!running_.load(std::memory_order_acquire)) {
      return;
    }

    MarketDataL2Message timestamped_message = message;
    if (config_.enable_statistics) {
      timestamped_message.timestamp_ns = get_current_time_ns();
    }

    if (ring_buffer_.try_push(std::move(timestamped_message))) {
      if (config_.enable_statistics) {
        stats_.messages_produced.fetch_add(1, std::memory_order_relaxed);
      }
    } else {
      if (config_.enable_statistics) {
        stats_.ring_full_events.fetch_add(1, std::memory_order_relaxed);
      }
    }
  }

  void consumer_thread_func() {
    MarketDataL2Message message;

    while (running_.load(std::memory_order_acquire)) {
      if (ring_buffer_.try_pop(message)) {
        bool updated = store_->update_from_l2(message);

        if (config_.enable_statistics && updated) {
          stats_.messages_consumed.fetch_add(1, std::memory_order_relaxed);

          uint64_t current_time = get_current_time_ns();
          uint64_t latency = current_time - message.timestamp_ns;

          stats_.total_latency_ns.fetch_add(latency, std::memory_order_relaxed);

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
        if (config_.enable_statistics) {
          stats_.ring_empty_events.fetch_add(1, std::memory_order_relaxed);
        }

        if (config_.consumer_yield_us > 0) {
          std::this_thread::sleep_for(
              std::chrono::microseconds(config_.consumer_yield_us));
          if (config_.enable_statistics) {
            stats_.consumer_yields.fetch_add(1, std::memory_order_relaxed);
          }
        } else {
          std::this_thread::yield();
        }
      }
    }
  }

  uint64_t get_current_time_ns() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration)
        .count();
  }

  std::shared_ptr<MarketDataProvider> provider_;
  std::shared_ptr<SecurityStore> store_;
  Config config_;
  SpscRing<MarketDataL2Message, DEFAULT_RING_SIZE> ring_buffer_;
  std::atomic<bool> running_{false};
  std::thread consumer_thread_;
  mutable Statistics stats_;
};

} // namespace mini_mart::market_data
