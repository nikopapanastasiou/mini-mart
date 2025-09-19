#pragma once

#include "types/messages.hpp"
#include <atomic>
#include <chrono>
#include <cstring>
#include <array>

namespace mini_mart::market_data {

using namespace mini_mart::types;

/**
 * @brief Truly lock-free security store for high-frequency price updates
 *
 * This store is designed to be updated by a single producer (market data feed)
 * and read by multiple consumers. It uses NO LOCKS - only atomic operations
 * and pre-allocated fixed-size storage for true lock-free operation.
 *
 * Key features:
 * - Completely lock-free - no mutexes anywhere
 * - Pre-allocated security slots for deterministic performance
 * - Single-producer, multi-reader safe
 * - Cache-aligned data structures for performance
 * - Optimized for SPSC ring buffer integration
 */
class SecurityStore {
public:
  // Maximum number of securities supported (fixed at compile time)
  static constexpr size_t MAX_SECURITIES = 256;
  
  /**
   * @brief Security data with atomic price updates
   */
  struct alignas(64) SecurityData {
    // Atomic flag indicating if this slot is active
    std::atomic<bool> active{false};
    
    // Security identification (immutable after activation)
    SecurityId security_id{};
    
    // Core price data (atomically updated)
    std::atomic<Price> best_bid{0};
    std::atomic<Price> best_ask{0};
    std::atomic<Price> last_trade_price{0};
    std::atomic<uint64_t> last_update_ns{0};
    
    // L2 order book data (single writer, multiple readers)
    struct alignas(8) OrderBookSide {
      std::atomic<uint8_t> num_levels{0};
      PriceLevel levels[5]; // Non-atomic, single writer safe
    };
    
    OrderBookSide bids;
    OrderBookSide asks;
    
    // Statistics (relaxed ordering for performance counters)
    std::atomic<uint64_t> update_count{0};
    std::atomic<uint64_t> total_volume{0};
    
    SecurityData() = default;
    
    // Initialize with security ID
    void initialize(const SecurityId& id) {
      security_id = id;
      // Reset all atomic values
      best_bid.store(0, std::memory_order_relaxed);
      best_ask.store(0, std::memory_order_relaxed);
      last_trade_price.store(0, std::memory_order_relaxed);
      last_update_ns.store(0, std::memory_order_relaxed);
      update_count.store(0, std::memory_order_relaxed);
      total_volume.store(0, std::memory_order_relaxed);
      bids.num_levels.store(0, std::memory_order_relaxed);
      asks.num_levels.store(0, std::memory_order_relaxed);
      
      // Activate last (acts as memory barrier)
      active.store(true, std::memory_order_release);
    }
    
    // Deactivate this slot
    void deactivate() {
      active.store(false, std::memory_order_release);
    }
    
    // Check if this slot is active and matches the security
    bool matches(const SecurityId& id) const {
      return active.load(std::memory_order_acquire) && (security_id == id);
    }
    
    // Non-copyable, non-movable for stable addresses
    SecurityData(const SecurityData&) = delete;
    SecurityData& operator=(const SecurityData&) = delete;
    SecurityData(SecurityData&&) = delete;
    SecurityData& operator=(SecurityData&&) = delete;
  };
  
  /**
   * @brief Snapshot of security data for consistent reads
   */
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
    
    /**
     * @brief Get mid price (average of best bid and ask)
     */
    Price get_mid_price() const {
      if (best_bid == 0 || best_ask == 0) {
        return last_trade_price;
      }
      return (best_bid + best_ask) / 2;
    }
    
    /**
     * @brief Get spread in basis points
     */
    double get_spread_bps() const {
      if (best_bid == 0 || best_ask == 0) {
        return 0.0;
      }
      Price mid = get_mid_price();
      if (mid == 0) {
        return 0.0;
      }
      return (static_cast<double>(best_ask - best_bid) / static_cast<double>(mid)) * 10000.0;
    }
    
    /**
     * @brief Convert price to double (4 decimal places)
     */
    static double price_to_double(Price price) {
      return static_cast<double>(price) / 10000.0;
    }
  };

  SecurityStore() = default;
  ~SecurityStore() = default;
  
  // Non-copyable, non-movable
  SecurityStore(const SecurityStore&) = delete;
  SecurityStore& operator=(const SecurityStore&) = delete;
  SecurityStore(SecurityStore&&) = delete;
  SecurityStore& operator=(SecurityStore&&) = delete;
  
  /**
   * @brief Add a security to the store (lock-free)
   * @param security_id The security to add
   * @return true if added successfully, false if already exists or store full
   */
  bool add_security(const SecurityId& security_id) {
    // Check if already exists
    if (find_security_data(security_id) != nullptr) {
      return false; // Already exists
    }
    
    // Find an empty slot
    for (size_t i = 0; i < MAX_SECURITIES; ++i) {
      SecurityData& slot = securities_[i];
      
      // Try to claim an inactive slot
      bool expected = false;
      if (slot.active.compare_exchange_strong(expected, false, std::memory_order_acquire)) {
        // We claimed the slot, now initialize it
        slot.initialize(security_id);
        active_count_.fetch_add(1, std::memory_order_relaxed);
        return true;
      }
    }
    
    return false; // Store is full
  }
  
