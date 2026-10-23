// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/wireless/wireless_link.hpp>
#include <ravenemudsk/wireless/mdns.hpp>
#include <algorithm>
#include <charconv>
#include <random>
#include <utility>

namespace ravenemudsk_link {
namespace {
using namespace std::chrono_literals;
constexpr auto tick_every = 500ms;
constexpr int connect_timeout_ms = 3000;
constexpr auto hello_timeout = 5s;
// The core keeps its signal three seconds; the search, a little longer.
constexpr auto search_grace = 10s;
constexpr auto cooldown = 3s;
const char* const searching_notice = "Câble link : recherche d'un autre appareil…";

std::uint64_t random_id() {
    std::random_device device;
    std::uint64_t id = 0;
    while (id == 0) id = (std::uint64_t(device()) << 32) ^ device();
    return id;
}

std::string default_device() {
#if defined(_WIN32)
    return "PC Windows";
#elif defined(__APPLE__)
    return "Mac";
#else
    return "PC Linux";
#endif
}

std::string hex(std::uint64_t id) {
    char out[20];
    const auto r = std::to_chars(out, out + sizeof out, id, 16);
    return std::string(out, r.ptr);
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return s;
}
} // namespace

void WirelessLink::Sink::send(void* user, const std::uint8_t* message, std::size_t size) {
    static_cast<Sink*>(user)->connection->send_core({message, size});
}

WirelessLink::WirelessLink(Game game, Options options)
    : game_(std::move(game)), id_(options.id ? options.id : random_id()),
      device_(options.device.empty() ? default_device() : options.device), discovery_(options.discovery) {
    ravenemudsk_net::startup();
    view_.own_label = device_ + " · " + short_code(id_);
    worker_ = std::thread([this] { run(); });
    servicer_ = std::thread([this] { serve(); });
}

WirelessLink::~WirelessLink() {
    post([this] {
        unlink(Ending::Leave, true);
        stop_search();
        close_pending();
    });
    {
        std::lock_guard lock(tasks_mutex_);
        stopping_ = true;
    }
    tasks_ready_.notify_one();
    worker_.join();
    for (auto& c : connectors_) if (c.thread.joinable()) c.thread.join();
    {
        std::lock_guard lock(service_mutex_);
        service_stop_ = true;
    }
    service_ready_.notify_one();
    servicer_.join();
    // Tasks left behind hold connections: let them go here, on this thread,
    // with those still saying goodbye.
    tasks_.clear();
    leaving_.clear();
}

void WirelessLink::post(std::function<void()> task) {
    {
        std::lock_guard lock(tasks_mutex_);
        if (stopping_) return;
        tasks_.push_back(std::move(task));
    }
    tasks_ready_.notify_one();
}

void WirelessLink::run() {
    auto next_tick = Clock::now();
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock lock(tasks_mutex_);
            tasks_ready_.wait_until(lock, next_tick, [&] { return stopping_ || !tasks_.empty(); });
            if (!tasks_.empty()) {
                task = std::move(tasks_.front());
                tasks_.pop_front();
            } else if (stopping_) {
                break;
            }
        }
        try {
            if (task) task();
            else if (Clock::now() >= next_tick) {
                tick();
                next_tick = Clock::now() + tick_every;
            }
        } catch (...) {
            // A failed step leaves the cable as it was; the next tick goes on.
        }
    }
}

void WirelessLink::serve() {
    while (true) {
        {
            std::unique_lock lock(service_mutex_);
            service_ready_.wait(lock, [&] { return service_wanted_ || service_stop_; });
            if (service_stop_) return;
            service_wanted_ = false;
        }
        // Between two frames, or during a pause: the other console never
        // waits for the next frame.
        std::lock_guard core(*game_.core_mutex);
        if (serviced_) ravenemu_remote_link_service(serviced_);
    }
}

void WirelessLink::set_paused(bool paused) {
    const int value = paused ? 1 : 0;
    if (paused_request_.exchange(value) == value) return;
    post([this, paused] {
        paused_ = paused;
        send_pause();
    });
}

void WirelessLink::set_wanted(bool wanted) { wanted_now_ = wanted; }

void WirelessLink::choose(std::optional<std::uint64_t> id) {
    post([this, id] {
        if (!id) {
            for (const auto& c : choices_) dismissed_.insert(c.id);
        } else {
            chosen_ = id;
            dismissed_.erase(*id);
        }
        evaluate();
    });
}

void WirelessLink::unplug() {
    post([this] {
        if (!current_) return;
        declined_.insert(current_->peer.id);
        unlink(Ending::Refuse);
        evaluate();
    });
}

