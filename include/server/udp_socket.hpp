#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace mini_mart::server {

class UdpSocket {
public:
  UdpSocket();
  ~UdpSocket();
  UdpSocket(const UdpSocket &) = delete;
  UdpSocket &operator=(const UdpSocket &) = delete;
  UdpSocket(UdpSocket &&) noexcept;
  UdpSocket &operator=(UdpSocket &&) noexcept;

  int fd() const { return fd_; }
  explicit operator bool() const { return fd_ >= 0; }

  // Sender setup
  void set_send_buffer(int bytes);
  void set_destination(const char *host, int port, sockaddr_in &out_dst);

  // Receiver setup
  void enable_reuseaddr();
  void bind_any(int port);

private:
  int fd_{-1};
};

} // namespace mini_mart::server