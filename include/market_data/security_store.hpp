#pragma once

#include "types/messages.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>

namespace mini_mart::market_data {

using namespace mini_mart::types;

// Lock-free security store for single producer, multiple readers
class SecurityStore {
public:
  static constexpr size_t MAX_SECURITIES = 256;

  struct alignas(64) SecurityData {
    std::atomic<bool> active{false};
    SecurityId security_id{};
    std::atomic<Price> best_bid{Price{0.0}};
    std::atomic<Price> best_ask{Price{0.0}};
    std::atomic<Price> last_trade_price{Price{0.0}};
    std::atomic<uint64_t> last_update_ns{0};

    struct alignas(8) OrderBookSide {
      std::atomic<uint8_t> num_levels{0};
      PriceLevel levels[5];
    };

    OrderBookSide bids;
    OrderBookSide asks;
    std::atomic<uint64_t> update_count{0};
    std::atomic<uint64_t> total_volume{0};

    SecurityData() = default;

    void initialize(const SecurityId &id) {
      security_id = id;
      best_bid.store(Price{0.0}, std::memory_order_relaxed);
      best_ask.store(Price{0.0}, std::memory_order_relaxed);
      last_trade_price.store(Price{0.0}, std::memory_order_relaxed);
      last_update_ns.store(0, std::memory_order_relaxed);
      update_count.store(0, std::memory_order_relaxed);
      total_volume.store(0, std::memory_order_relaxed);
      bids.num_levels.store(0, std::memory_order_relaxed);
      asks.num_levels.store(0, std::memory_order_relaxed);
      active.store(true, std::memory_order_release);
    }

    void deactivate() { active.store(false, std::memory_order_release); }

    bool matches(const SecurityId &id) const {
      return active.load(std::memory_order_acquire) && (security_id == id);
    }

    SecurityData(const SecurityData &) = delete;
    SecurityData &operator=(const SecurityData &) = delete;
    SecurityData(SecurityData &&) = delete;
    SecurityData &operator=(SecurityData &&) = delete;
  };

  struct SecuritySnapshot {
    SecurityId security_id;
    Price best_bid;
    Price best_ask;
    Price last_trade_price;
    uint64_t last_update_ns;
    uint8_t num_bid_levels;
    uint8_t num_ask_levels;
    PriceLevel bids[5];
    PriceLevel asks[5];
    uint64_t update_count;
    uint64_t total_volume;

    SecuritySnapshot() = default;

    Price get_mid_price() const {
      if (best_bid == Price{0.0} || best_ask == Price{0.0}) {
        return last_trade_price;
      }
      return (best_bid + best_ask) / 2u;
    }

    double get_spread_bps() const {
      if (best_bid == Price{0.0} || best_ask == Price{0.0}) {
        return 0.0;
      }
      Price mid = get_mid_price();
      if (mid == Price{0.0}) {
        return 0.0;
      }
      return ((best_ask - best_bid).dollars() / mid.dollars()) * 10000.0;
    }

    static double price_to_double(Price price) {
      return price.dollars();
    }
  };

  SecurityStore() = default;
  ~SecurityStore() = default;

  SecurityStore(const SecurityStore &) = delete;
  SecurityStore &operator=(const SecurityStore &) = delete;
  SecurityStore(SecurityStore &&) = delete;
  SecurityStore &operator=(SecurityStore &&) = delete;
  bool add_security(const SecurityId &security_id) {
    if (find_security_data(security_id) != nullptr) {
      return false;
    }

    for (size_t i = 0; i < MAX_SECURITIES; ++i) {
      SecurityData &slot = securities[i];
      bool expected = false;
      if (slot.active.compare_exchange_strong(expected, false,
                                              std::memory_order_acquire)) {
        slot.initialize(security_id);
        active_count.fetch_add(1, std::memory_order_relaxed);
        return true;
      }
    }

    return false;
  }

