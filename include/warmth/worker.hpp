#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "warmth/transport.hpp"
#include "warmth/identity.hpp"
#include "warmth/warmth_object.hpp"
#include "warmth/protocol.hpp"
#include "warmth/messages.hpp"

namespace warmth {

// Executes one warming action for an object. The hook is supplied by the
// application so Warmth Fabric stays independent of any specific backend.
class WarmHook {
public:
    virtual ~WarmHook() = default;
    virtual bool execute(const WarmthObjectId& object, WarmthAction action,
                         double& duration_ms, std::uint64_t& bytes) = 0;
};

// A deterministic synthetic hook used in demos/tests where no real backend is
// configured. It reports a small fixed measured cost and succeeds.
class SyntheticWarmHook : public WarmHook {
public:
    bool execute(const WarmthObjectId& object, WarmthAction action,
                 double& duration_ms, std::uint64_t& bytes) override;
};

// A worker that connects to the coordinator, registers, and services warming
// commands over the framed transport.
class Worker {
public:
    struct Config {
        std::string host = "127.0.0.1";
        std::uint16_t port = 0;
        WorkerId worker_id;
        WorkerBootId boot;
        NodeId node;
        WorkloadId workload;
        std::string backend = "cuda";
        std::vector<std::string> devices;
        std::vector<std::uint8_t> actions;
    };

    explicit Worker(Config cfg) : cfg_(std::move(cfg)) {}

    // Connect and send HELLO. Returns true on an accepted registration.
    bool connect();
    // The blocking receive/process loop. Returns when the connection closes or a
    // SHUTDOWN is received. The hook must be kept alive for the duration.
    void run(WarmHook& hook);

    void request_shutdown();

private:
    void handle_frame(const proto::Frame& frame, WarmHook& hook);
    void send_warm_result(const WarmResult& res);

    Config cfg_;
    net::TcpConnection conn_;
    std::map<WarmthObjectId, WarmthObject> observed_;
    std::uint64_t epoch_ = 0;
    std::uint64_t warmth_gen_ = 0;
    std::uint64_t dep_gen_ = 0;
    std::uint64_t policy_gen_ = 0;
    bool shutdown_requested_ = false;
};

} // namespace warmth
