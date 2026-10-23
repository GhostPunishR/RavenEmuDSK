// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/wireless/mdns.hpp>
#include <algorithm>
#include <cctype>

namespace ravenemudsk_mdns {
namespace {
constexpr std::uint16_t class_in = 1;
constexpr std::uint16_t top_bit = 0x8000;
// A service not heard from for this long is gone, whatever its TTL: a phone
// that leaves the network says no goodbye. Queries every 15 s refresh it.
constexpr auto stale_after = std::chrono::seconds(45);
constexpr std::uint32_t own_ttl = 120;

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> p) : p_(p) {}
    bool ok() const { return ok_; }
    std::size_t at() const { return at_; }
    void seek(std::size_t at) { if (at > p_.size()) ok_ = false; else at_ = at; }
    std::uint8_t u8() {
        if (at_ + 1 > p_.size()) { ok_ = false; return 0; }
        return p_[at_++];
    }
    std::uint16_t u16() { const std::uint16_t hi = u8(); return static_cast<std::uint16_t>((hi << 8) | u8()); }
    std::uint32_t u32() { const std::uint32_t hi = u16(); return (hi << 16) | u16(); }
    std::string bytes(std::size_t n) {
        if (at_ + n > p_.size()) { ok_ = false; return {}; }
        std::string out(reinterpret_cast<const char*>(p_.data() + at_), n);
        at_ += n;
        return out;
    }
    // A name, following compression pointers (bounded, so a loop cannot hang).
    Name name() {
        Name out;
        std::size_t at = at_, end = 0, total = 0;
        int jumps = 0;
        while (ok_) {
            if (at >= p_.size()) { ok_ = false; break; }
            const std::uint8_t len = p_[at];
            if (len == 0) { ++at; break; }
            if ((len & 0xC0) == 0xC0) {
                if (at + 1 >= p_.size() || ++jumps > 32) { ok_ = false; break; }
                if (!end) end = at + 2;
                at = static_cast<std::size_t>(((len & 0x3F) << 8) | p_[at + 1]);
                continue;
            }
            if ((len & 0xC0) != 0 || at + 1 + len > p_.size()) { ok_ = false; break; }
            out.emplace_back(reinterpret_cast<const char*>(p_.data() + at + 1), len);
            total += len + 1u;
            if (total > 255) { ok_ = false; break; }
            at += 1u + len;
        }
        at_ = end ? end : at;
        return out;
    }
private:
    std::span<const std::uint8_t> p_;
    std::size_t at_ = 0;
    bool ok_ = true;
};

class Writer {
public:
    void u8(std::uint8_t v) { out_.push_back(v); }
    void u16(std::uint16_t v) { u8(static_cast<std::uint8_t>(v >> 8)); u8(static_cast<std::uint8_t>(v)); }
    void u32(std::uint32_t v) { u16(static_cast<std::uint16_t>(v >> 16)); u16(static_cast<std::uint16_t>(v)); }
    void name(const Name& n) {
        for (const auto& label : n) {
            const auto size = std::min<std::size_t>(label.size(), 63);
            u8(static_cast<std::uint8_t>(size));
            out_.insert(out_.end(), label.begin(), label.begin() + std::ptrdiff_t(size));
        }
        u8(0);
    }
    void record(const Record& r) {
        name(r.name);
        u16(r.type);
        u16(static_cast<std::uint16_t>(class_in | (r.cache_flush ? top_bit : 0)));
        u32(r.ttl);
        const auto length_at = out_.size();
        u16(0);
        const auto start = out_.size();
        switch (r.type) {
        case PTR: name(r.target); break;
        case SRV: u16(0); u16(0); u16(r.port); name(r.target); break;
        case TXT:
            if (r.txt.empty()) u8(0);
            for (const auto& entry : r.txt) {
                const auto size = std::min<std::size_t>(entry.size(), 255);
                u8(static_cast<std::uint8_t>(size));
                out_.insert(out_.end(), entry.begin(), entry.begin() + std::ptrdiff_t(size));
            }
            break;
        case A: u32(r.ipv4); break;
        default: break;
        }
        const auto length = out_.size() - start;
        out_[length_at] = static_cast<std::uint8_t>(length >> 8);
        out_[length_at + 1] = static_cast<std::uint8_t>(length);
    }
    std::vector<std::uint8_t> take() { return std::move(out_); }
private:
    std::vector<std::uint8_t> out_;
};