LinkView WirelessLink::view() const {
    std::lock_guard lock(view_mutex_);
    return view_;
}

std::vector<std::string> WirelessLink::take_notices() {
    std::lock_guard lock(view_mutex_);
    return std::exchange(notices_, {});
}

void WirelessLink::add_peer(std::string name, std::uint64_t id, std::string device, ravenemudsk_net::Ipv4 address,
                            std::uint16_t port) {
    post([this, name = std::move(name), found = Found{id, std::move(device), address, port}] {
        found_[name] = found;
        evaluate();
    });
}

void WirelessLink::notice(std::string text) {
    std::lock_guard lock(view_mutex_);
    notices_.push_back(std::move(text));
}

void WirelessLink::tick() {
    const auto now = Clock::now();
    if (wanted_now_) last_wanted_ = now;
    std::erase_if(cooling_, [&](const auto& entry) { return entry.second <= now; });
    // A connection silent after its opening is not RavenEmu; closing it goes
    // through closed(), which puts the device called on hold.
    for (const auto& c : pending_)
        if (!c->peer && now - c->opened() > hello_timeout) c->close();
    std::erase_if(leaving_, [](const auto& c) { return c->closed(); });
    std::erase_if(connectors_, [](Connector& c) {
        if (!c.done->load()) return false;
        c.thread.join();
        return true;
    });
    send_pause();
    evaluate();
}

bool WirelessLink::wanted(Clock::time_point now) const {
    return last_wanted_ != Clock::time_point{} && now - last_wanted_ < search_grace;
}

void WirelessLink::evaluate() {
    if (current_) {
        publish();
        return;
    }
    if (!wanted(Clock::now()) && pending_.empty() && !connecting_to_) {
        stop_search();
        choices_.clear();
        publish();
        return;
    }
    start_search();
    std::vector<policy::Peer> peers;
    for (const auto& [name, f] : found_)
        if (std::none_of(peers.begin(), peers.end(), [&](const policy::Peer& p) { return p.id == f.id; }))
            peers.push_back({f.id, label(f)});
    std::set<std::uint64_t> avoided = declined_;
    avoided.insert(refused_by_.begin(), refused_by_.end());
    avoided.insert(dismissed_.begin(), dismissed_.end());
    for (const auto& [id, until] : cooling_) avoided.insert(id);
    const auto decision = policy::decide(id_, peers, avoided, chosen_);
    choices_.clear();
    if (const auto* connect_to = std::get_if<policy::Connect>(&decision)) {
        for (const auto& [name, f] : found_)
            if (f.id == connect_to->peer.id) {
                connect(f);
                break;
            }
    } else if (const auto* choose = std::get_if<policy::Choose>(&decision)) {
        for (const auto& p : choose->peers) choices_.push_back({p.id, p.device});
    }
    publish();
}

void WirelessLink::start_search() {
    if (server_.valid()) return;
    std::uint16_t port = 0;
    server_ = ravenemudsk_net::listen_tcp(port);
    if (!server_.valid()) return;
    port_ = port;
    accept_stop_ = std::make_shared<std::atomic<bool>>(false);
    // server_ stays open until this thread is joined, in stop_search().
    acceptor_ = std::thread([this, stop = accept_stop_] {
        while (!*stop) {
            auto incoming = ravenemudsk_net::accept_tcp(server_, 250);
            if (!incoming.valid()) continue;
            auto shared = std::make_shared<ravenemudsk_net::Socket>(std::move(incoming));
            post([this, shared] {
                if (!server_.valid() || current_) return; // the socket closes with `shared`
                open(std::move(*shared), std::nullopt);
            });
        }
    });
    if (discovery_) {
        ravenemudsk_mdns::Service service;
        service.instance = "RavenEmu " + short_code(id_);
        service.host = "ravenemu-" + lower(short_code(id_));
        service.type = {"_ravenemulink", "_tcp", "local"}; // service_type, label by label
        service.port = port;
        service.txt = {"v=" + std::to_string(protocol_version), "f=" + std::to_string(game_.family), "id=" + hex(id_),
                       "n=" + device_.substr(0, 40)};
        if (const auto address = ravenemudsk_net::primary_address()) service.ipv4 = address->value;
        mdns_ = std::make_unique<ravenemudsk_mdns::Mdns>(
            service, [this](std::vector<ravenemudsk_mdns::Peer> peers) {
                post([this, peers = std::move(peers)] { on_peers(peers); });
            });
    }
    // Shown once per game; then the banner says linked or unplugged.
    if (!searching_announced_) {
        searching_announced_ = true;
        notice(searching_notice);
    }
}

