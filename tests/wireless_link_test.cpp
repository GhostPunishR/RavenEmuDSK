// SPDX-License-Identifier: AGPL-3.0-only
// The wireless link cable on this computer's loopback: one connection, then
// two games that link by themselves and trade a byte through RavenCore.
#include <ravenemudsk/wireless/wireless_link.hpp>
#include <ravenemu/bridge.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace ravenemudsk_link;
using namespace std::chrono_literals;
static void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }

using Clock = std::chrono::steady_clock;
static bool eventually(const std::function<bool()>& done, std::chrono::milliseconds limit = 5000ms) {
    const auto deadline = Clock::now() + limit;
    while (Clock::now() < deadline) {
        if (done()) return true;
        std::this_thread::sleep_for(10ms);
    }
    return done();
}

static constexpr ravenemudsk_net::Ipv4 loopback{0x7F000001};

// A connected pair of sockets on the loopback.
static std::pair<ravenemudsk_net::Socket, ravenemudsk_net::Socket> pair_of_sockets() {
    std::uint16_t port = 0;
    auto server = ravenemudsk_net::listen_tcp(port);
    require(server.valid() && port != 0, "Listening");
    auto client = ravenemudsk_net::connect_tcp(loopback, port, 2000);
    auto accepted = ravenemudsk_net::accept_tcp(server, 2000);
    require(client.valid() && accepted.valid(), "Connected");
    return {std::move(client), std::move(accepted)};
}

struct Received {
    std::mutex mutex;
    std::vector<Control> controls;
    std::vector<std::vector<std::uint8_t>> core;
    std::atomic<bool> closed{false};
    Connection::Listener listener() {
        return {[this](Connection&, const Control& c) {
                    std::lock_guard lock(mutex);
                    controls.push_back(c);
                },
                {}, [this](Connection&) { closed = true; }};
    }
    std::size_t count() {
        std::lock_guard lock(mutex);
        return controls.size();
    }
};

static void connection_test() {
    auto [one, two] = pair_of_sockets();
    Received at_a, at_b; // outlives the connections, which call it until they end
    Connection a(std::move(one), true), b(std::move(two), false);
    a.start(at_a.listener());
    b.start(at_b.listener());
    const Hello hello{1, family_gb, 42, false, "PC Linux", "Tetris"};
    a.send_control(hello);
    // Console messages before the receiver is bound wait, in order.
    a.send_core(std::vector<std::uint8_t>{1, 2, 3});
    a.send_core(std::vector<std::uint8_t>{4});
    require(eventually([&] { return at_b.count() == 1; }), "Hello received");
    require(std::get<Hello>(at_b.controls[0]) == hello, "Hello intact");
    std::this_thread::sleep_for(100ms);
    std::vector<std::vector<std::uint8_t>> core;
    std::mutex core_mutex;
    b.bind_core([&](std::span<const std::uint8_t> m) {
        std::lock_guard lock(core_mutex);
        core.emplace_back(m.begin(), m.end());
    });
    a.send_core(std::vector<std::uint8_t>{5, 6});
    require(eventually([&] {
        std::lock_guard lock(core_mutex);
        return core.size() == 3;
    }), "Core messages delivered");
    require(core[0] == std::vector<std::uint8_t>{1, 2, 3} && core[1] == std::vector<std::uint8_t>{4} &&
                core[2] == std::vector<std::uint8_t>{5, 6}, "In order");
    // Heartbeats keep a quiet connection open, unseen by the listener.
    std::this_thread::sleep_for(1500ms);
    require(!a.closed() && !b.closed() && at_a.count() == 0 && at_b.count() == 1, "Pings stay below");
    // A goodbye reaches the other side before the end.
    b.bind_core({});
    a.leave(true);
    require(eventually([&] { return at_b.count() == 2; }), "Goodbye received");
    require(std::get<Bye>(at_b.controls[1]).refused, "Refusal kept");
    b.close();
    require(eventually([&] { return at_a.closed && at_b.closed; }), "Both closed");
}