void header(Writer& w, std::uint16_t id, bool response, std::size_t questions, std::size_t answers,
            std::size_t additionals) {
    w.u16(id);
    w.u16(response ? 0x8400 : 0); // response, authoritative
    w.u16(static_cast<std::uint16_t>(questions));
    w.u16(static_cast<std::uint16_t>(answers));
    w.u16(0);
    w.u16(static_cast<std::uint16_t>(additionals));
}

bool is_instance_of(const Name& name, const Name& type) {
    if (name.size() != type.size() + 1) return false;
    return same(Name(name.begin() + 1, name.end()), type);
}
} // namespace

bool same(const Name& a, const Name& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i])) return false;
    return true;
}

std::string text(const Name& name) {
    std::string out;
    for (const auto& label : name) {
        if (!out.empty()) out += '.';
        out += label;
    }
    return out;
}

std::optional<Message> parse(std::span<const std::uint8_t> packet) {
    Reader r(packet);
    Message m;
    m.id = r.u16();
    const auto flags = r.u16();
    m.response = (flags & 0x8000) != 0;
    const auto questions = r.u16(), answers = r.u16(), authorities = r.u16(), additionals = r.u16();
    if (!r.ok()) return std::nullopt;
    for (int i = 0; i < questions && r.ok(); ++i) {
        Question q;
        q.name = r.name();
        q.type = r.u16();
        q.unicast = (r.u16() & top_bit) != 0;
        m.questions.push_back(std::move(q));
    }
    const int records = answers + authorities + additionals;
    for (int i = 0; i < records && r.ok(); ++i) {
        Record rec;
        rec.name = r.name();
        rec.type = r.u16();
        rec.cache_flush = (r.u16() & top_bit) != 0;
        rec.ttl = r.u32();
        const std::size_t length = r.u16();
        const std::size_t start = r.at();
        if (!r.ok() || start + length > packet.size()) return std::nullopt;
        switch (rec.type) {
        case PTR: rec.target = r.name(); break;
        case SRV: r.u16(); r.u16(); rec.port = r.u16(); rec.target = r.name(); break;
        case TXT:
            while (r.ok() && r.at() < start + length) {
                const std::size_t size = r.u8();
                if (size) rec.txt.push_back(r.bytes(size));
            }
            break;
        case A: if (length == 4) rec.ipv4 = r.u32(); break;
        default: break;
        }
        if (!r.ok()) return std::nullopt;
        r.seek(start + length);
        m.records.push_back(std::move(rec));
    }
    if (!r.ok()) return std::nullopt;
    return m;
}

std::vector<std::uint8_t> query(const std::vector<Question>& questions) {
    Writer w;
    header(w, 0, false, questions.size(), 0, 0);
    for (const auto& q : questions) {
        w.name(q.name);
        w.u16(q.type);
        w.u16(static_cast<std::uint16_t>(class_in | (q.unicast ? top_bit : 0)));
    }
    return w.take();
}

std::vector<std::uint8_t> response(std::uint16_t id, const std::vector<Question>& questions,
                                   const std::vector<Record>& answers, const std::vector<Record>& additionals) {
    Writer w;
    header(w, id, true, questions.size(), answers.size(), additionals.size());
    for (const auto& q : questions) {
        w.name(q.name);
        w.u16(q.type);
        w.u16(class_in);
    }
    for (const auto& r : answers) w.record(r);
    for (const auto& r : additionals) w.record(r);
    return w.take();
}

Name Service::instance_name() const {
    Name n{instance};
    n.insert(n.end(), type.begin(), type.end());
    return n;
}
Name Service::host_name() const { return {host, "local"}; }

