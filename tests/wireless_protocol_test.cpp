// SPDX-License-Identifier: AGPL-3.0-only
// The wireless link protocol, byte for byte as RavenEmu Android writes it
// (WirelessLinkProtocolTest.kt checks the same cases there).
#include <ravenemudsk/wireless/protocol.hpp>
#include <iostream>
#include <stdexcept>
using namespace ravenemudsk_link;
static void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }

static std::vector<std::uint8_t> bytes(std::string_view text) { return {text.begin(), text.end()}; }

static std::string repeat(std::string_view piece, int count) {
    std::string out;
    for (int i = 0; i < count; ++i) out += piece;
    return out;
}

int main() try {
    // Frames: channel, size, payload, whatever the pieces they arrive in.
    const std::vector<std::uint8_t> core{3, 1, 0, 0, 0, 0x55, 0, 0, 0};
    auto stream = frame(channel_core, core);
    require(stream.size() == 11 && stream[0] == 0 && stream[1] == 9, "Frame header");
    const auto ping = frame(channel_control, encode(Ping{}));
    require(ping == std::vector<std::uint8_t>{1, 1, 3}, "Ping frame");
    stream.insert(stream.end(), ping.begin(), ping.end());
    FrameReader reader;
    for (const auto byte : stream) reader.feed({&byte, 1});
    const auto first = reader.next();
    require(first && first->channel == channel_core && first->payload == core, "Core frame read back");
    const auto second = reader.next();
    require(second && second->channel == channel_control && decode(second->payload) == Control{Ping{}},
            "Control frame read back");
    require(!reader.next() && !reader.bad(), "Nothing more");
    // Cut in the middle: it waits for the rest.
    FrameReader partial;
    const std::vector<std::uint8_t> half{0, 4, 1, 2};
    partial.feed(half);
    require(!partial.next() && !partial.bad(), "Half a frame waits");
    for (const auto& bad : {std::vector<std::uint8_t>{2, 1, 0}, std::vector<std::uint8_t>{0, 0}}) {
        FrameReader invalid;
        invalid.feed(bad);
        require(!invalid.next() && invalid.bad(), "Invalid frame refused");
    }
    for (const std::size_t size : {std::size_t{0}, std::size_t{256}}) {
        bool thrown = false;
        try {
            frame(channel_core, std::vector<std::uint8_t>(size));
        } catch (const std::invalid_argument&) {
            thrown = true;
        }
        require(thrown, "Payload size refused");
    }

    // The introduction, as Android encodes it: id little-endian, then the texts.
    const Hello hello{1, family_gba, 0xEDCBA98765432110ull, true, "Pixel 7", "Pokémon Émeraude"};
    std::vector<std::uint8_t> expected{1, 1, 2, 0x10, 0x21, 0x43, 0x65, 0x87, 0xA9, 0xCB, 0xED, 1, 7};
    for (const auto b : bytes("Pixel 7")) expected.push_back(b);
    const auto game = bytes("Pokémon Émeraude");
    expected.push_back(static_cast<std::uint8_t>(game.size()));
    expected.insert(expected.end(), game.begin(), game.end());
    require(encode(hello) == expected, "Hello bytes match Android");
    for (const Control& message : {Control{hello}, Control{Pause{true}}, Control{Pause{false}}, Control{Ping{}},
                                   Control{Bye{true}}, Control{Bye{false}}})
        require(decode(encode(message)) == message, "Control message round trip");
    require(encode(Pause{true}) == std::vector<std::uint8_t>{2, 1}, "Pause bytes");
    require(encode(Bye{true}) == std::vector<std::uint8_t>{4, 1}, "Bye bytes");

    // Long names: 60 bytes at most, never half a character.
    const auto emoji = repeat("🎮", 40);
    const auto cut = decode(encode(Hello{1, 1, 7, false, emoji, repeat("é", 100)}));
    require(cut && std::get<Hello>(*cut).device == repeat("🎮", 15), "Device cut on a character");
    require(std::get<Hello>(*cut).game == repeat("é", 30), "Game cut on a character");
    require(encode(Hello{1, 1, 7, false, emoji, emoji}).size() <= max_payload, "Hello fits a frame");

    // Malformed messages are ignored.
    auto short_hello = encode(Hello{1, 1, 42, false, "A", "B"});
    auto long_hello = short_hello;
    long_hello.push_back(0);
    short_hello.pop_back();
    require(!decode(short_hello) && !decode(long_hello), "Hello of the wrong size");
    for (const auto& bad : {std::vector<std::uint8_t>{}, std::vector<std::uint8_t>{9}, std::vector<std::uint8_t>{2},
                            std::vector<std::uint8_t>{3, 0}})
        require(!decode(bad), "Malformed control message");

    // Player 0 is the smaller identifier, compared unsigned.
    require(player_for(5, ~0ull) == 0 && player_for(~0ull, 5) == 1, "Unsigned comparison");
    require(player_for(1, 2) == 0 && player_for(2, 1) == 1, "Smaller is player 0");
    require(short_code(0xABCD000000001234ull) == "ABCD" && short_code(0x0001FFFFFFFFFFFFull) == "0001", "Short code");

    // A single partner links by itself, called by the smaller identifier.
    using namespace policy;
    const Peer other{20, "Pixel 7"};
    require(std::get<Connect>(decide(10, {other}, {}, std::nullopt)).peer == other, "Smaller calls");
    require(std::holds_alternative<Wait>(decide(30, {other}, {}, std::nullopt)), "Larger waits");
    require(std::holds_alternative<Wait>(decide(10, {}, {}, std::nullopt)), "Nobody");
    require(std::holds_alternative<Wait>(decide(10, {other}, {20}, std::nullopt)), "Avoided");
    require(std::holds_alternative<Wait>(decide(10, {{10, "Moi"}}, {}, std::nullopt)), "Own announcement");
    // Several ask the player, by name.
    const Peer b{20, "Pixel 7"}, a{30, "Galaxy S23"};
    require(std::get<Choose>(decide(10, {b, a}, {}, std::nullopt)).peers == std::vector<Peer>{a, b}, "Choice");
    require(std::get<Connect>(decide(10, {b, a}, {30}, std::nullopt)).peer == b, "One left after avoiding");
    require(std::get<Connect>(decide(40, {b, a}, {}, 20)).peer == b, "The player's choice calls");
    require(std::holds_alternative<Wait>(decide(40, {a}, {}, 20)), "The chosen one is gone");

    const Hello caller{1, family_gb, 99, false, "Pixel 7", ""};
    require(accepts(caller, family_gb, {}), "Same console accepted");
    require(!accepts(caller, family_gba, {}), "Other console refused");
    auto newer = caller;
    newer.version = 2;
    require(!accepts(newer, family_gb, {}), "Other protocol refused");
    require(!accepts(caller, family_gb, {99}), "Declined refused");
    std::cout << "Wireless protocol OK\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "FAILED: " << e.what() << "\n";
    return 1;
}
