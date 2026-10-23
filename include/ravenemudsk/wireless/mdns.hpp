// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ravenemudsk/wireless/net.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

// Multicast DNS (RFC 6762) with DNS-SD (RFC 6763), the little the wireless
// link cable needs: announce this computer's service and find the others',
// phones included (Android's NsdManager speaks the same). Written for
// RavenEmu; no system responder is required.
namespace ravenemudsk_mdns {
using Clock = std::chrono::steady_clock;
// A domain name, label by label; compared without regard to ASCII case.
using Name = std::vector<std::string>;
bool same(const Name& a, const Name& b);
std::string text(const Name& name);

enum Type : std::uint16_t { A = 1, PTR = 12, TXT = 16, AAAA = 28, SRV = 33, ANY = 255 };

struct Question {
    Name name;
    std::uint16_t type = PTR;
    bool unicast = false; // QU: the asker accepts a unicast reply
};
struct Record {
    Name name;
    std::uint16_t type = 0;
    bool cache_flush = false; // a unique record: replaces the cached ones
    std::uint32_t ttl = 0;    // 0: goodbye, the record is gone
    Name target;              // PTR, SRV
    std::uint16_t port = 0;   // SRV
    std::vector<std::string> txt; // TXT entries, "key=value"
    std::uint32_t ipv4 = 0;   // A
};
struct Message {
    std::uint16_t id = 0;
    bool response = false;
    std::vector<Question> questions;
    std::vector<Record> records; // answers, authorities and additionals together
};

// Empty for a malformed packet.
std::optional<Message> parse(std::span<const std::uint8_t> packet);
std::vector<std::uint8_t> query(const std::vector<Question>& questions);
// `questions` are echoed for a legacy (non-5353) asker, empty otherwise.
std::vector<std::uint8_t> response(std::uint16_t id, const std::vector<Question>& questions,
                                   const std::vector<Record>& answers, const std::vector<Record>& additionals);

// What this computer announces.
struct Service {
    std::string instance;          // "RavenEmu 4F2A"
    std::string host;              // "ravenemu-4f2a" (".local" added)
    Name type;                     // {"_ravenemulink", "_tcp", "local"}
    std::uint16_t port = 0;
    std::vector<std::string> txt;  // "v=1", "f=1", ...
    std::uint32_t ipv4 = 0;
    Name instance_name() const;
    Name host_name() const;
};
// The service's four records: PTR, SRV, TXT and A.
std::vector<Record> records(const Service& service, std::uint32_t ttl);
struct Reply {
    std::vector<Record> answers, additionals;
};
// The answer to a query about this service, if it asks about it.
std::optional<Reply> answer(const Message& query, const Service& service);

// A service of the same type found on the network, once complete.
struct Peer {
    std::string instance;
    std::uint32_t ipv4 = 0;
    std::uint16_t port = 0;
    std::map<std::string, std::string> txt;
    bool operator==(const Peer&) const = default;
};
// What the network said about services of one type; pure, fed by the runner.
class Browser {
public:
    explicit Browser(Name type) : type_(std::move(type)) {}
    void feed(const Message& message, Clock::time_point now);
    // The services known in full (address, port, attributes), not expired.
    std::vector<Peer> peers(Clock::time_point now) const;
    // Questions that would complete the services half known.
    std::vector<Question> missing(Clock::time_point now) const;
private:
    struct Instance {
        std::string label;
        Clock::time_point expires;
        std::optional<Name> host;
        std::uint16_t port = 0;
        std::optional<std::map<std::string, std::string>> txt;
    };
    Name type_;
    std::map<std::string, Instance> instances_;          // by lower-case label
    std::map<std::string, std::pair<std::uint32_t, Clock::time_point>> hosts_; // by lower-case name
};

// Announces `service` and watches the network for its type, on a thread of
// its own. `on_peers` receives the complete list at each change, from that
// thread; this computer's own service is left out.
class Mdns {
public:
    using Listener = std::function<void(std::vector<Peer>)>;
    Mdns(Service service, Listener on_peers);
    ~Mdns(); // says goodbye, then stops
    Mdns(const Mdns&) = delete;
    Mdns& operator=(const Mdns&) = delete;
    // False when port 5353 could not be opened: the service is neither
    // announced nor searched.
    bool working() const { return working_; }
private:
    void run();
    void handle(const Message& message, ravenemudsk_net::Ipv4 from, std::uint16_t port, Clock::time_point now);
    void send(const std::vector<std::uint8_t>& packet);
    Service service_;
    Listener on_peers_;
    ravenemudsk_net::Socket socket_;
    Browser browser_;
    std::vector<Peer> reported_;
    std::atomic<bool> stop_{false};
    bool working_ = false;
    std::thread thread_;
};
} // namespace ravenemudsk_mdns