std::vector<Record> records(const Service& s, std::uint32_t ttl) {
    std::vector<Record> out;
    Record ptr;
    ptr.name = s.type;
    ptr.type = PTR;
    ptr.ttl = ttl;
    ptr.target = s.instance_name();
    out.push_back(ptr);
    Record srv;
    srv.name = s.instance_name();
    srv.type = SRV;
    srv.cache_flush = true;
    srv.ttl = ttl;
    srv.port = s.port;
    srv.target = s.host_name();
    out.push_back(srv);
    Record txt;
    txt.name = s.instance_name();
    txt.type = TXT;
    txt.cache_flush = true;
    txt.ttl = ttl;
    txt.txt = s.txt;
    out.push_back(txt);
    if (s.ipv4) {
        Record a;
        a.name = s.host_name();
        a.type = A;
        a.cache_flush = true;
        a.ttl = ttl;
        a.ipv4 = s.ipv4;
        out.push_back(a);
    }
    return out;
}

std::optional<Reply> answer(const Message& query, const Service& service) {
    if (query.response) return std::nullopt;
    const auto all = records(service, own_ttl);
    const auto find = [&](std::uint16_t type) -> const Record* {
        for (const auto& r : all)
            if (r.type == type) return &r;
        return nullptr;
    };
    // The asker already knows this PTR (known-answer suppression).
    const bool known_ptr = std::any_of(query.records.begin(), query.records.end(), [&](const Record& r) {
        return r.type == PTR && r.ttl > own_ttl / 2 && same(r.target, service.instance_name());
    });
    Reply reply;
    const auto add = [](std::vector<Record>& to, const Record* r) {
        if (r && std::none_of(to.begin(), to.end(), [&](const Record& x) { return x.type == r->type; }))
            to.push_back(*r);
    };
    for (const auto& q : query.questions) {
        const bool any = q.type == ANY;
        if ((q.type == PTR || any) && same(q.name, service.type) && !known_ptr) {
            add(reply.answers, find(PTR));
            add(reply.additionals, find(SRV));
            add(reply.additionals, find(TXT));
            add(reply.additionals, find(A));
        }
        if (same(q.name, service.instance_name())) {
            if (q.type == SRV || any) {
                add(reply.answers, find(SRV));
                add(reply.additionals, find(A));
            }
            if (q.type == TXT || any) add(reply.answers, find(TXT));
        }
        if ((q.type == A || any) && same(q.name, service.host_name())) add(reply.answers, find(A));
    }
    std::erase_if(reply.additionals, [&](const Record& r) {
        return std::any_of(reply.answers.begin(), reply.answers.end(), [&](const Record& x) { return x.type == r.type; });
    });
    if (reply.answers.empty()) return std::nullopt;
    return reply;
}

void Browser::feed(const Message& message, Clock::time_point now) {
    if (!message.response) return;
    const auto life = [&](std::uint32_t ttl) {
        return now + std::min<Clock::duration>(std::chrono::seconds(ttl), stale_after);
    };
    for (const auto& r : message.records) {
        if (r.type == A) {
            const auto key = lower(text(r.name));
            if (r.ttl == 0) hosts_.erase(key);
            else hosts_[key] = {r.ipv4, life(r.ttl)};
            continue;
        }
        Name instance;
        if (r.type == PTR && same(r.name, type_) && is_instance_of(r.target, type_)) instance = r.target;
        else if ((r.type == SRV || r.type == TXT) && is_instance_of(r.name, type_)) instance = r.name;
        else continue;
        const auto key = lower(instance.front());
        if (r.ttl == 0) {
            instances_.erase(key);
            continue;
        }
        auto& entry = instances_[key];
        entry.label = instance.front();
        entry.expires = std::max(entry.expires, life(r.ttl));
        if (r.type == SRV) {
            entry.host = r.target;
            entry.port = r.port;
        } else if (r.type == TXT) {
            std::map<std::string, std::string> attributes;
            for (const auto& item : r.txt) {
                const auto eq = item.find('=');
                if (eq == std::string::npos) attributes[lower(item)] = "";
                else attributes[lower(item.substr(0, eq))] = item.substr(eq + 1);
            }
            entry.txt = std::move(attributes);
        }
    }
}