// Game Boy, serial port. The caller sends 0xA5 with its own clock until the
// other side answers something else than 0xFF (no cable); the other loads
// 0x3C and waits for the clock. Each stores the byte received in C000 and
// 1 in C001.
static std::vector<std::uint8_t> serial_rom(bool clocks) {
    std::vector<std::uint8_t> rom(0x8000);
    const std::uint8_t entry[] = {0x00, 0xC3, 0x50, 0x01}; // NOP ; JP 0150
    std::memcpy(&rom[0x100], entry, sizeof entry);
    const std::vector<std::uint8_t> caller{
        0xF3,             // DI
        0x3E, 0xA5,       // again: LD A, A5
        0xE0, 0x01,       // LDH (SB), A
        0x3E, 0x81,       // LD A, 81
        0xE0, 0x02,       // LDH (SC), A
        0xF0, 0x02,       // wait: LDH A, (SC)
        0xCB, 0x7F,       // BIT 7, A
        0x20, 0xFA,       // JR NZ, wait
        0xF0, 0x01,       // LDH A, (SB)
        0xFE, 0xFF,       // CP FF
        0x28, 0xEC,       // JR Z, again
        0xEA, 0x00, 0xC0, // LD (C000), A
        0x3E, 0x01,       // LD A, 1
        0xEA, 0x01, 0xC0, // LD (C001), A
        0x18, 0xFE,       // JR .
    };
    const std::vector<std::uint8_t> answerer{
        0xF3, 0x3E, 0x3C, 0xE0, 0x01, 0x3E, 0x80, 0xE0, 0x02, 0xF0, 0x02, 0xCB, 0x7F, 0x20, 0xFA,
        0xF0, 0x01, 0xEA, 0x00, 0xC0, 0x3E, 0x01, 0xEA, 0x01, 0xC0, 0x18, 0xFE,
    };
    const auto& program = clocks ? caller : answerer;
    std::memcpy(&rom[0x150], program.data(), program.size());
    return rom;
}

// One game, run by its own thread as the program's game loop does: the core
// mutex held around each frame, the link told whether the game wants it.
struct Player {
    ravenemu_bridge* core = nullptr;
    std::mutex core_mutex;
    std::unique_ptr<WirelessLink> link;
    std::atomic<bool> stop{false};
    std::thread loop;

    Player(bool clocks, std::uint64_t id, std::string device, std::string title) {
        core = ravenemu_create(RAVENEMU_GB);
        const auto rom = serial_rom(clocks);
        require(core && ravenemu_load(core, rom.data(), rom.size(), nullptr, 0), "Game loaded");
        WirelessLink::Options options;
        options.discovery = false;
        options.id = id;
        options.device = std::move(device);
        link = std::make_unique<WirelessLink>(WirelessLink::Game{core, &core_mutex, family_gb, std::move(title)},
                                              options);
        loop = std::thread([this] {
            std::vector<std::uint32_t> frame(160 * 144);
            while (!stop) {
                bool wanted = false;
                {
                    std::lock_guard lock(core_mutex);
                    ravenemu_run_frame(core, frame.data(), frame.size());
                    wanted = ravenemu_link_wanted(core) != 0;
                }
                link->set_wanted(wanted);
                std::this_thread::sleep_for(5ms);
            }
        });
    }
    ~Player() {
        stop = true;
        loop.join();
        link.reset();
        ravenemu_destroy(core);
    }
    std::uint8_t read(std::uint32_t address) {
        std::lock_guard lock(core_mutex);
        std::uint8_t value = 0;
        ravenemu_read_achievement_memory(core, address, &value, 1);
        return value;
    }
    bool said(const std::string& text) {
        const auto notices = link->take_notices();
        return std::find(notices.begin(), notices.end(), text) != notices.end();
    }
};