  /**
   * @brief Remove a security from the store (lock-free)
   * @param security_id The security to remove
   * @return true if removed successfully, false if not found
   */
  bool remove_security(const SecurityId& security_id) {
    SecurityData* data = find_security_data(security_id);
    if (!data) {
      return false; // Not found
    }
    
    data->deactivate();
    active_count_.fetch_sub(1, std::memory_order_relaxed);
    return true;
  }
  
  /**
   * @brief Update security data from L2 market data (single producer)
   * @param message The L2 market data message
   * @return true if updated successfully, false if security not found
   */
  bool update_from_l2(const MarketDataL2Message& message) {
    // Find the security (read-only access to map)
    SecurityData* data = find_security_data(message.security_id);
    if (!data) {
      return false;
    }
    
    // Update timestamp first
    data->last_update_ns.store(message.timestamp_ns, std::memory_order_release);
    
    // Update best bid/ask atomically
    if (message.num_bid_levels > 0) {
      data->best_bid.store(message.bids[0].price, std::memory_order_relaxed);
    }
    if (message.num_ask_levels > 0) {
      data->best_ask.store(message.asks[0].price, std::memory_order_relaxed);
    }
    
    // Update order book levels (single writer, no synchronization needed)
    update_order_book_side(data->bids, message.bids.data(), message.num_bid_levels);
    update_order_book_side(data->asks, message.asks.data(), message.num_ask_levels);
    
    // Update statistics
    data->update_count.fetch_add(1, std::memory_order_relaxed);
    
    return true;
  }
  
  /**
   * @brief Get a consistent snapshot of security data
   * @param security_id The security to get data for
   * @param snapshot Output snapshot
   * @return true if security found, false otherwise
   */
  bool get_security_snapshot(const SecurityId& security_id, SecuritySnapshot& snapshot) const {
    const SecurityData* data = find_security_data(security_id);
    if (!data) {
      return false;
    }
    
    // Take a consistent snapshot using acquire semantics
    snapshot.security_id = data->security_id;
    snapshot.last_update_ns = data->last_update_ns.load(std::memory_order_acquire);
    snapshot.best_bid = data->best_bid.load(std::memory_order_relaxed);
    snapshot.best_ask = data->best_ask.load(std::memory_order_relaxed);
    snapshot.last_trade_price = data->last_trade_price.load(std::memory_order_relaxed);
    snapshot.update_count = data->update_count.load(std::memory_order_relaxed);
    snapshot.total_volume = data->total_volume.load(std::memory_order_relaxed);
    
    // Copy order book data (single writer, safe to read)
    snapshot.num_bid_levels = data->bids.num_levels.load(std::memory_order_relaxed);
    snapshot.num_ask_levels = data->asks.num_levels.load(std::memory_order_relaxed);
    
    std::memcpy(snapshot.bids, data->bids.levels, sizeof(PriceLevel) * 5);
    std::memcpy(snapshot.asks, data->asks.levels, sizeof(PriceLevel) * 5);
    
    return true;
  }
  
  /**
   * @brief Get all securities currently in the store (lock-free)
   * @return Vector of security IDs
   */
  std::vector<SecurityId> get_all_securities() const {
    std::vector<SecurityId> result;
    result.reserve(active_count_.load(std::memory_order_relaxed));
    
    for (size_t i = 0; i < MAX_SECURITIES; ++i) {
      const SecurityData& slot = securities_[i];
      if (slot.active.load(std::memory_order_acquire)) {
        result.push_back(slot.security_id);
      }
    }
    
    return result;
  }
  
  /**
   * @brief Get the number of securities in the store (lock-free)
   */
  size_t size() const {
    return active_count_.load(std::memory_order_relaxed);
  }
  
  /**
   * @brief Check if a security exists in the store (lock-free)
   */
  bool contains(const SecurityId& security_id) const {
    return find_security_data(security_id) != nullptr;
  }
  
  /**
   * @brief Clear all securities from the store (lock-free)
   */
  void clear() {
    for (size_t i = 0; i < MAX_SECURITIES; ++i) {
      securities_[i].deactivate();
    }
    active_count_.store(0, std::memory_order_relaxed);
  }

private:
  /**
   * @brief Find security data (lock-free linear search)
   */
  SecurityData* find_security_data(const SecurityId& security_id) const {
    for (size_t i = 0; i < MAX_SECURITIES; ++i) {
      SecurityData& slot = const_cast<SecurityData&>(securities_[i]);
      if (slot.matches(security_id)) {
        return &slot;
      }
    }
    return nullptr;
  }
  
  /**
   * @brief Update order book side (single writer, no locks needed)
   */
  void update_order_book_side(SecurityData::OrderBookSide& side, 
                              const PriceLevel* levels, 
                              uint8_t num_levels) {
    // Copy the levels
    uint8_t copy_count = std::min(num_levels, static_cast<uint8_t>(5));
    std::memcpy(side.levels, levels, sizeof(PriceLevel) * copy_count);
    
    // Clear remaining levels
    for (uint8_t i = copy_count; i < 5; ++i) {
      side.levels[i] = PriceLevel{};
    }
    
    // Update count last (acts as a memory barrier for readers)
    side.num_levels.store(copy_count, std::memory_order_release);
  }
  
  // Fixed-size array of securities (no dynamic allocation)
  std::array<SecurityData, MAX_SECURITIES> securities_;
  
  // Atomic counter for active securities
  std::atomic<size_t> active_count_{0};
};

} // namespace mini_mart::market_data
