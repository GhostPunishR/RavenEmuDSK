// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/wireless/connection.hpp>
#include <array>

namespace ravenemudsk_link {
namespace {
constexpr std::size_t queue_limit = 256;
// After a goodbye, the other side has this long to read it and close.
constexpr auto leave_grace = std::chrono::seconds(2);
std::int64_t ticks(Connection::Clock::time_point t) { return t.time_since_epoch().count(); }
} // namespace

Connection::Connection(ravenemudsk_net::Socket socket, bool outgoing) : socket_(std::move(socket)), outgoing_(outgoing) {}

Connection::~Connection() {
    // A goodbye in progress ends by itself, once read (two seconds at most).
    if (!leaving_) close();
    if (reader_.joinable()) reader_.join();
    if (writer_.joinable()) writer_.join();
    socket_.close();
}

void Connection::start(Listener listener) {
    listener_ = std::move(listener);
    ravenemudsk_net::tune_tcp(socket_);
    reader_ = std::thread([this] { read_loop(); });
    writer_ = std::thread([this] { write_loop(); });
}

void Connection::send_core(std::span<const std::uint8_t> message) { enqueue(frame(channel_core, message)); }

void Connection::send_control(const Control& message) { enqueue(frame(channel_control, encode(message))); }

void Connection::enqueue(std::vector<std::uint8_t> frame_bytes) {
    if (closed_) return;
    bool full = false;
    {
        std::lock_guard lock(queue_mutex_);
        if (end_) return;
        if (queue_.size() >= queue_limit) full = true;
        else queue_.push_back(std::move(frame_bytes));
    }
    // A full queue: the other side no longer reads.
    if (full) close();
    else queue_ready_.notify_one();
}

void Connection::bind_core(std::function<void(std::span<const std::uint8_t>)> receiver) {
    std::lock_guard lock(core_mutex_);
    if (receiver)
        for (const auto& message : early_) receiver(message);
    early_.clear();
    receiver_ = std::move(receiver);
}

void Connection::leave(bool refused) {
    send_control(Bye{refused});
    left_at_ = ticks(Clock::now());
    leaving_ = true;
    {
        std::lock_guard lock(queue_mutex_);
        end_ = true;
    }
    queue_ready_.notify_one();
}

void Connection::close() {
    if (closed_.exchange(true)) return;
    socket_.shutdown();
    queue_ready_.notify_one();
    if (listener_.closed) listener_.closed(*this);
}

void Connection::deliver_core(std::vector<std::uint8_t> message) {
    bool bound = false, flooded = false;
    {
        std::lock_guard lock(core_mutex_);
        if (receiver_) {
            receiver_(message);
            bound = true;
        } else if (early_.size() >= queue_limit) {
            // Nothing legitimate sends that much before its introduction.
            flooded = true;
        } else {
            early_.push_back(std::move(message));
        }
    }
    if (flooded) close();
    if (bound && listener_.core) listener_.core(*this);
}

void Connection::read_loop() {
    FrameReader reader;
    std::array<std::uint8_t, 2048> buffer{};
    auto last = Clock::now();
    while (!closed_) {
        const int n = ravenemudsk_net::receive(socket_, buffer, 250);
        const auto now = Clock::now();
        if (leaving_ && now - Clock::time_point(Clock::duration(left_at_.load())) > leave_grace) break;
        if (n == -1) {
            if (now - last > timeout) break;
            continue;
        }
        if (n <= 0) break;
        last = now;
        reader.feed({buffer.data(), static_cast<std::size_t>(n)});
        while (auto f = reader.next()) {
            if (f->channel == channel_core) {
                deliver_core(std::move(f->payload));
            } else if (const auto message = decode(f->payload)) {
                if (!std::holds_alternative<Ping>(*message) && listener_.control) listener_.control(*this, *message);
            }
        }
        if (reader.bad()) break;
    }
    close();
}

void Connection::write_loop() {
    const auto ping = frame(channel_control, encode(Ping{}));
    while (!closed_) {
        std::vector<std::vector<std::uint8_t>> batch;
        bool ending = false;
        {
            std::unique_lock lock(queue_mutex_);
            queue_ready_.wait_for(lock, ping_every, [&] { return closed_ || !queue_.empty() || end_; });
            if (closed_) break;
            while (!queue_.empty()) {
                batch.push_back(std::move(queue_.front()));
                queue_.pop_front();
            }
            ending = end_;
        }
        if (batch.empty() && !ending) batch.push_back(ping);
        for (const auto& bytes : batch) {
            if (!ravenemudsk_net::send_all(socket_, bytes)) {
                close();
                return;
            }
        }
        if (ending) {
            // Reading goes on until the other side closes: closing now, with
            // unread bytes, could erase the goodbye.
            socket_.shutdown_send();
            return;
        }
    }
}
} // namespace ravenemudsk_link
