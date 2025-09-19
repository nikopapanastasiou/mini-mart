#pragma once

#include <cstdint>

namespace mini_mart::types {

enum MessageType : uint16_t {
    HEARTBEAT = 0,
};

struct MessageHeader {
    uint32_t seq_no;
    uint16_t length;
    uint16_t type;
};
static_assert(sizeof(MessageHeader) == 8, "MessageHeader size is not 8 bytes");

struct HeartbeatMessage {
    MessageHeader header;
};
static_assert(sizeof(HeartbeatMessage) == 8, "HeartbeatMessage size is not 8 bytes");

}