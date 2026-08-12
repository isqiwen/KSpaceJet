#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <sys/types.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <cerrno>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace ksj::platform {

#ifdef _WIN32
using SocketHandle = SOCKET;
using SocketLength = int;
inline constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
inline constexpr int kSocketError = SOCKET_ERROR;
#else
using SocketHandle = int;
using SocketLength = socklen_t;
inline constexpr SocketHandle kInvalidSocket = -1;
inline constexpr int kSocketError = -1;
#endif

struct Ipv4Endpoint {
  sockaddr_in native{};
};

[[nodiscard]] inline std::uint16_t socket_port(int port) noexcept {
  return static_cast<std::uint16_t>(port);
}

inline int close_socket(SocketHandle socket) noexcept {
#ifdef _WIN32
  return closesocket(socket);
#else
  return close(socket);
#endif
}

[[nodiscard]] inline int last_socket_error() noexcept {
#ifdef _WIN32
  return WSAGetLastError();
#else
  return errno;
#endif
}

[[nodiscard]] SocketHandle create_tcp_socket() noexcept;
[[nodiscard]] bool parse_ipv4_endpoint(std::string_view address, int port, Ipv4Endpoint* endpoint);
[[nodiscard]] Ipv4Endpoint any_ipv4_endpoint(int port) noexcept;
[[nodiscard]] std::string endpoint_address_string(const Ipv4Endpoint& endpoint);
[[nodiscard]] int endpoint_port(const Ipv4Endpoint& endpoint) noexcept;

[[nodiscard]] bool connect_socket(SocketHandle socket, const Ipv4Endpoint& endpoint) noexcept;
[[nodiscard]] bool bind_socket(SocketHandle socket, const Ipv4Endpoint& endpoint) noexcept;
[[nodiscard]] bool listen_socket(SocketHandle socket, int backlog) noexcept;
[[nodiscard]] SocketHandle accept_socket(SocketHandle socket, Ipv4Endpoint* peer_endpoint) noexcept;
[[nodiscard]] bool get_socket_name(SocketHandle socket, Ipv4Endpoint* endpoint) noexcept;

[[nodiscard]] bool set_socket_reuse_address(SocketHandle socket, bool enabled) noexcept;
[[nodiscard]] bool set_socket_keep_alive(SocketHandle socket, bool enabled) noexcept;
[[nodiscard]] bool set_socket_receive_buffer_size(SocketHandle socket, int bytes) noexcept;
[[nodiscard]] bool set_socket_send_buffer_size(SocketHandle socket, int bytes) noexcept;
[[nodiscard]] bool wait_socket_readable(SocketHandle socket, int timeout_ms) noexcept;
[[nodiscard]] int send_socket(SocketHandle socket, std::span<const std::byte> data) noexcept;
[[nodiscard]] int receive_socket(SocketHandle socket, std::span<std::byte> data) noexcept;
[[nodiscard]] int shutdown_socket(SocketHandle socket) noexcept;

} // namespace ksj::platform