std::vector<Peer> Browser::peers(Clock::time_point now) const {
    std::vector<Peer> out;
    for (const auto& [key, entry] : instances_) {
        if (entry.expires <= now || !entry.host || !entry.port || !entry.txt) continue;
        const auto host = hosts_.find(lower(text(*entry.host)));
        if (host == hosts_.end() || host->second.second <= now) continue;
        out.push_back({entry.label, host->second.first, entry.port, *entry.txt});
    }
    return out;
}

std::vector<Question> Browser::missing(Clock::time_point now) const {
    std::vector<Question> out;
    for (const auto& [key, entry] : instances_) {
        if (entry.expires <= now) continue;
        Name name{entry.label};
        name.insert(name.end(), type_.begin(), type_.end());
        if (!entry.host) out.push_back({name, SRV, false});
        if (!entry.txt) out.push_back({name, TXT, false});
        if (entry.host) {
            const auto host = hosts_.find(lower(text(*entry.host)));
            if (host == hosts_.end() || host->second.second <= now) out.push_back({*entry.host, A, false});
        }
    }
    return out;
}

Mdns::Mdns(Service service, Listener on_peers)
    : service_(std::move(service)), on_peers_(std::move(on_peers)), browser_(service_.type) {
    socket_ = ravenemudsk_net::open_mdns({service_.ipv4});
    working_ = socket_.valid();
    if (working_) thread_ = std::thread([this] { run(); });
}

Mdns::~Mdns() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    // Goodbye: the same records with a TTL of 0.
    if (working_) send(response(0, {}, records(service_, 0), {}));
}

void Mdns::send(const std::vector<std::uint8_t>& packet) {
    ravenemudsk_net::send_udp(socket_, packet, ravenemudsk_net::mdns_group, ravenemudsk_net::mdns_port);
}

void Mdns::handle(const Message& message, ravenemudsk_net::Ipv4 from, std::uint16_t port, Clock::time_point now) {
    if (message.response) {
        browser_.feed(message, now);
        return;
    }
    const auto reply = answer(message, service_);
    if (!reply) return;
    // A one-shot (legacy) asker waits on its own port, for its own id.
    if (port != ravenemudsk_net::mdns_port)
        ravenemudsk_net::send_udp(socket_, response(message.id, message.questions, reply->answers, reply->additionals),
                                  from, port);
    else
        send(response(0, {}, reply->answers, reply->additionals));
}

void Mdns::run() {
    using namespace std::chrono_literals;
    const Question browse{service_.type, PTR, false};
    auto now = Clock::now();
    auto next_query = now, next_announce = now, next_missing = now + 500ms;
    int queries = 0, announcements = 0;
    std::vector<std::uint8_t> buffer(9000);
    while (!stop_) {
        now = Clock::now();
        if (announcements < 2 && now >= next_announce) {
            send(response(0, {}, records(service_, own_ttl), {}));
            ++announcements;
            next_announce = now + 1s;
        }
        if (now >= next_query) {
            // The first query asks for unicast replies (QU), as RFC 6762 suggests.
            send(query({{browse.name, PTR, queries == 0}}));
            static constexpr std::chrono::seconds delays[] = {1s, 2s, 4s, 8s};
            next_query = now + (queries < 4 ? delays[queries] : 15s);
            ++queries;
        }
        if (now >= next_missing) {
            if (const auto questions = browser_.missing(now); !questions.empty()) send(query(questions));
            next_missing = now + 1s;
        }
        ravenemudsk_net::Ipv4 from;
        std::uint16_t port = 0;
        const int n = ravenemudsk_net::receive_udp(socket_, buffer, 200, from, port);
        if (n > 0) {
            if (const auto message = parse({buffer.data(), static_cast<std::size_t>(n)}))
                handle(*message, from, port, Clock::now());
        }
        auto peers = browser_.peers(Clock::now());
        std::erase_if(peers, [&](const Peer& p) { return same({p.instance}, {service_.instance}); });
        if (peers != reported_) {
            reported_ = peers;
            on_peers_(peers);
        }
    }
}
} // namespace ravenemudsk_mdns
