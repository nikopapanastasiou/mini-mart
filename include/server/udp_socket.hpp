#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace mini_mart::server {

enum class SocketError {
  SUCCESS = 0,
  SOCKET_CREATE_FAILED,
  SETSOCKOPT_FAILED,
  BIND_FAILED,
  GETADDRINFO_FAILED,
  INVALID_SOCKET
};

class UdpSocket {
public:
  UdpSocket();
  ~UdpSocket();
  UdpSocket(const UdpSocket &) = delete;
  UdpSocket &operator=(const UdpSocket &) = delete;
  UdpSocket(UdpSocket &&) noexcept;
  UdpSocket &operator=(UdpSocket &&) noexcept;

  int fd() const { return fd_; }
  bool is_valid() const { return fd_ >= 0 && error_ == SocketError::SUCCESS; }
  SocketError last_error() const { return error_; }
  explicit operator bool() const { return is_valid(); }

  bool set_send_buffer(int bytes);
  bool set_destination(const char *host, int port, sockaddr_in &out_dst);
  bool enable_reuseaddr();
  bool bind_any(int port);

private:
  int fd_{-1};
  SocketError error_{SocketError::SUCCESS};
};

} // namespace mini_mart::server