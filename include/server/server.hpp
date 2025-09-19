#pragma once

#include "server/udp_socket.hpp"

#include <cstdint>
#include <netinet/in.h>

namespace minimart::server {

class Server {

public:
  Server(const char *host, int port, int hz);
  ~Server();

  void run(); // blocking loop; returns when stop_flag is set

private:
  mini_mart::server::UdpSocket sock_;
  sockaddr_in dst_{};
  int hz_;
  const bool *stop_flag_{nullptr};

  // non-copyable
  Server(const Server &) = delete;
  Server &operator=(const Server &) = delete;

  // helpers
  static std::uint64_t now_ns();
};

} // namespace minimart::server
