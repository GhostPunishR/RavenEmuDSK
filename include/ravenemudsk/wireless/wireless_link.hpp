// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ravenemudsk/wireless/connection.hpp>
#include <ravenemudsk/wireless/net.hpp>
#include <ravenemu/bridge.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace ravenemudsk_mdns { class Mdns; struct Peer; }

// The wireless link cable: links the running game with that of another
// device on the local network (a phone with RavenEmu Android, or another
// computer) as soon as the game uses its link port. Same rules as on Android:
//
// - Search. While the game wants a partner (ravenemu_link_wanted), this
//   computer announces itself and looks for the others (DNS-SD); otherwise it
//   announces nothing.
// - Choice. A single compatible partner links by itself; several, the player
//   picks one (choose()).
// - Link. A TCP connection carries RavenCore's messages. The device with the
//   smaller identifier is player 0 (the GBA parent).
// - Pause. The game menu here stops the other game too (partner_paused()
//   there), while the cable is in use.
// - End. The cable comes out when the other device leaves or on unplug(), and
//   plugs back in by itself afterwards.
//
// All the state lives on one thread; the public calls are handed over to it.
namespace ravenemudsk_link {
struct LinkChoice {
    std::uint64_t id = 0;
    std::string label;
    bool operator==(const LinkChoice&) const = default;
};
// What the game screen shows of the cable.
struct LinkView {
    std::string partner;          // "Pixel 7 (Pokémon Bleu)" while linked, empty otherwise
    bool partner_paused = false;  // the other game's menu is open
    std::vector<LinkChoice> choices; // several devices wait: the player picks one
    std::string own_label;        // this computer, as the others list it
};

class WirelessLink {
public:
    struct Game {
        ravenemu_bridge* core = nullptr;
        // Held by the game loop around every RavenCore call: a frame may wait
        // for the other console, the cable is plugged in between frames.
        std::mutex* core_mutex = nullptr;
        int family = 0;   // family_gb or family_gba
        std::string title;
    };
    struct Options {
        bool discovery = true;    // mDNS; tests introduce the peers themselves
        std::uint64_t id = 0;     // 0: drawn at random
        std::string device;       // empty: "PC Windows", "Mac" or "PC Linux"
    };
    using Clock = std::chrono::steady_clock;

    WirelessLink(Game game, Options options);
    explicit WirelessLink(Game game) : WirelessLink(std::move(game), Options{}) {}
    ~WirelessLink();
    WirelessLink(const WirelessLink&) = delete;
    WirelessLink& operator=(const WirelessLink&) = delete;

    // Any thread.
    void set_paused(bool paused);  // game menu or quick pause
    void set_wanted(bool wanted);  // ravenemu_link_wanted, after each frame
    void choose(std::optional<std::uint64_t> id); // empty: later
    void unplug();                 // from the game menu: not this partner again for this game
    LinkView view() const;
    bool linked() const { return linked_; }
    bool partner_paused() const { return partner_paused_; }
    // Lines to show for a few seconds: linked, unplugged, searching.
    std::vector<std::string> take_notices();

    // Tests.
    std::uint64_t id() const { return id_; }
    std::uint16_t port() const { return port_; } // listening while searching, 0 otherwise
    void add_peer(std::string name, std::uint64_t id, std::string device, ravenemudsk_net::Ipv4 address,
                  std::uint16_t port);

private:
    struct Found {
        std::uint64_t id = 0;
        std::string device;
        ravenemudsk_net::Ipv4 address;
        std::uint16_t port = 0;
    };
    // RavenCore's messages to the other device, through the connection.
    struct Sink {
        std::shared_ptr<Connection> connection;
        static void send(void* user, const std::uint8_t* message, std::size_t size);
    };
    struct Linked {
        std::shared_ptr<Connection> connection;
        ravenemu_remote_link* cable = nullptr;
        std::unique_ptr<Sink> sink;
        Hello peer;
    };
    enum class Ending { Leave, Refuse, Closed };

    void post(std::function<void()> task);
    void run();
    void serve();
    void tick();
    bool wanted(Clock::time_point now) const;
    void evaluate();
    void start_search();
    void stop_search();
    void close_pending();
    void connect(const Found& peer);
    void open(ravenemudsk_net::Socket socket, std::optional<std::uint64_t> target);
    void on_peers(std::vector<ravenemudsk_mdns::Peer> peers);
    void handle(const std::shared_ptr<Connection>& connection, const Control& message);
    void hello(const std::shared_ptr<Connection>& connection, const Hello& hello);
    void closed(const std::shared_ptr<Connection>& connection);
    bool opens_winner(const Connection& connection, const Hello& hello) const;
    void link(const std::shared_ptr<Connection>& connection, const Hello& hello);
    void unlink(Ending ending, bool quiet = false);
    void send_pause();
    void retire(std::shared_ptr<Connection> connection, bool refused);
    void cool(std::uint64_t id);
    void notice(std::string text);
    void publish();
    std::string label(const Found& found) const;

    const Game game_;
    const std::uint64_t id_;
    const std::string device_;
    const bool discovery_;

    // Shared with the other threads.
    std::atomic<bool> linked_{false};
    std::atomic<bool> partner_paused_{false};
    std::atomic<bool> wanted_now_{false};
    std::atomic<int> paused_request_{-1};
    std::atomic<std::uint16_t> port_{0};
    mutable std::mutex view_mutex_;
    LinkView view_;
    std::vector<std::string> notices_;

    // Hand-over to the link's thread.
    std::mutex tasks_mutex_;
    std::condition_variable tasks_ready_;
    std::deque<std::function<void()>> tasks_;
    bool stopping_ = false;

    // Console service between frames, when a message arrives.
    std::mutex service_mutex_;
    std::condition_variable service_ready_;
    bool service_wanted_ = false;
    bool service_stop_ = false;
    ravenemu_remote_link* serviced_ = nullptr; // under game_.core_mutex

    // Link's thread only.
    bool paused_ = false;
    bool pause_sent_ = false;
    Clock::time_point last_wanted_{};
    std::unique_ptr<ravenemudsk_mdns::Mdns> mdns_;
    ravenemudsk_net::Socket server_;
    std::shared_ptr<std::atomic<bool>> accept_stop_;
    std::thread acceptor_;
    struct Connector { std::thread thread; std::shared_ptr<std::atomic<bool>> done; };
    std::vector<Connector> connectors_;
    std::map<std::string, Found> found_;
    std::vector<std::shared_ptr<Connection>> pending_;
    std::vector<std::shared_ptr<Connection>> leaving_; // goodbye said, until closed
    std::optional<std::uint64_t> connecting_to_;
    std::optional<Linked> current_;
    std::set<std::uint64_t> declined_, refused_by_, dismissed_;
    std::map<std::uint64_t, Clock::time_point> cooling_;
    std::optional<std::uint64_t> chosen_;
    std::vector<LinkChoice> choices_;
    bool searching_announced_ = false;

    std::thread worker_;
    std::thread servicer_;
};
} // namespace ravenemudsk_link
