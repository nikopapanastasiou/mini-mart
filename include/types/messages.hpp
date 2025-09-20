#pragma once

#include "types/price.hpp"
#include <array>
#include <cstdint>

namespace mini_mart::types {

enum class MessageType : uint16_t {
  MARKET_DATA_L2 = 1,
};

enum class Side : uint8_t {
  BID = 0,
  ASK = 1,
};

// Quantity remains simple for now
using Quantity = uint64_t;

struct PriceLevel {
  Price price;
  Quantity quantity;
};

// Security identifier - 8 character symbol padded with nulls
using SecurityId = std::array<char, 8>;
static_assert(sizeof(PriceLevel) == 16, "PriceLevel size is not 16 bytes");

struct MessageHeader {
  uint32_t seq_no;
  uint16_t length;
  uint16_t type;
};
static_assert(sizeof(MessageHeader) == 8, "MessageHeader size is not 8 bytes");

struct HeartbeatMessage {
  MessageHeader header;
};
static_assert(sizeof(HeartbeatMessage) == 8,
              "HeartbeatMessage size is not 8 bytes");

// L2 Market Data Message - contains top 5 levels for both sides
struct MarketDataL2Message {
  MessageHeader header;
  SecurityId security_id;
  uint64_t timestamp_ns;          // nanoseconds since epoch
  std::array<PriceLevel, 5> bids; // sorted descending by price
  std::array<PriceLevel, 5> asks; // sorted ascending by price
  uint8_t num_bid_levels;
  uint8_t num_ask_levels;
  uint8_t padding[6]; // align to 8-byte boundary
};
static_assert(sizeof(MarketDataL2Message) == 192,
              "MarketDataL2Message size is not 192 bytes");

} // namespace mini_mart::types