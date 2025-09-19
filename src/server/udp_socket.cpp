#include "server/udp_socket.hpp"
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include <netdb.h>

namespace mini_mart::server {

UdpSocket::UdpSocket() {
  fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd_ < 0) {
    throw std::runtime_error(std::string("socket() failed: ") + std::strerror(errno));
  }
}

UdpSocket::~UdpSocket() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

UdpSocket::UdpSocket(UdpSocket &&other) noexcept {
  fd_ = other.fd_;
  other.fd_ = -1;
}

UdpSocket &UdpSocket::operator=(UdpSocket &&other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) {
      ::close(fd_);
    }
    fd_ = other.fd_;
    other.fd_ = -1;
  }
  return *this;
}

void UdpSocket::set_send_buffer(int bytes) {
  if (fd_ < 0) {
    throw std::runtime_error("set_send_buffer on invalid socket");
  }
  if (::setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof(bytes)) < 0) {
    throw std::runtime_error(std::string("setsockopt(SO_SNDBUF) failed: ") + std::strerror(errno));
  }
}

void UdpSocket::set_destination(const char *host, int port, sockaddr_in &out_dst) {
  if (fd_ < 0) {
    throw std::runtime_error("set_destination on invalid socket");
  }
  std::memset(&out_dst, 0, sizeof(out_dst));
  out_dst.sin_family = AF_INET;
  out_dst.sin_port = htons(static_cast<uint16_t>(port));

  // Try parsing as a numeric IPv4 address first.
  in_addr addr{};
  int rc = ::inet_pton(AF_INET, host, &addr);
  if (rc == 1) {
    out_dst.sin_addr = addr;
    return;
  }
  // Fallback to DNS resolution (IPv4 only)
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo *res = nullptr;
  rc = ::getaddrinfo(host, nullptr, &hints, &res);
  if (rc != 0 || res == nullptr) {
    throw std::runtime_error(std::string("getaddrinfo failed for host '") + host + "': " + (rc != 0 ? ::gai_strerror(rc) : "unknown error"));
  }
  // Use the first result
  auto *sin = reinterpret_cast<sockaddr_in *>(res->ai_addr);
  out_dst.sin_addr = sin->sin_addr;
  ::freeaddrinfo(res);
}

void UdpSocket::enable_reuseaddr() {
  if (fd_ < 0) {
    throw std::runtime_error("enable_reuseaddr on invalid socket");
  }
  int opt = 1;
  if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    throw std::runtime_error(std::string("setsockopt(SO_REUSEADDR) failed: ") + std::strerror(errno));
  }
}

void UdpSocket::bind_any(int port) {
  if (fd_ < 0) {
    throw std::runtime_error("bind_any on invalid socket");
  }
  sockaddr_in addr{};
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (::bind(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    throw std::runtime_error(std::string("bind() failed: ") + std::strerror(errno));
  }
}

} // namespace mini_mart::server