  bool remove_security(const SecurityId &security_id) {
    SecurityData *data = find_security_data(security_id);
    if (!data) {
      return false;
    }

    data->deactivate();
    active_count.fetch_sub(1, std::memory_order_relaxed);
    return true;
  }

  bool update_from_l2(const MarketDataL2Message &message) {
    SecurityData *data = find_security_data(message.security_id);
    if (!data) {
      return false;
    }

    data->last_update_ns.store(message.timestamp_ns, std::memory_order_release);

    if (message.num_bid_levels > 0) {
      data->best_bid.store(message.bids[0].price, std::memory_order_relaxed);
    }
    if (message.num_ask_levels > 0) {
      data->best_ask.store(message.asks[0].price, std::memory_order_relaxed);
    }

    update_order_book_side(data->bids, message.bids.data(),
                           message.num_bid_levels);
    update_order_book_side(data->asks, message.asks.data(),
                           message.num_ask_levels);
    data->update_count.fetch_add(1, std::memory_order_relaxed);

    return true;
  }

  bool get_security_snapshot(const SecurityId &security_id,
                             SecuritySnapshot &snapshot) const {
    const SecurityData *data = find_security_data(security_id);
    if (!data) {
      return false;
    }

    snapshot.security_id = data->security_id;
    snapshot.last_update_ns =
        data->last_update_ns.load(std::memory_order_acquire);
    snapshot.best_bid = data->best_bid.load(std::memory_order_relaxed);
    snapshot.best_ask = data->best_ask.load(std::memory_order_relaxed);
    snapshot.last_trade_price =
        data->last_trade_price.load(std::memory_order_relaxed);
    snapshot.update_count = data->update_count.load(std::memory_order_relaxed);
    snapshot.total_volume = data->total_volume.load(std::memory_order_relaxed);

    snapshot.num_bid_levels =
        data->bids.num_levels.load(std::memory_order_relaxed);
    snapshot.num_ask_levels =
        data->asks.num_levels.load(std::memory_order_relaxed);

    std::memcpy(snapshot.bids, data->bids.levels, sizeof(PriceLevel) * 5);
    std::memcpy(snapshot.asks, data->asks.levels, sizeof(PriceLevel) * 5);

    return true;
  }

  std::vector<SecurityId> get_all_securities() const {
    std::vector<SecurityId> result;
    result.reserve(active_count.load(std::memory_order_relaxed));

    for (size_t i = 0; i < MAX_SECURITIES; ++i) {
      const SecurityData &slot = securities[i];
      if (slot.active.load(std::memory_order_acquire)) {
        result.push_back(slot.security_id);
      }
    }

    return result;
  }

  size_t size() const { return active_count.load(std::memory_order_relaxed); }

  bool contains(const SecurityId &security_id) const {
    return find_security_data(security_id) != nullptr;
  }

  void clear() {
    for (size_t i = 0; i < MAX_SECURITIES; ++i) {
      securities[i].deactivate();
    }
    active_count.store(0, std::memory_order_relaxed);
  }

private:
  SecurityData *find_security_data(const SecurityId &security_id) const {
    for (size_t i = 0; i < MAX_SECURITIES; ++i) {
      SecurityData &slot = const_cast<SecurityData &>(securities[i]);
      if (slot.matches(security_id)) {
        return &slot;
      }
    }
    return nullptr;
  }

  void update_order_book_side(SecurityData::OrderBookSide &side,
                              const PriceLevel *levels, uint8_t num_levels) {
    uint8_t copy_count = std::min(num_levels, static_cast<uint8_t>(5));
    std::memcpy(side.levels, levels, sizeof(PriceLevel) * copy_count);

    for (uint8_t i = copy_count; i < 5; ++i) {
      side.levels[i] = PriceLevel{};
    }

    side.num_levels.store(copy_count, std::memory_order_release);
  }

  std::array<SecurityData, MAX_SECURITIES> securities;
  std::atomic<size_t> active_count{0};
};

} // namespace mini_mart::market_data