static void introduce(Player& to, Player& peer, const char* name, std::uint64_t id, const char* device) {
    to.link->add_peer(name, id, device, loopback, peer.link->port());
}

static void cable_test() {
    Player a(true, 0x1000, "PC Linux", "Tetris");
    Player b(false, 0x2000, "Pixel 7", "Tetris");
    require(a.link->view().own_label == "PC Linux · 0000", "Own label");
    // Both games want the cable: both listen.
    require(eventually([&] { return a.link->port() && b.link->port(); }), "Searching");
    std::vector<std::string> a_notices = a.link->take_notices();
    require(std::find(a_notices.begin(), a_notices.end(), "Câble link : recherche d'un autre appareil…") !=
                a_notices.end(), "Searching said");
    // The discovery's part, done by hand: each sees the other.
    introduce(a, b, "RavenEmu 0000 b", 0x2000, "Pixel 7");
    introduce(b, a, "RavenEmu 0000 a", 0x1000, "PC Linux");
    require(eventually([&] { return a.link->linked() && b.link->linked(); }), "Linked by themselves");
    require(a.link->view().partner == "Pixel 7 (Tetris)" && b.link->view().partner == "PC Linux (Tetris)",
            "Partner shown");
    require(eventually([&] { return a.read(0xC001) == 1 && b.read(0xC001) == 1; }), "Transfer done");
    require(a.read(0xC000) == 0x3C && b.read(0xC000) == 0xA5, "Bytes traded");
    require(a.link->port() == 0 && b.link->port() == 0, "No more listening once linked");

    // The menu here holds the other game.
    a.link->set_paused(true);
    require(eventually([&] { return b.link->partner_paused() && b.link->view().partner_paused; }), "Pause sent");
    a.link->set_paused(false);
    require(eventually([&] { return !b.link->partner_paused(); }), "Pause lifted");

    // Unplugged by the player: neither calls the other again.
    a_notices = a.link->take_notices();
    require(std::find(a_notices.begin(), a_notices.end(), "Câble relié à Pixel 7") != a_notices.end(), "Linked said");
    a.link->unplug();
    require(eventually([&] { return !a.link->linked() && !b.link->linked(); }), "Unplugged on both sides");
    require(a.said("Câble débranché") && b.said("Câble débranché"), "Unplugged said");
    require(eventually([&] { return a.link->port() && b.link->port(); }), "Searching again");
    introduce(a, b, "RavenEmu 0000 b", 0x2000, "Pixel 7");
    introduce(b, a, "RavenEmu 0000 a", 0x1000, "PC Linux");
    std::this_thread::sleep_for(1500ms);
    require(!a.link->linked() && !b.link->linked(), "Not again");
}

// A caller with another console is refused, politely.
static void refusal_test() {
    Player b(false, 0x2000, "Pixel 7", "Tetris");
    require(eventually([&] { return b.link->port() != 0; }), "Searching");
    auto socket = ravenemudsk_net::connect_tcp(loopback, b.link->port(), 2000);
    require(socket.valid(), "Called");
    Received received;
    Connection caller(std::move(socket), true);
    caller.start(received.listener());
    caller.send_control(Hello{1, family_gba, 0x1000, false, "Mac", "Pokémon Émeraude"});
    require(eventually([&] { return received.count() == 2; }), "Answered");
    require(std::holds_alternative<Hello>(received.controls[0]) &&
                std::get<Hello>(received.controls[0]).device == "Pixel 7", "Its introduction");
    require(std::holds_alternative<Bye>(received.controls[1]) && std::get<Bye>(received.controls[1]).refused,
            "Then a refusal");
    require(!b.link->linked(), "Not linked");
}

int main() try {
    ravenemudsk_net::startup();
    connection_test();
    cable_test();
    refusal_test();
    std::cout << "Wireless link OK\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "FAILED: " << e.what() << "\n";
    return 1;
}
