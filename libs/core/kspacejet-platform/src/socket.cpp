#include "kspacejet/platform/socket.hpp"

#include <array>
#include <algorithm>
#include <cstring>

namespace ksj::platform {
namespace {

[[nodiscard]] sockaddr* socket_address(sockaddr_in& address) noexcept {
  return reinterpret_cast<sockaddr*>(&address);
}

[[nodiscard]] const sockaddr* socket_address(const sockaddr_in& address) noexcept {
  return reinterpret_cast<const sockaddr*>(&address);
}

[[nodiscard]] const char* socket_option_data(const int& value) noexcept {
  return reinterpret_cast<const char*>(&value);
}

[[nodiscard]] SocketLength endpoint_size() noexcept {
  return static_cast<SocketLength>(sizeof(sockaddr_in));
}

[[nodiscard]] bool set_socket_bool_option(SocketHandle socket, int option, bool enabled) noexcept {
  const int value = enabled ? 1 : 0;
  return setsockopt(socket, SOL_SOCKET, option, socket_option_data(value), static_cast<SocketLength>(sizeof(value))) !=
         kSocketError;
}

[[nodiscard]] bool set_socket_int_option(SocketHandle socket, int option, int value) noexcept {
  return setsockopt(socket, SOL_SOCKET, option, socket_option_data(value), static_cast<SocketLength>(sizeof(value))) !=
         kSocketError;
}

} // namespace

SocketHandle create_tcp_socket() noexcept {
  return socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

bool parse_ipv4_endpoint(std::string_view address, int port, Ipv4Endpoint* endpoint) {
  if (endpoint == nullptr) {
    return false;
  }

  auto parsed = Ipv4Endpoint{};
  parsed.native.sin_family = AF_INET;
  parsed.native.sin_port = htons(socket_port(port));

  const std::string address_text(address);
#ifdef _WIN32
  sockaddr_in parsed_address{};
  auto parsed_address_size = static_cast<int>(sizeof(parsed_address));
  parsed_address.sin_family = AF_INET;
  if (WSAStringToAddressA(const_cast<char*>(address_text.c_str()), AF_INET, nullptr, socket_address(parsed_address),
                          &parsed_address_size) != 0) {
    return false;
  }
  parsed.native.sin_addr = parsed_address.sin_addr;
#else
  if (inet_pton(AF_INET, address_text.c_str(), &parsed.native.sin_addr) != 1) {
    return false;
  }
#endif

  *endpoint = parsed;
  return true;
}

Ipv4Endpoint any_ipv4_endpoint(int port) noexcept {
  auto endpoint = Ipv4Endpoint{};
  endpoint.native.sin_family = AF_INET;
  endpoint.native.sin_addr.s_addr = htonl(INADDR_ANY);
  endpoint.native.sin_port = htons(socket_port(port));
  return endpoint;
}

std::string endpoint_address_string(const Ipv4Endpoint& endpoint) {
  std::array<char, INET_ADDRSTRLEN> buffer{};
#ifdef _WIN32
  if (InetNtopA(AF_INET, const_cast<in_addr*>(&endpoint.native.sin_addr), buffer.data(),
                static_cast<DWORD>(buffer.size())) == nullptr)
#else
  if (inet_ntop(AF_INET, &endpoint.native.sin_addr, buffer.data(), static_cast<socklen_t>(buffer.size())) == nullptr)
#endif
  {
    return {};
  }
  return std::string(buffer.data());
}

int endpoint_port(const Ipv4Endpoint& endpoint) noexcept {
  return static_cast<int>(ntohs(endpoint.native.sin_port));
}

bool connect_socket(SocketHandle socket, const Ipv4Endpoint& endpoint) noexcept {
  return connect(socket, socket_address(endpoint.native), endpoint_size()) != kSocketError;
}

bool bind_socket(SocketHandle socket, const Ipv4Endpoint& endpoint) noexcept {
  return bind(socket, socket_address(endpoint.native), endpoint_size()) != kSocketError;
}

bool listen_socket(SocketHandle socket, int backlog) noexcept {
  return listen(socket, backlog) != kSocketError;
}

SocketHandle accept_socket(SocketHandle socket, Ipv4Endpoint* peer_endpoint) noexcept {
  auto accepted_endpoint = Ipv4Endpoint{};
  auto accepted_endpoint_size = endpoint_size();
  const SocketHandle accepted_socket =
    accept(socket, socket_address(accepted_endpoint.native), &accepted_endpoint_size);
  if (accepted_socket != kInvalidSocket && peer_endpoint != nullptr) {
    *peer_endpoint = accepted_endpoint;
  }
  return accepted_socket;
}

bool get_socket_name(SocketHandle socket, Ipv4Endpoint* endpoint) noexcept {
  if (endpoint == nullptr) {
    return false;
  }

  auto socket_endpoint_size = endpoint_size();
  return getsockname(socket, socket_address(endpoint->native), &socket_endpoint_size) != kSocketError;
}

bool set_socket_reuse_address(SocketHandle socket, bool enabled) noexcept {
  return set_socket_bool_option(socket, SO_REUSEADDR, enabled);
}

bool set_socket_keep_alive(SocketHandle socket, bool enabled) noexcept {
  return set_socket_bool_option(socket, SO_KEEPALIVE, enabled);
}

bool set_socket_receive_buffer_size(SocketHandle socket, int bytes) noexcept {
  return set_socket_int_option(socket, SO_RCVBUF, bytes);
}

bool set_socket_send_buffer_size(SocketHandle socket, int bytes) noexcept {
  return set_socket_int_option(socket, SO_SNDBUF, bytes);
}

bool wait_socket_readable(SocketHandle socket, int timeout_ms) noexcept {
  fd_set read_set;
  FD_ZERO(&read_set);
  FD_SET(socket, &read_set);

  timeval timeout{};
  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;

  const int ready = select(static_cast<int>(socket + 1), &read_set, nullptr, nullptr, &timeout);
  return ready > 0 && FD_ISSET(socket, &read_set);
}

int send_socket(SocketHandle socket, std::span<const std::byte> data) noexcept {
  if (data.empty()) {
    return 0;
  }

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
  return static_cast<int>(send(socket, reinterpret_cast<const char*>(data.data()), data.size(), MSG_NOSIGNAL));
}

int receive_socket(SocketHandle socket, std::span<std::byte> data) noexcept {
  if (data.empty()) {
    return 0;
  }
  return static_cast<int>(recv(socket, reinterpret_cast<char*>(data.data()), data.size(), 0));
}

int shutdown_socket(SocketHandle socket) noexcept {
#ifdef _WIN32
  return shutdown(socket, SD_BOTH);
#else
  return shutdown(socket, SHUT_RDWR);
#endif
}

} // namespace ksj::platform
