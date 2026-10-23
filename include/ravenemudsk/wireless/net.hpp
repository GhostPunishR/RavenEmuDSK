// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>

// Just enough sockets for the wireless link cable, on Windows (Winsock) and
// POSIX alike: TCP between two devices, UDP multicast for discovery (mDNS).
// Every wait has a timeout, so a thread can always notice it must stop.
namespace ravenemudsk_net {
#ifdef _WIN32
using Handle = std::uintptr_t;
#else
using Handle = int;
#endif

// An IPv4 address, in host order.
struct Ipv4 {
    std::uint32_t value = 0;
    std::string text() const;
    bool operator==(const Ipv4&) const = default;
};

class Socket {
public:
    Socket() = default;
    explicit Socket(Handle handle) : handle_(handle) {}
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;
    ~Socket() { close(); }
    bool valid() const;
    Handle handle() const { return handle_; }
    // Wakes a thread waiting on this socket; the handle stays open until close().
    void shutdown();
    void shutdown_send();
    void close();
private:
    Handle handle_ = invalid();
    static Handle invalid();
};

// Winsock is started once for the whole program; nothing to do elsewhere.
void startup();

// A TCP server on every interface, on a port the system picks.
Socket listen_tcp(std::uint16_t& port);
// The next incoming connection within `timeout_ms`, or an invalid socket.
Socket accept_tcp(const Socket& server, int timeout_ms);
Socket connect_tcp(Ipv4 address, std::uint16_t port, int timeout_ms);
// Small messages leave at once (TCP_NODELAY).
void tune_tcp(const Socket& socket);
bool send_all(const Socket& socket, std::span<const std::uint8_t> bytes);
// Bytes read (> 0), 0 at the end of the connection, -1 when nothing came
// within `timeout_ms`, -2 on an error.
int receive(const Socket& socket, std::span<std::uint8_t> buffer, int timeout_ms);

inline constexpr std::uint16_t mdns_port = 5353;
inline constexpr Ipv4 mdns_group{0xE00000FBu}; // 224.0.0.251
// UDP port 5353, shared with the system's own mDNS responder, joined to the
// mDNS group on `interface_address`'s interface.
Socket open_mdns(Ipv4 interface_address);
bool send_udp(const Socket& socket, std::span<const std::uint8_t> bytes, Ipv4 address, std::uint16_t port);
// As receive(), with the sender's address and port.
int receive_udp(const Socket& socket, std::span<std::uint8_t> buffer, int timeout_ms, Ipv4& from,
                std::uint16_t& port);
// The address of the interface that leads to the local network, if any.
std::optional<Ipv4> primary_address();
} // namespace ravenemudsk_net
