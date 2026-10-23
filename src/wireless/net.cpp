// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/wireless/net.hpp>
#include <mutex>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ravenemudsk_net {
namespace {
#ifdef _WIN32
using Native = SOCKET;
constexpr Native none = INVALID_SOCKET;
int last_error() { return WSAGetLastError(); }
bool would_block(int error) { return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS; }
void set_blocking(Native s, bool blocking) {
    u_long mode = blocking ? 0 : 1;
    ioctlsocket(s, FIONBIO, &mode);
}
constexpr int send_flags = 0;
#else
using Native = int;
constexpr Native none = -1;
int last_error() { return errno; }
bool would_block(int error) { return error == EINPROGRESS || error == EWOULDBLOCK || error == EAGAIN; }
void set_blocking(Native s, bool blocking) {
    const int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK));
}
#ifdef MSG_NOSIGNAL
constexpr int send_flags = MSG_NOSIGNAL;
#else
constexpr int send_flags = 0;
#endif
#endif

Native native(Handle h) { return static_cast<Native>(h); }
Handle wrap(Native s) { return static_cast<Handle>(s); }

// A closed peer must not kill the program (macOS has no MSG_NOSIGNAL).
void no_sigpipe([[maybe_unused]] Native s) {
#ifdef SO_NOSIGPIPE
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
}

sockaddr_in address_of(Ipv4 address, std::uint16_t port) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(address.value);
    return a;
}

// 1 readable (or writable), 0 timeout, -1 error. Windows reports a failed
// connection among the exceptions, not as writable.
int wait(Native s, int timeout_ms, bool write) {
    fd_set set, failed;
    FD_ZERO(&set);
    FD_ZERO(&failed);
    FD_SET(s, &set);
    FD_SET(s, &failed);
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    const int r = select(static_cast<int>(s) + 1, write ? nullptr : &set, write ? &set : nullptr,
                         write ? &failed : nullptr, &tv);
    return r < 0 ? -1 : r > 0 ? 1 : 0;
}
} // namespace

std::string Ipv4::text() const {
    return std::to_string((value >> 24) & 255) + "." + std::to_string((value >> 16) & 255) + "." +
           std::to_string((value >> 8) & 255) + "." + std::to_string(value & 255);
}

Handle Socket::invalid() { return wrap(none); }
bool Socket::valid() const { return native(handle_) != none; }
Socket::Socket(Socket&& other) noexcept : handle_(other.handle_) { other.handle_ = invalid(); }
Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        other.handle_ = invalid();
    }
    return *this;
}
void Socket::shutdown() {
#ifdef _WIN32
    if (valid()) ::shutdown(native(handle_), SD_BOTH);
#else
    if (valid()) ::shutdown(native(handle_), SHUT_RDWR);
#endif
}
void Socket::shutdown_send() {
#ifdef _WIN32
    if (valid()) ::shutdown(native(handle_), SD_SEND);
#else
    if (valid()) ::shutdown(native(handle_), SHUT_WR);
#endif
}
void Socket::close() {
    if (!valid()) return;
#ifdef _WIN32
    closesocket(native(handle_));
#else
    ::close(native(handle_));
#endif
    handle_ = invalid();
}

void startup() {
#ifdef _WIN32
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    });
#endif
}

Socket listen_tcp(std::uint16_t& port) {
    startup();
    Socket server(wrap(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)));
    if (!server.valid()) return {};
    auto a = address_of({0}, 0);
    if (::bind(native(server.handle()), reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) return {};
    if (::listen(native(server.handle()), 4) != 0) return {};
    socklen_t size = sizeof a;
    if (getsockname(native(server.handle()), reinterpret_cast<sockaddr*>(&a), &size) != 0) return {};
    port = ntohs(a.sin_port);
    return server;
}

Socket accept_tcp(const Socket& server, int timeout_ms) {
    if (!server.valid() || wait(native(server.handle()), timeout_ms, false) <= 0) return {};
    Socket client(wrap(::accept(native(server.handle()), nullptr, nullptr)));
    if (client.valid()) no_sigpipe(native(client.handle()));
    return client;
}

