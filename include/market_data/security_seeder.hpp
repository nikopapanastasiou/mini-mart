#pragma once

#include "types/messages.hpp"
#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace mini_mart::market_data {

using namespace mini_mart::types;

/**
 * @brief Utility class for seeding securities at startup
 *
 * This class provides predefined sets of securities commonly used
 * in trading systems, along with utilities to create custom security lists.
 */
class SecuritySeeder {
public:
  struct EquityInfo {
    std::string symbol;
    std::string name;
    double base_price;
  };

  /**
   * @brief Get centralized equity information
   * @return Map of symbol to equity information
   */
  static const std::unordered_map<std::string, EquityInfo> &get_equity_info() {
    static const std::unordered_map<std::string, EquityInfo> equity_data = {
        {"AAPL", {"AAPL", "Apple Inc.", 175.0}},
        {"MSFT", {"MSFT", "Microsoft Corporation", 350.0}},
        {"GOOGL", {"GOOGL", "Alphabet Inc.", 2800.0}},
        {"AMZN", {"AMZN", "Amazon.com Inc.", 3200.0}},
        {"TSLA", {"TSLA", "Tesla Inc.", 250.0}},
        {"META", {"META", "Meta Platforms Inc.", 320.0}},
        {"NVDA", {"NVDA", "NVIDIA Corporation", 450.0}},
        {"JPM", {"JPM", "JPMorgan Chase & Co.", 145.0}},
        {"JNJ", {"JNJ", "Johnson & Johnson", 165.0}},
        {"V", {"V", "Visa Inc.", 240.0}},
        {"PG", {"PG", "Procter & Gamble Co.", 140.0}},
        {"UNH", {"UNH", "UnitedHealth Group Inc.", 520.0}},
        {"HD", {"HD", "Home Depot Inc.", 330.0}},
        {"MA", {"MA", "Mastercard Inc.", 380.0}},
        {"BAC", {"BAC", "Bank of America Corp.", 32.0}},
        {"XOM", {"XOM", "Exxon Mobil Corporation", 110.0}},
        {"DIS", {"DIS", "Walt Disney Co.", 95.0}},
        {"ADBE", {"ADBE", "Adobe Inc.", 480.0}},
        {"CRM", {"CRM", "Salesforce Inc.", 220.0}},
        {"NFLX", {"NFLX", "Netflix Inc.", 450.0}}};
    return equity_data;
  }

  /**
   * @brief Get base price for a security symbol
   * @param symbol The security symbol
   * @param default_price Default price if symbol not found
   * @return Base price for the security
   */
  static double get_base_price(const std::string &symbol,
                               double default_price = 150.0) {
    const auto &equity_info = get_equity_info();
    auto it = equity_info.find(symbol);
    return (it != equity_info.end()) ? it->second.base_price : default_price;
  }

  /**
   * @brief Get a predefined set of major US equities
   * @return Vector of SecurityId for major US stocks
   */
  static std::vector<SecurityId> get_major_us_equities() {
    const auto &equity_info = get_equity_info();
    std::vector<SecurityId> result;
    result.reserve(equity_info.size());

    for (const auto &[symbol, info] : equity_info) {
      result.push_back(create_security_id(symbol));
    }

    return result;
  }

  /**
   * @brief Get a predefined set of major currency pairs
   * @return Vector of SecurityId for major FX pairs
   */
  static std::vector<SecurityId> get_major_fx_pairs() {
    std::vector<std::string> symbols = {
        "EURUSD", // Euro / US Dollar
        "GBPUSD", // British Pound / US Dollar
        "USDJPY", // US Dollar / Japanese Yen
        "USDCHF", // US Dollar / Swiss Franc
        "AUDUSD", // Australian Dollar / US Dollar
        "USDCAD", // US Dollar / Canadian Dollar
        "NZDUSD", // New Zealand Dollar / US Dollar
        "EURGBP", // Euro / British Pound
        "EURJPY", // Euro / Japanese Yen
        "GBPJPY", // British Pound / Japanese Yen
        "CHFJPY", // Swiss Franc / Japanese Yen
        "EURCHF", // Euro / Swiss Franc
        "AUDCAD", // Australian Dollar / Canadian Dollar
        "CADJPY", // Canadian Dollar / Japanese Yen
        "NZDJPY"  // New Zealand Dollar / Japanese Yen
    };

    std::vector<SecurityId> result;
    result.reserve(symbols.size());

    for (const auto &symbol : symbols) {
      result.push_back(create_security_id(symbol));
    }

    return result;
  }

  /**
   * @brief Get a predefined set of major cryptocurrency pairs
   * @return Vector of SecurityId for major crypto pairs
   */
  static std::vector<SecurityId> get_major_crypto_pairs() {
    std::vector<std::string> symbols = {
        "BTCUSD",   // Bitcoin / US Dollar
        "ETHUSD",   // Ethereum / US Dollar
        "ADAUSD",   // Cardano / US Dollar
        "BNBUSD",   // Binance Coin / US Dollar
        "XRPUSD",   // Ripple / US Dollar
        "SOLUSD",   // Solana / US Dollar
        "DOTUSD",   // Polkadot / US Dollar
        "AVAXUSD",  // Avalanche / US Dollar
        "MATICUSD", // Polygon / US Dollar
        "LINKUSD",  // Chainlink / US Dollar
        "LTCUSD",   // Litecoin / US Dollar
        "BCHUSD",   // Bitcoin Cash / US Dollar
        "XLMUSD",   // Stellar / US Dollar
        "VETUSD",   // VeChain / US Dollar
        "FILUSD"    // Filecoin / US Dollar
    };

    std::vector<SecurityId> result;
    result.reserve(symbols.size());

    for (const auto &symbol : symbols) {
      result.push_back(create_security_id(symbol));
    }

    return result;
  }

  /**
   * @brief Get a comprehensive set of securities for testing (equities only)
   * @return Vector of SecurityId for major US equities suitable for testing
   */
  static std::vector<SecurityId> get_test_securities() {
    auto equities = get_major_us_equities();

    // Return first 10 equities for testing
    std::vector<SecurityId> result;
    size_t count = std::min(10ul, equities.size());
    result.insert(result.end(), equities.begin(), equities.begin() + count);

    return result;
  }

  /**
   * @brief Create a SecurityId from a string symbol
   * @param symbol The symbol string (will be truncated/padded to 8 chars)
   * @return SecurityId with the symbol
   */
  static SecurityId create_security_id(const std::string &symbol) {
    SecurityId security_id{};
    security_id.fill('\0'); // Initialize with null characters

    size_t copy_length = std::min(symbol.length(), MAX_SYMBOL_LENGTH);
    std::memcpy(security_id.data(), symbol.c_str(), copy_length);

    return security_id;
  }

  /**
   * @brief Convert a SecurityId back to a string
   * @param security_id The SecurityId to convert
   * @return String representation of the security ID
   */
  static std::string security_id_to_string(const SecurityId &security_id) {
    // Find the first null character or use the full length
    size_t length = 0;
    for (size_t i = 0; i < security_id.size(); ++i) {
      if (security_id[i] == '\0') {
        break;
      }
      length = i + 1;
    }

    return std::string(security_id.data(), length);
  }

private:
  static constexpr size_t MAX_SYMBOL_LENGTH = 8;
};

} // namespace mini_mart::market_data
