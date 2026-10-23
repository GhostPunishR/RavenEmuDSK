// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/wireless/protocol.hpp>
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace ravenemudsk_link {
namespace {
constexpr std::uint8_t kind_hello = 1, kind_pause = 2, kind_ping = 3, kind_bye = 4;
constexpr std::size_t max_text = 60;

// A name cut to 60 bytes without splitting a UTF-8 character.
std::string text(const std::string& value) {
    if (value.size() <= max_text) return value;
    std::size_t end = max_text;
    while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xC0) == 0x80) --end;
    return value.substr(0, end);
}
} // namespace

std::vector<std::uint8_t> frame(std::uint8_t channel, std::span<const std::uint8_t> payload) {
    if (channel != channel_core && channel != channel_control) throw std::invalid_argument("Canal inconnu");
    if (payload.empty() || payload.size() > max_payload) throw std::invalid_argument("Taille de trame invalide");
    std::vector<std::uint8_t> out{channel, static_cast<std::uint8_t>(payload.size())};
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<std::uint8_t> encode(const Control& message) {
    struct Encoder {
        std::vector<std::uint8_t> operator()(const Hello& h) const {
            const auto device = text(h.device), game = text(h.game);
            std::vector<std::uint8_t> out{kind_hello, static_cast<std::uint8_t>(h.version),
                                          static_cast<std::uint8_t>(h.family)};
            for (int shift = 0; shift < 64; shift += 8) out.push_back(static_cast<std::uint8_t>(h.id >> shift));
            out.push_back(h.paused ? 1 : 0);
            out.push_back(static_cast<std::uint8_t>(device.size()));
            out.insert(out.end(), device.begin(), device.end());
            out.push_back(static_cast<std::uint8_t>(game.size()));
            out.insert(out.end(), game.begin(), game.end());
            return out;
        }
        std::vector<std::uint8_t> operator()(const Pause& p) const { return {kind_pause, std::uint8_t(p.paused ? 1 : 0)}; }
        std::vector<std::uint8_t> operator()(const Ping&) const { return {kind_ping}; }
        std::vector<std::uint8_t> operator()(const Bye& b) const { return {kind_bye, std::uint8_t(b.refused ? 1 : 0)}; }
    };
    return std::visit(Encoder{}, message);
}

std::optional<Control> decode(std::span<const std::uint8_t> p) {
    if (p.empty()) return std::nullopt;
    switch (p[0]) {
    case kind_hello: {
        if (p.size() < 14) return std::nullopt;
        Hello h;
        h.version = p[1];
        h.family = p[2];
        for (int i = 0; i < 8; ++i) h.id |= std::uint64_t(p[3 + i]) << (8 * i);
        h.paused = p[11] != 0;
        std::size_t at = 12;
        const std::size_t device = p[at++];
        if (at + device >= p.size()) return std::nullopt;
        h.device.assign(reinterpret_cast<const char*>(p.data() + at), device);
        at += device;
        const std::size_t game = p[at++];
        if (at + game != p.size()) return std::nullopt;
        h.game.assign(reinterpret_cast<const char*>(p.data() + at), game);
        return h;
    }
    case kind_pause: return p.size() == 2 ? std::optional<Control>(Pause{p[1] != 0}) : std::nullopt;
    case kind_ping: return p.size() == 1 ? std::optional<Control>(Ping{}) : std::nullopt;
    case kind_bye: return p.size() == 2 ? std::optional<Control>(Bye{p[1] != 0}) : std::nullopt;
    default: return std::nullopt;
    }
}

void FrameReader::feed(std::span<const std::uint8_t> bytes) { buffer_.insert(buffer_.end(), bytes.begin(), bytes.end()); }

std::optional<Frame> FrameReader::next() {
    if (bad_ || buffer_.size() < 2) return std::nullopt;
    const auto channel = buffer_[0];
    const std::size_t size = buffer_[1];
    if ((channel != channel_core && channel != channel_control) || size == 0) {
        bad_ = true;
        return std::nullopt;
    }
    if (buffer_.size() < 2 + size) return std::nullopt;
    Frame f{channel, std::vector<std::uint8_t>(buffer_.begin() + 2, buffer_.begin() + 2 + std::ptrdiff_t(size))};
    buffer_.erase(buffer_.begin(), buffer_.begin() + 2 + std::ptrdiff_t(size));
    return f;
}

int player_for(std::uint64_t own, std::uint64_t peer) { return own < peer ? 0 : 1; }

std::string short_code(std::uint64_t id) {
    char out[8];
    std::snprintf(out, sizeof out, "%04X", static_cast<unsigned>((id >> 48) & 0xFFFF));
    return out;
}

namespace policy {
Decision decide(std::uint64_t own, const std::vector<Peer>& peers, const std::set<std::uint64_t>& avoided,
                std::optional<std::uint64_t> chosen) {
    std::vector<Peer> candidates;
    for (const auto& peer : peers)
        if (peer.id != own && !avoided.contains(peer.id)) candidates.push_back(peer);
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Peer& a, const Peer& b) { return a.device < b.device; });
    if (chosen) {
        for (const auto& peer : candidates)
            if (peer.id == *chosen) return Connect{peer};
        return Wait{};
    }
    if (candidates.empty()) return Wait{};
    if (candidates.size() == 1) {
        if (player_for(own, candidates.front().id) == 0) return Connect{candidates.front()};
        return Wait{};
    }
    return Choose{std::move(candidates)};
}

bool accepts(const Hello& hello, int family, const std::set<std::uint64_t>& declined) {
    return hello.version == protocol_version && hello.family == family && !declined.contains(hello.id);
}
} // namespace policy
} // namespace ravenemudsk_link