Socket connect_tcp(Ipv4 address, std::uint16_t port, int timeout_ms) {
    startup();
    Socket s(wrap(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)));
    if (!s.valid()) return {};
    const auto a = address_of(address, port);
    const Native n = native(s.handle());
    set_blocking(n, false);
    if (::connect(n, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0) {
        if (!would_block(last_error()) || wait(n, timeout_ms, true) <= 0) return {};
        int error = 0;
        socklen_t size = sizeof error;
        if (getsockopt(n, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size) != 0 || error != 0) return {};
    }
    set_blocking(n, true);
    no_sigpipe(n);
    return s;
}

void tune_tcp(const Socket& socket) {
    int on = 1;
    setsockopt(native(socket.handle()), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof on);
}

bool send_all(const Socket& socket, std::span<const std::uint8_t> bytes) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const auto n = ::send(native(socket.handle()), reinterpret_cast<const char*>(bytes.data() + sent),
                              static_cast<int>(bytes.size() - sent), send_flags);
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

int receive(const Socket& socket, std::span<std::uint8_t> buffer, int timeout_ms) {
    if (!socket.valid()) return -2;
    const int ready = wait(native(socket.handle()), timeout_ms, false);
    if (ready < 0) return -2;
    if (ready == 0) return -1;
    const auto n = ::recv(native(socket.handle()), reinterpret_cast<char*>(buffer.data()),
                          static_cast<int>(buffer.size()), 0);
    return n < 0 ? -2 : static_cast<int>(n);
}

Socket open_mdns(Ipv4 interface_address) {
    startup();
    Socket s(wrap(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)));
    if (!s.valid()) return {};
    const Native n = native(s.handle());
    int on = 1;
    // Shared with the system's responder (mDNSResponder, Avahi, Windows).
    setsockopt(n, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof on);
#ifdef SO_REUSEPORT
    setsockopt(n, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&on), sizeof on);
#endif
    auto a = address_of({0}, mdns_port);
    if (::bind(n, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) return {};
    ip_mreq group{};
    group.imr_multiaddr.s_addr = htonl(mdns_group.value);
    group.imr_interface.s_addr = htonl(interface_address.value);
    if (setsockopt(n, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&group), sizeof group) != 0)
        return {};
    in_addr out{};
    out.s_addr = htonl(interface_address.value);
    setsockopt(n, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&out), sizeof out);
#ifdef _WIN32
    DWORD ttl = 255, loop = 1;
#else
    unsigned char ttl = 255, loop = 1;
#endif
    setsockopt(n, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof ttl);
    setsockopt(n, IPPROTO_IP, IP_MULTICAST_LOOP, reinterpret_cast<const char*>(&loop), sizeof loop);
    return s;
}

bool send_udp(const Socket& socket, std::span<const std::uint8_t> bytes, Ipv4 address, std::uint16_t port) {
    const auto a = address_of(address, port);
    return ::sendto(native(socket.handle()), reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()),
                    0, reinterpret_cast<const sockaddr*>(&a), sizeof a) == static_cast<int>(bytes.size());
}

int receive_udp(const Socket& socket, std::span<std::uint8_t> buffer, int timeout_ms, Ipv4& from,
                std::uint16_t& port) {
    if (!socket.valid()) return -2;
    const int ready = wait(native(socket.handle()), timeout_ms, false);
    if (ready < 0) return -2;
    if (ready == 0) return -1;
    sockaddr_in a{};
    socklen_t size = sizeof a;
    const auto n = ::recvfrom(native(socket.handle()), reinterpret_cast<char*>(buffer.data()),
                              static_cast<int>(buffer.size()), 0, reinterpret_cast<sockaddr*>(&a), &size);
    if (n < 0) return -2;
    from.value = ntohl(a.sin_addr.s_addr);
    port = ntohs(a.sin_port);
    return static_cast<int>(n);
}

std::optional<Ipv4> primary_address() {
    startup();
    // The interface the system would send through; no packet leaves.
    for (const Ipv4 target : {mdns_group, Ipv4{0x08080808u}}) {
        Socket s(wrap(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)));
        if (!s.valid()) continue;
        auto a = address_of(target, 53);
        if (::connect(native(s.handle()), reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) continue;
        socklen_t size = sizeof a;
        if (getsockname(native(s.handle()), reinterpret_cast<sockaddr*>(&a), &size) != 0) continue;
        const Ipv4 local{ntohl(a.sin_addr.s_addr)};
        if (local.value != 0 && (local.value >> 24) != 127) return local;
    }
    return std::nullopt;
}
} // namespace ravenemudsk_net
