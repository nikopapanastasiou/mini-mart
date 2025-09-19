#pragma once

#include "types/messages.hpp"
#include <functional>
#include <memory>
#include <vector>

namespace mini_mart::market_data {

using namespace mini_mart::types;

/**
 * @brief Callback function type for market data updates
 * @param message The L2 market data message
 */
using MarketDataCallback = std::function<void(const MarketDataL2Message &)>;

/**
 * @brief Abstract interface for market data providers
 *
 * This interface defines the contract for all market data providers,
 * whether they source data from exchanges, simulators, or other feeds.
 */
class MarketDataProvider {
public:
  virtual ~MarketDataProvider() = default;

  /**
   * @brief Start the market data feed
   * @return true if started successfully, false otherwise
   */
  virtual bool start() = 0;

  /**
   * @brief Stop the market data feed
   */
  virtual void stop() = 0;

  /**
   * @brief Check if the provider is currently running
   * @return true if running, false otherwise
   */
  virtual bool is_running() const = 0;

  /**
   * @brief Subscribe to market data for a specific security
   * @param security_id The security to subscribe to
   * @return true if subscription successful, false otherwise
   */
  virtual bool subscribe(const SecurityId &security_id) = 0;

  /**
   * @brief Unsubscribe from market data for a specific security
   * @param security_id The security to unsubscribe from
   * @return true if unsubscription successful, false otherwise
   */
  virtual bool unsubscribe(const SecurityId &security_id) = 0;

  /**
   * @brief Set the callback function for market data updates
   * @param callback The callback function to be called on each update
   */
  virtual void set_callback(MarketDataCallback callback) = 0;

  /**
   * @brief Get the list of currently subscribed securities
   * @return Vector of subscribed security IDs
   */
  virtual std::vector<SecurityId> get_subscribed_securities() const = 0;
};

/**
 * @brief Factory function type for creating market data providers
 */
using MarketDataProviderFactory =
    std::function<std::unique_ptr<MarketDataProvider>()>;

} // namespace mini_mart::market_data
