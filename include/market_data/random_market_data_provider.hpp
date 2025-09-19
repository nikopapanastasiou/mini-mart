#pragma once

#include "market_data_provider.hpp"
#include "security_seeder.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <random>
#include <thread>

namespace mini_mart::market_data {

using namespace mini_mart::types;

// Lock-free random market data provider for simulation and testing
class RandomMarketDataProvider : public MarketDataProvider {
public:
  static constexpr size_t MAX_SECURITIES = 256;

  struct Config {
    double base_price;
    double volatility;
    double spread_bps;
    uint32_t update_interval_us;
    uint32_t max_quantity;
    uint32_t min_quantity;
    uint32_t messages_per_burst;
    bool enable_activity_spikes;
    uint32_t spike_probability;
    uint32_t spike_multiplier;
    uint32_t spike_duration_us;

    Config()
        : base_price(150.0), volatility(0.02), spread_bps(2.0),
          update_interval_us(10), max_quantity(1000), min_quantity(100),
          messages_per_burst(5), enable_activity_spikes(false),
          spike_probability(5), spike_multiplier(10), spike_duration_us(1000) {}
  };

  explicit RandomMarketDataProvider(const Config &config = Config())
      : config_(config) {}

  ~RandomMarketDataProvider() override { stop(); }
  bool start() override {
    if (running_.load()) {
      return false;
    }

    running_.store(true);
    market_data_thread_ =
        std::thread(&RandomMarketDataProvider::market_data_thread, this);
    return true;
  }

  void stop() override {
    if (!running_.load()) {
      return;
    }

    running_.store(false);
    if (market_data_thread_.joinable()) {
      market_data_thread_.join();
    }
  }

  bool is_running() const override { return running_.load(); }

  bool subscribe(const SecurityId &security_id) override {
    if (find_security_slot(security_id) != nullptr) {
      return false;
    }

    for (size_t i = 0; i < MAX_SECURITIES; ++i) {
      SecuritySlot &slot = securities_[i];
      bool expected = false;
      if (slot.active.compare_exchange_strong(expected, false,
                                              std::memory_order_acquire)) {
        slot.initialize(security_id, get_security_base_price(security_id));
        active_count_.fetch_add(1, std::memory_order_relaxed);
        return true;
      }
    }

    return false;
  }

  bool unsubscribe(const SecurityId &security_id) override {
    SecuritySlot *slot = find_security_slot(security_id);
    if (!slot) {
      return false;
    }

    slot->deactivate();
    active_count_.fetch_sub(1, std::memory_order_relaxed);
    return true;
  }

  void set_callback(MarketDataCallback callback) override {
    callback_ = std::move(callback);
  }

  std::vector<SecurityId> get_subscribed_securities() const override {
    std::vector<SecurityId> result;
    result.reserve(active_count_.load(std::memory_order_relaxed));

    for (size_t i = 0; i < MAX_SECURITIES; ++i) {
      const SecuritySlot &slot = securities_[i];
      if (slot.active.load(std::memory_order_acquire)) {
        result.push_back(slot.security_id);
      }
    }

    return result;
  }

private:
  struct alignas(64) SecuritySlot {
    std::atomic<bool> active{false};
    SecurityId security_id{};
    double current_price{0.0};
    uint64_t last_update_ns{0};
    mutable std::mt19937 rng;

    SecuritySlot() : rng(std::random_device{}()) {}

    void initialize(const SecurityId &id, double base_price) {
      security_id = id;
      current_price = base_price;
      last_update_ns = 0;

      uint32_t seed = 0;
      for (size_t i = 0; i < id.size(); ++i) {
        seed ^= static_cast<uint32_t>(id[i]) << (i % 4 * 8);
      }
      rng.seed(seed);
      active.store(true, std::memory_order_release);
    }

    void deactivate() { active.store(false, std::memory_order_release); }

    bool matches(const SecurityId &id) const {
      return active.load(std::memory_order_acquire) && (security_id == id);
    }

    SecuritySlot(const SecuritySlot &) = delete;
    SecuritySlot &operator=(const SecuritySlot &) = delete;
    SecuritySlot(SecuritySlot &&) = delete;
    SecuritySlot &operator=(SecuritySlot &&) = delete;
  };

  double get_security_base_price(const SecurityId &security_id) const {
    std::string symbol = SecuritySeeder::security_id_to_string(security_id);
    return SecuritySeeder::get_base_price(symbol, config_.base_price);
  }

  SecuritySlot *find_security_slot(const SecurityId &security_id) const {
    for (size_t i = 0; i < MAX_SECURITIES; ++i) {
      SecuritySlot &slot = const_cast<SecuritySlot &>(securities_[i]);
      if (slot.matches(security_id)) {
        return &slot;
      }
    }
    return nullptr;
  }

