#include "server/udp_socket.hpp"
#include <cerrno>
#include <cstring>
#include <netdb.h>

namespace mini_mart::server {

UdpSocket::UdpSocket() {
  fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd_ < 0) {
    error_ = SocketError::SOCKET_CREATE_FAILED;
  }
}

UdpSocket::~UdpSocket() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

UdpSocket::UdpSocket(UdpSocket &&other) noexcept : fd_(other.fd_), error_(other.error_) {
  other.fd_ = -1;
  other.error_ = SocketError::SUCCESS;
}

UdpSocket &UdpSocket::operator=(UdpSocket &&other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) {
      ::close(fd_);
    }
    fd_ = other.fd_;
    error_ = other.error_;
    other.fd_ = -1;
    other.error_ = SocketError::SUCCESS;
  }
  return *this;
}

bool UdpSocket::set_send_buffer(int bytes) {
  if (fd_ < 0) {
    error_ = SocketError::INVALID_SOCKET;
    return false;
  }
  if (::setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof(bytes)) < 0) {
    error_ = SocketError::SETSOCKOPT_FAILED;
    return false;
  }
  return true;
}

bool UdpSocket::set_destination(const char *host, int port, sockaddr_in &out_dst) {
  if (fd_ < 0) {
    error_ = SocketError::INVALID_SOCKET;
    return false;
  }
  std::memset(&out_dst, 0, sizeof(out_dst));
  out_dst.sin_family = AF_INET;
  out_dst.sin_port = htons(static_cast<uint16_t>(port));

  int rc = ::inet_pton(AF_INET, host, &out_dst.sin_addr);
  if (rc == 1) {
    return true;
  }
  if (rc < 0) {
    error_ = SocketError::GETADDRINFO_FAILED;
    return false;
  }

  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo *res = nullptr;
  rc = ::getaddrinfo(host, nullptr, &hints, &res);
  if (rc != 0 || res == nullptr) {
    error_ = SocketError::GETADDRINFO_FAILED;
    return false;
  }
  auto *sin = reinterpret_cast<sockaddr_in *>(res->ai_addr);
  out_dst.sin_addr = sin->sin_addr;
  ::freeaddrinfo(res);
  return true;
}

bool UdpSocket::enable_reuseaddr() {
  if (fd_ < 0) {
    error_ = SocketError::INVALID_SOCKET;
    return false;
  }
  int opt = 1;
  if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    error_ = SocketError::SETSOCKOPT_FAILED;
    return false;
  }
  return true;
}

bool UdpSocket::bind_any(int port) {
  if (fd_ < 0) {
    error_ = SocketError::INVALID_SOCKET;
    return false;
  }
  sockaddr_in addr{};
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (::bind(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    error_ = SocketError::BIND_FAILED;
    return false;
  }
  return true;
}

} // namespace mini_mart::server