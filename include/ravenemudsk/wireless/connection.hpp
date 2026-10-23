// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ravenemudsk/wireless/net.hpp>
#include <ravenemudsk/wireless/protocol.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

// One TCP connection of the wireless link cable, to another device.
//
// One thread reads frames and another writes them: send_core() and
// send_control() never block, since RavenCore calls them from the emulation.
// With nothing to send, the writer sends a Ping every second; with nothing
// received for five seconds, the connection closes.
//
// Console messages received before bind_core() wait, in order: the other
// device may plug its cable in before this one is ready.
namespace ravenemudsk_link {
class Connection {
public:
    using Clock = std::chrono::steady_clock;
    struct Listener {
        // Reading thread: a control message, heartbeats excepted.
        std::function<void(Connection&, const Control&)> control;
        // Reading thread: a console message, once bound.
        std::function<void(Connection&)> core;
        // Once, from any thread.
        std::function<void(Connection&)> closed;
    };
    static constexpr auto ping_every = std::chrono::seconds(1);
    static constexpr auto timeout = std::chrono::seconds(5);

    Connection(ravenemudsk_net::Socket socket, bool outgoing);
    // Closes (after a goodbye: lets the other side read it, two seconds at
    // most), then waits for both threads: never from inside a listener.
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    void start(Listener listener);
    void send_core(std::span<const std::uint8_t> message);
    void send_control(const Control& message);
    // Console messages go to `receiver` from now on, the waiting ones first;
    // an empty function stops them (and waits for one in progress).
    void bind_core(std::function<void(std::span<const std::uint8_t>)> receiver);
    // Says goodbye, then closes once the other side has read it.
    void leave(bool refused);
    void close();
    bool closed() const { return closed_; }
    bool outgoing() const { return outgoing_; }
    Clock::time_point opened() const { return opened_; }

    // The other device's introduction, once received (owner's thread only).
    std::optional<Hello> peer;
    // The device this computer called, known before its introduction.
    std::optional<std::uint64_t> target;

private:
    void read_loop();
    void write_loop();
    void enqueue(std::vector<std::uint8_t> frame_bytes);
    void deliver_core(std::vector<std::uint8_t> message);

    ravenemudsk_net::Socket socket_;
    const bool outgoing_;
    const Clock::time_point opened_ = Clock::now();
    Listener listener_;
    std::atomic<bool> closed_{false};
    std::atomic<bool> leaving_{false};
    std::atomic<std::int64_t> left_at_{0};
    std::mutex queue_mutex_;
    std::condition_variable queue_ready_;
    std::deque<std::vector<std::uint8_t>> queue_;
    bool end_ = false; // goodbye queued: the writer stops after it
    std::mutex core_mutex_;
    std::vector<std::vector<std::uint8_t>> early_;
    std::function<void(std::span<const std::uint8_t>)> receiver_;
    std::thread reader_, writer_;
};
} // namespace ravenemudsk_link