  void market_data_thread() {
    static thread_local uint64_t spike_rng_state = 12345;
    auto spike_end_time = std::chrono::steady_clock::now();
    bool in_spike = false;
    
    while (running_.load()) {
      auto start_time = std::chrono::steady_clock::now();

      // Check for activity spike
      uint32_t burst_multiplier = 1;
      if (config_.enable_activity_spikes) {
        if (!in_spike) {
          // Check if we should start a spike
          spike_rng_state = spike_rng_state * 1103515245 + 12345;
          if ((spike_rng_state % 100) < config_.spike_probability) {
            in_spike = true;
            burst_multiplier = config_.spike_multiplier;
            spike_end_time = start_time + std::chrono::microseconds(config_.spike_duration_us);
          }
        } else {
          // Check if spike is over
          if (start_time >= spike_end_time) {
            in_spike = false;
          } else {
            burst_multiplier = config_.spike_multiplier;
          }
        }
      }

      // Generate messages with potential spike multiplier
      for (size_t i = 0; i < MAX_SECURITIES; ++i) {
        SecuritySlot &slot = securities_[i];
        if (slot.active.load(std::memory_order_acquire)) {
          // Generate burst of messages per security (with spike multiplier)
          uint32_t total_bursts = config_.messages_per_burst * burst_multiplier;
          for (uint32_t burst = 0; burst < total_bursts; ++burst) {
            generate_market_data_for_security(slot.security_id, slot);
          }
        }
      }

      // Sleep for MICROSECONDS (reduced during spikes)
      auto end_time = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
          end_time - start_time);
      
      // Reduce sleep time during spikes for even higher frequency
      uint32_t effective_interval = in_spike ? 
          config_.update_interval_us / 2 : config_.update_interval_us;
      auto sleep_time =
          std::chrono::microseconds(effective_interval) - elapsed;

      if (sleep_time > std::chrono::microseconds(0)) {
        std::this_thread::sleep_for(sleep_time);
      }
    }
  }

  void generate_market_data_for_security(const SecurityId &security_id,
                                         SecuritySlot &slot) {
    if (!callback_) {
      return;
    }

    static thread_local uint64_t fast_rng_state = 1;
    fast_rng_state = fast_rng_state * 1103515245 + 12345;
    double price_change = ((fast_rng_state & 0xFFFF) / 65535.0 - 0.5) * 0.001;
    
    slot.current_price *= (1.0 + price_change);
    if (slot.current_price < 1.0) slot.current_price = 1.0;
    slot.last_update_ns = get_current_time_ns();

    auto message = create_l2_message(security_id, slot);
    callback_(message);
  }

  MarketDataL2Message create_l2_message(const SecurityId &security_id,
                                        const SecuritySlot &slot) {
    MarketDataL2Message message{};

    message.header.seq_no = 0;
    message.header.length = sizeof(MarketDataL2Message);
    message.header.type = static_cast<uint16_t>(MessageType::MARKET_DATA_L2);

    message.security_id = security_id;
    message.timestamp_ns = slot.last_update_ns;

    double spread = slot.current_price * (config_.spread_bps / 10000.0);
    double mid_price = slot.current_price;
    double best_bid = mid_price - spread / 2.0;
    double best_ask = mid_price + spread / 2.0;

    static thread_local uint64_t qty_rng_state = 42;
    static thread_local uint64_t level_rng_state = 123;

    message.num_bid_levels = 5;
    double current_bid = best_bid;
    for (int i = 0; i < 5; ++i) {
      message.bids[i].price = double_to_price(current_bid);
      qty_rng_state = qty_rng_state * 1103515245 + 12345;
      message.bids[i].quantity = 100 + (qty_rng_state % 900);
      level_rng_state = level_rng_state * 1103515245 + 12345;
      double level_spacing = 0.0001 + ((level_rng_state & 0xFFFF) / 65535.0) * 0.0004;
      current_bid -= level_spacing * slot.current_price;
    }

    message.num_ask_levels = 5;
    double current_ask = best_ask;
    for (int i = 0; i < 5; ++i) {
      message.asks[i].price = double_to_price(current_ask);
      qty_rng_state = qty_rng_state * 1103515245 + 12345;
      message.asks[i].quantity = 100 + (qty_rng_state % 900);
      level_rng_state = level_rng_state * 1103515245 + 12345;
      double level_spacing = 0.0001 + ((level_rng_state & 0xFFFF) / 65535.0) * 0.0004;
      current_ask += level_spacing * slot.current_price;
    }

    return message;
  }

  uint64_t get_current_time_ns() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration)
        .count();
  }

  Price double_to_price(double price) const {
    return static_cast<Price>(price * 10000.0);
  }

  double price_to_double(Price price) const {
    return static_cast<double>(price) / 10000.0;
  }

  Config config_;
  std::atomic<bool> running_{false};
  std::thread market_data_thread_;
  MarketDataCallback callback_;
  std::array<SecuritySlot, MAX_SECURITIES> securities_;
  std::atomic<size_t> active_count_{0};
};

} // namespace mini_mart::market_data
