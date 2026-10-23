// SPDX-License-Identifier: AGPL-3.0-only
// The mDNS codec, the answers to queries and the browser, without a network.
#include <ravenemudsk/wireless/mdns.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace ravenemudsk_mdns;
using namespace std::chrono_literals;
static void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }

static Service service() {
    Service s;
    s.instance = "RavenEmu 4F2A";
    s.host = "ravenemu-4f2a";
    s.type = {"_ravenemulink", "_tcp", "local"};
    s.port = 40123;
    s.txt = {"v=1", "f=2", "id=4f2a000000000001", "n=PC Linux"};
    s.ipv4 = 0xC0A8010A; // 192.168.1.10
    return s;
}

static const Record* find(const std::vector<Record>& records, std::uint16_t type) {
    const auto it = std::find_if(records.begin(), records.end(), [&](const Record& r) { return r.type == type; });
    return it == records.end() ? nullptr : &*it;
}

int main() try {
    const auto s = service();
    require(same({"RavenEmu 4F2A", "_RavenEmuLink", "_TCP", "Local"}, s.instance_name()), "Names ignore case");
    require(text(s.host_name()) == "ravenemu-4f2a.local", "Host name");

    // A query survives the trip, with its unicast bit.
    const auto q = parse(query({{s.type, PTR, true}}));
    require(q && !q->response && q->questions.size() == 1, "Query parsed");
    require(same(q->questions[0].name, s.type) && q->questions[0].type == PTR && q->questions[0].unicast, "Question");

    // The four records survive the trip.
    const auto announced = parse(response(0, {}, records(s, 120), {}));
    require(announced && announced->response && announced->records.size() == 4, "Announcement parsed");
    const auto* ptr = find(announced->records, PTR);
    const auto* srv = find(announced->records, SRV);
    const auto* txt = find(announced->records, TXT);
    const auto* a = find(announced->records, A);
    require(ptr && same(ptr->target, s.instance_name()) && ptr->ttl == 120 && !ptr->cache_flush, "PTR");
    require(srv && srv->port == 40123 && same(srv->target, s.host_name()) && srv->cache_flush, "SRV");
    require(txt && txt->txt == s.txt, "TXT");
    require(a && a->ipv4 == 0xC0A8010A && same(a->name, s.host_name()), "A");

    // Compressed names, as Android and the system responders write them:
    // the PTR's target points back to the question's name.
    const std::vector<std::uint8_t> packet{
        0, 0, 0x84, 0, 0, 1, 0, 1, 0, 0, 0, 0,
        5, '_', 'a', 'b', 'c', 'd', 4, '_', 't', 'c', 'p', 5, 'l', 'o', 'c', 'a', 'l', 0, 0, 12, 0, 1,
        0xC0, 12, 0, 12, 0, 1, 0, 0, 0, 120, 0, 4, 1, 'X', 0xC0, 12};
    const auto compressed = parse(packet);
    require(compressed && compressed->records.size() == 1, "Compressed packet parsed");
    require(text(compressed->records[0].target) == "X._abcd._tcp.local", "Compression pointer followed");
    // A pointer to itself never ends: refused.
    auto loop = packet;
    loop[loop.size() - 2] = 0xC0;
    loop[loop.size() - 1] = static_cast<std::uint8_t>(loop.size() - 2);
    require(!parse(loop), "Pointer loop refused");
    auto truncated = packet;
    truncated.resize(truncated.size() - 3);
    require(!parse(truncated), "Truncated packet refused");

    // Answers: the PTR with its details, unless the asker already knows it.
    Message browse;
    browse.questions = {{s.type, PTR, false}};
    const auto reply = answer(browse, s);
    require(reply && reply->answers.size() == 1 && reply->answers[0].type == PTR, "PTR answered");
    require(reply->additionals.size() == 3, "SRV, TXT and A added");
    auto known = browse;
    known.records = {*ptr};
    require(!answer(known, s), "Known answer suppressed");
    known.records[0].ttl = 30;
    require(answer(known, s).has_value(), "Expiring known answer refreshed");
    Message resolve;
    resolve.questions = {{s.instance_name(), SRV, false}, {s.instance_name(), TXT, false}};
    const auto resolved = answer(resolve, s);
    require(resolved && resolved->answers.size() == 2 && resolved->additionals.size() == 1 &&
                resolved->additionals[0].type == A, "Instance resolved");
    Message host;
    host.questions = {{s.host_name(), A, false}};
    require(answer(host, s) && answer(host, s)->answers[0].ipv4 == s.ipv4, "Host address");
    Message other;
    other.questions = {{{"_http", "_tcp", "local"}, PTR, false}};
    require(!answer(other, s), "Another service is not ours to answer");

    // The browser: a service is known once complete, then forgotten.
    const auto now = Clock::now();
    Browser browser(s.type);
    Message first;
    first.response = true;
    first.records = {*ptr};
    browser.feed(first, now);
    require(browser.peers(now).empty(), "Half known");
    auto missing = browser.missing(now);
    require(missing.size() == 2 && missing[0].type == SRV && missing[1].type == TXT, "Asks for SRV and TXT");
    Message rest;
    rest.response = true;
    rest.records = {*srv, *txt};
    browser.feed(rest, now);
    missing = browser.missing(now);
    require(missing.size() == 1 && missing[0].type == A, "Then for the address");
    Message address;
    address.response = true;
    address.records = {*a};
    browser.feed(address, now);
    const auto peers = browser.peers(now);
    require(peers.size() == 1 && peers[0].instance == "RavenEmu 4F2A" && peers[0].port == 40123 &&
                peers[0].ipv4 == 0xC0A8010A && peers[0].txt.at("id") == "4f2a000000000001" &&
                peers[0].txt.at("n") == "PC Linux", "Complete peer");
    require(browser.missing(now).empty(), "Nothing missing");
    require(browser.peers(now + 46s).empty(), "Stale after 45 seconds");
    Message goodbye;
    goodbye.response = true;
    goodbye.records = records(s, 0);
    browser.feed(goodbye, now + 1s);
    require(browser.peers(now + 1s).empty(), "Goodbye forgets");
    // Queries and other types are not the browser's business.
    browser.feed(*q, now);
    Message http;
    http.response = true;
    auto foreign = *ptr;
    foreign.name = {"_http", "_tcp", "local"};
    http.records = {foreign};
    browser.feed(http, now);
    require(browser.peers(now).empty() && browser.missing(now).empty(), "Ignores the rest");
    std::cout << "mDNS OK\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "FAILED: " << e.what() << "\n";
    return 1;
}