void WirelessLink::stop_search() {
    mdns_.reset();
    if (accept_stop_) *accept_stop_ = true;
    if (acceptor_.joinable()) acceptor_.join();
    server_.close();
    port_ = 0;
    if (discovery_) found_.clear();
}

void WirelessLink::on_peers(std::vector<ravenemudsk_mdns::Peer> peers) {
    if (!mdns_) return;
    std::map<std::string, Found> found;
    for (const auto& p : peers) {
        const auto attribute = [&](const char* key) -> std::string {
            const auto it = p.txt.find(key);
            return it == p.txt.end() ? std::string() : it->second;
        };
        if (attribute("v") != std::to_string(protocol_version)) continue;
        if (attribute("f") != std::to_string(game_.family)) continue;
        const auto text = attribute("id");
        std::uint64_t id = 0;
        const auto r = std::from_chars(text.data(), text.data() + text.size(), id, 16);
        if (r.ec != std::errc() || r.ptr != text.data() + text.size() || id == 0 || id == id_) continue;
        auto device = attribute("n");
        if (device.empty()) device = "Appareil";
        found[p.instance] = {id, device, {p.ipv4}, p.port};
    }
    // The chosen device left: the choice comes back.
    if (chosen_ && std::none_of(found.begin(), found.end(), [&](const auto& e) { return e.second.id == *chosen_; }))
        chosen_.reset();
    found_ = std::move(found);
    evaluate();
}

void WirelessLink::close_pending() {
    for (auto& c : pending_) retire(std::move(c), false);
    pending_.clear();
}

void WirelessLink::connect(const Found& peer) {
    if (connecting_to_ || current_) return;
    if (std::any_of(pending_.begin(), pending_.end(),
                    [&](const auto& c) { return c->outgoing() || (c->peer && c->peer->id == peer.id); }))
        return;
    connecting_to_ = peer.id;
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread thread([this, peer, done] {
        auto socket = ravenemudsk_net::connect_tcp(peer.address, peer.port, connect_timeout_ms);
        auto shared = std::make_shared<ravenemudsk_net::Socket>(std::move(socket));
        post([this, shared, id = peer.id] {
            connecting_to_.reset();
            if (!shared->valid()) {
                cool(id);
                evaluate();
                return;
            }
            if (current_ || !server_.valid()) return;
            open(std::move(*shared), id);
        });
        *done = true;
    });
    connectors_.push_back({std::move(thread), done});
}

void WirelessLink::open(ravenemudsk_net::Socket socket, std::optional<std::uint64_t> target) {
    auto connection = std::make_shared<Connection>(std::move(socket), target.has_value());
    connection->target = target;
    std::weak_ptr<Connection> weak = connection;
    Connection::Listener listener;
    listener.control = [this, weak](Connection&, const Control& message) {
        post([this, weak, message] {
            if (auto c = weak.lock()) handle(c, message);
        });
    };
    // Reading thread: the console answers without waiting for the next frame.
    listener.core = [this](Connection&) {
        {
            std::lock_guard lock(service_mutex_);
            service_wanted_ = true;
        }
        service_ready_.notify_one();
    };
    listener.closed = [this, weak](Connection&) {
        post([this, weak] {
            if (auto c = weak.lock()) closed(c);
        });
    };
    pending_.push_back(connection);
    connection->start(std::move(listener));
    connection->send_control(Hello{protocol_version, game_.family, id_, paused_ && wanted(Clock::now()), device_,
                                   game_.title});
}

void WirelessLink::closed(const std::shared_ptr<Connection>& connection) {
    const auto it = std::find(pending_.begin(), pending_.end(), connection);
    const bool waiting = it != pending_.end();
    if (waiting) pending_.erase(it);
    if (current_ && current_->connection == connection) {
        const auto id = current_->peer.id;
        unlink(Ending::Closed);
        cool(id);
    } else if (waiting) {
        // Closed before the link: not called again at every tick.
        const auto id = connection->peer ? std::optional(connection->peer->id) : connection->target;
        if (id && (!current_ || current_->peer.id != *id)) cool(*id);
    }
    evaluate();
}

void WirelessLink::handle(const std::shared_ptr<Connection>& connection, const Control& message) {
    if (const auto* h = std::get_if<Hello>(&message)) {
        hello(connection, *h);
    } else if (const auto* p = std::get_if<Pause>(&message)) {
        if (current_ && current_->connection == connection) {
            partner_paused_ = p->paused;
            publish();
        }
    } else if (const auto* b = std::get_if<Bye>(&message)) {
        const auto peer = connection->peer ? std::optional(connection->peer->id) : std::nullopt;
        if (peer && b->refused) refused_by_.insert(*peer);
        if (current_ && current_->connection == connection) {
            unlink(Ending::Closed);
            if (peer) cool(*peer);
        } else {
            std::erase(pending_, connection);
            connection->close();
            // Busy elsewhere: not called again at once.
            if (peer && (!current_ || current_->peer.id != *peer)) cool(*peer);
        }
        evaluate();
    }
}

