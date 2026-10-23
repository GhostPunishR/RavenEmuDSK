// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <variant>
#include <vector>

// What two devices exchange over the wireless link cable: byte for byte the
// protocol of RavenEmu Android (WirelessLinkProtocol.kt), so a computer links
// with a phone as well as with another computer.
//
// A TCP connection carries frames [channel u8][size u8][payload]. Channel 0
// carries RavenCore's messages untouched; channel 1 the application's own:
// introduction, pause, heartbeat and goodbye.
namespace ravenemudsk_link {

inline constexpr int protocol_version = 1;
inline constexpr std::uint8_t channel_core = 0;
inline constexpr std::uint8_t channel_control = 1;
inline constexpr std::size_t max_payload = 255;
// DNS-SD service type announced on the local network.
inline constexpr const char* service_type = "_ravenemulink._tcp";
// Families of consoles that link, RavenCore's.
inline constexpr int family_gb = 1;
inline constexpr int family_gba = 2;

// First frame of each side: who speaks, with which console.
struct Hello {
    int version = protocol_version;
    int family = 0;
    std::uint64_t id = 0;
    bool paused = false;
    std::string device;
    std::string game;
    bool operator==(const Hello&) const = default;
};
// The game menu (or an interruption) stops this console, or it starts again.
struct Pause {
    bool paused = false;
    bool operator==(const Pause&) const = default;
};
// Nothing else to say: proves the connection is alive.
struct Ping {
    bool operator==(const Ping&) const = default;
};
// Leaving on purpose. `refused`: this device does not want that partner.
struct Bye {
    bool refused = false;
    bool operator==(const Bye&) const = default;
};
using Control = std::variant<Hello, Pause, Ping, Bye>;

struct Frame {
    std::uint8_t channel = 0;
    std::vector<std::uint8_t> payload;
};

// Throws std::invalid_argument for an unknown channel or a payload of 0 or
// more than 255 bytes.
std::vector<std::uint8_t> frame(std::uint8_t channel, std::span<const std::uint8_t> payload);
std::vector<std::uint8_t> encode(const Control& message);
// Empty for an unknown or malformed message.
std::optional<Control> decode(std::span<const std::uint8_t> payload);

// Frames out of a TCP stream, whatever the pieces it arrives in.
class FrameReader {
public:
    void feed(std::span<const std::uint8_t> bytes);
    // The next whole frame, if any. An invalid one makes bad() true.
    std::optional<Frame> next();
    bool bad() const { return bad_; }
private:
    std::vector<std::uint8_t> buffer_;
    bool bad_ = false;
};

// Player 0, the GBA parent: the device with the smaller identifier, compared
// unsigned, as Android does.
int player_for(std::uint64_t own, std::uint64_t peer);
// Four hexadecimal digits that tell two devices of the same kind apart.
std::string short_code(std::uint64_t id);

// Whom to link, from the devices seen on the network: a pure decision, taken
// again at each change. A single possible partner links by itself; several
// ask the player.
namespace policy {
struct Peer {
    std::uint64_t id = 0;
    std::string device;
    bool operator==(const Peer&) const = default;
};
struct Wait {};       // Nothing to do: no partner, or the other one calls.
struct Connect { Peer peer; };
struct Choose { std::vector<Peer> peers; };
using Decision = std::variant<Wait, Connect, Choose>;

// `chosen`: the partner the player picked. `avoided`: unplugged by the player,
// or who just left. Only the smaller identifier calls by itself, so that a
// single connection is born.
Decision decide(std::uint64_t own, const std::vector<Peer>& peers, const std::set<std::uint64_t>& avoided,
                std::optional<std::uint64_t> chosen);
// An incoming call: accepted from the same protocol and family, unless declined.
bool accepts(const Hello& hello, int family, const std::set<std::uint64_t>& declined);
} // namespace policy
} // namespace ravenemudsk_link
