#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "warmth/transport.hpp"
#include "warmth/protocol.hpp"
#include "warmth/identity.hpp"
#include "warmth/warmth_object.hpp"
#include "warmth/budget.hpp"
#include "warmth/policy.hpp"
#include "warmth/scheduler.hpp"

namespace warmth {

// The authoritative coordinator for warmth metadata. Owns the current
// CoordinatorEpoch, WarmthGeneration, DependencyGeneration and PolicyGeneration,
// the set of registered workers and the single source of truth for WarmthObject
// state. All authority validation lives here.
//
// Concurrency contract: network I/O (send/recv on sockets) is performed ONLY by
// the owning connection thread and is never executed while holding the global
// state mutex. The mutex guards only the authoritative metadata and worker
// registry. No durable storage, backend, transfer or blocking work is done
// under the lock.
class Coordinator {
public:
    struct Config {
        std::string host = "127.0.0.1";
        std::uint16_t port = 0;
        std::uint64_t warm_replica_limit = 0; // 0 = unlimited
    };

    explicit Coordinator(Config cfg = {});
    ~Coordinator();
    Coordinator(const Coordinator&) = delete;
    Coordinator& operator=(const Coordinator&) = delete;

    bool start();
    void stop();
    [[nodiscard]] std::uint16_t port() const noexcept;

    void run_accept_loop(std::atomic<bool>& stop);
    void handle_one_connection();

    // ---- authority generations ----
    [[nodiscard]] CoordinatorEpoch epoch() const noexcept { return epoch_.load(); }
    [[nodiscard]] WarmthGeneration warmth_generation() const;
    [[nodiscard]] DependencyGeneration dependency_generation() const;
    void bump_dependency_generation();
    void bump_warmth_generation();
    void roll_epoch();

    // ---- authoritative metadata view (for tests / CLI) ----
    std::size_t object_count() const;
    std::vector<WarmthObject> objects() const;
    std::size_t outstanding_budget() const;
    std::size_t in_flight_attempts() const;
    std::size_t registered_workers() const;
    std::optional<WarmthObject> find_object(const WarmthObjectId& id) const;
    [[nodiscard]] bool balanced() const;

    // Issue a warming command to a worker and register the in-flight attempt.
    // Returns false if no eligible worker exists or the budget is exhausted.
    bool issue_warm(const WarmthObjectId& object, WarmthAction action,
                    const WorkerId* preferred_worker = nullptr);

    // Block until all issued warming attempts have settled. This is a genuine
    // wait (not a timeout): it returns only when the coordinator observes that
    // no warming is in flight.
    void wait_until_settled();

private:
    struct AttemptInfo {
        WarmthObjectId object;
        WorkerId worker;
        std::uint64_t reservation_id = 0;
        std::uint64_t op_reservation_id = 0;
    };

    struct ConnState {
        net::TcpConnection conn;
        WorkerId id;
        WorkerBootId boot;
        NodeId node;
        bool registered = false;
        std::string backend;
        std::mutex send_mutex;
    };

    void on_connection(net::TcpConnection conn);
    void process_connection(std::shared_ptr<ConnState> cs);
    void handle_frame(std::shared_ptr<ConnState> cs, const proto::Frame& frame);
    void on_disconnect(std::shared_ptr<ConnState> cs);
    void invalidate_worker_objects_locked(const WorkerId& worker);

    bool send_frame(net::TcpConnection& conn, std::mutex& send_mutex,
                    proto::MessageType type, std::vector<std::uint8_t> payload);
    bool send_frame_plain(net::TcpConnection& conn, std::mutex& send_mutex, proto::MessageType type);

    mutable std::mutex mutex_;
    Config cfg_;
    net::TcpListener listener_;
    std::atomic<CoordinatorEpoch> epoch_{CoordinatorEpoch::initial()};
    std::atomic<std::uint64_t> warmth_gen_value_{0};
    std::atomic<std::uint64_t> dep_gen_value_{0};
    PolicyGeneration policy_gen_{PolicyGeneration(1)};
    WarmthPolicy policy_;
    std::map<WarmthObjectId, WarmthObject> objects_;
    std::map<WorkerId, std::shared_ptr<ConnState>> workers_;
    std::map<WorkerId, WorkerBootId> last_boot_;
    std::map<WorkerId, std::set<WarmthObjectId>> worker_objects_;
    BudgetTracker budget_;
    std::map<AttemptId, AttemptInfo> in_flight_;
    std::atomic<std::size_t> in_flight_count_{0};
    std::atomic<std::uint64_t> attempt_counter_{1};
    std::atomic<bool> stopping_{false};

    std::mutex warm_mutex_;
    std::condition_variable warm_cv_;

    std::vector<std::thread> threads_;
};

} // namespace warmth