void WirelessLink::hello(const std::shared_ptr<Connection>& connection, const Hello& h) {
    if (std::find(pending_.begin(), pending_.end(), connection) == pending_.end() || connection->peer) return;
    connection->peer = h;
    const bool compatible = h.version == protocol_version && h.family == game_.family;
    if (!policy::accepts(h, game_.family, declined_)) {
        std::erase(pending_, connection);
        retire(connection, !compatible || declined_.contains(h.id));
        return;
    }
    if (current_) {
        if (current_->peer.id != h.id || !opens_winner(*connection, h)) {
            // Busy, or a second connection to the same device: both sides keep
            // the one opened by the smaller identifier.
            std::erase(pending_, connection);
            retire(connection, false);
            return;
        }
        unlink(Ending::Leave, true);
    }
    link(connection, h);
}

bool WirelessLink::opens_winner(const Connection& connection, const Hello& h) const {
    const auto opener = connection.outgoing() ? id_ : h.id;
    const auto first = player_for(id_, h.id) == 0 ? id_ : h.id;
    return opener == first;
}

void WirelessLink::link(const std::shared_ptr<Connection>& connection, const Hello& h) {
    const int player = player_for(id_, h.id);
    auto sink = std::make_unique<Sink>(Sink{connection});
    ravenemu_remote_link* cable = nullptr;
    {
        std::lock_guard core(*game_.core_mutex);
        cable = ravenemu_remote_link_create(game_.core, player, &Sink::send, sink.get());
        serviced_ = cable;
    }
    std::erase(pending_, connection);
    if (!cable) {
        retire(connection, true);
        return;
    }
    current_ = Linked{connection, cable, std::move(sink), h};
    ravenemu_remote_link_set_connected(cable, 1);
    connection->bind_core([cable](std::span<const std::uint8_t> message) {
        ravenemu_remote_link_receive(cable, message.data(), message.size());
    });
    {
        std::lock_guard lock(service_mutex_);
        service_wanted_ = true;
    }
    service_ready_.notify_one();
    partner_paused_ = h.paused;
    pause_sent_ = false;
    send_pause();
    chosen_.reset();
    stop_search();
    close_pending();
    linked_ = true;
    notice("Câble relié à " + h.device);
    publish();
}

void WirelessLink::unlink(Ending ending, bool quiet) {
    if (!current_) return;
    auto linked = std::move(*current_);
    current_.reset();
    // Unplugged first: a frame waiting for the other console goes on.
    ravenemu_remote_link_set_connected(linked.cable, 0);
    linked.connection->bind_core({});
    {
        std::lock_guard core(*game_.core_mutex);
        serviced_ = nullptr;
        ravenemu_remote_link_destroy(linked.cable);
    }
    if (ending == Ending::Closed) linked.connection->close();
    else retire(std::move(linked.connection), ending == Ending::Refuse);
    partner_paused_ = false;
    pause_sent_ = false;
    linked_ = false;
    if (!quiet) notice("Câble débranché");
    publish();
}

// A pause stops the other game only while the cable is in use; otherwise
// each plays on its own.
void WirelessLink::send_pause() {
    if (!current_) return;
    const bool pause = paused_ && wanted(Clock::now());
    if (pause == pause_sent_) return;
    pause_sent_ = pause;
    current_->connection->send_control(Pause{pause});
}

// The goodbye goes out, then the connection waits for the other side to
// close (two seconds at most) before it is let go.
void WirelessLink::retire(std::shared_ptr<Connection> connection, bool refused) {
    connection->leave(refused);
    leaving_.push_back(std::move(connection));
}

void WirelessLink::cool(std::uint64_t id) { cooling_[id] = Clock::now() + cooldown; }

std::string WirelessLink::label(const Found& found) const { return found.device + " · " + short_code(found.id); }

void WirelessLink::publish() {
    std::lock_guard lock(view_mutex_);
    view_.choices = choices_;
    view_.partner_paused = partner_paused_;
    if (current_) {
        const auto& peer = current_->peer;
        view_.partner = peer.game.empty() ? peer.device : peer.device + " (" + peer.game + ")";
    } else {
        view_.partner.clear();
    }
}
} // namespace ravenemudsk_link
