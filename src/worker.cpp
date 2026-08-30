// Warmth Fabric - src/worker.cpp
// Copyright 2026 Summon Software Labs
// SPDX-License-Identifier: Apache-2.0
#include "warmth/worker.hpp"

#include <utility>

#include "warmth/messages.hpp"

namespace warmth {

bool SyntheticWarmHook::execute(const WarmthObjectId& object, WarmthAction action,
                                double& duration_ms, std::uint64_t& bytes) {
    (void)object;
    bytes = 1ULL << 28;
    duration_ms = 0.5 + 0.25 * static_cast<int>(action);
    return true;
}

bool Worker::connect() {
    conn_ = net::connect(cfg_.host, cfg_.port);
    if (!conn_.valid()) return false;

    Hello h;
    h.worker = cfg_.worker_id; h.boot = cfg_.boot; h.node = cfg_.node;
    h.epoch_guess = 0; h.backend = cfg_.backend;
    h.devices = cfg_.devices; h.actions = cfg_.actions;
    for (const auto& kv : observed_) h.observed.push_back(kv.second);

    proto::WireEncoder w; encode_hello(w, h);
    proto::Frame f; f.type = proto::MessageType::HELLO; f.payload = w.done();
    proto::FrameWriter fw(conn_);
    if (!fw.write(f)) return false;

    proto::FrameReader fr(conn_);
    proto::Frame reply;
    if (!fr.read(reply)) return false;
    if (reply.type != proto::MessageType::HELLO_ACK) return false;
    proto::WireDecoder dec(reply.payload);
    HelloAck ack;
    if (!decode_hello_ack(dec, ack)) return false;
    if (!ack.accepted) return false;
    epoch_ = ack.epoch; warmth_gen_ = ack.warmth_gen; dep_gen_ = ack.dep_gen; policy_gen_ = ack.policy_gen;
    return true;
}

void Worker::run(WarmHook& hook) {
    proto::FrameReader fr(conn_);
    for (;;) {
        proto::Frame f;
        if (!fr.read(f)) return;
        handle_frame(f, hook);
        if (shutdown_requested_) return;
    }
}

void Worker::request_shutdown() {
    shutdown_requested_ = true;
    if (conn_.valid()) {
        proto::Frame f; f.type = proto::MessageType::SHUTDOWN;
        proto::FrameWriter fw(conn_);
        fw.write(f);
    }
}

void Worker::handle_frame(const proto::Frame& frame, WarmHook& hook) {
    proto::WireDecoder dec(frame.payload);
    switch (frame.type) {
        case proto::MessageType::WARM_TOPIC: {
            WarmTopic topic;
            if (!decode_warm_topic(dec, topic)) return;
            for (const auto& cmd : topic.cmds) {
                WarmResult res;
                res.boot = cfg_.boot;
                res.epoch = epoch_; res.warmth_gen = warmth_gen_; res.dep_gen = dep_gen_;
                res.object = cmd.object; res.action = cmd.action; res.attempt = cmd.attempt;
                double duration = 0.0; std::uint64_t bytes = 0;
                res.success = hook.execute(cmd.object, cmd.action, duration, bytes);
                res.duration_ms = duration; res.bytes = bytes;
                if (!res.success) res.error = "warming_hook_failed";
                if (res.success) {
                    auto it = observed_.find(cmd.object);
                    if (it == observed_.end()) {
                        WarmthObject o(cmd.object, WarmthCategory::APPLICATION_DEFINED, cfg_.workload, cfg_.node, "cuda:0", cfg_.backend);
                        o.transition(WarmthState::WARM, "warm_completed");
                        o.set_warmth_generation(WarmthGeneration(warmth_gen_));
                        o.set_measured_cost_bytes(bytes);
                        o.set_observed_ttr_ms(duration);
                        o.mark_prepared(now_ms()); o.mark_used(now_ms());
                        o.set_provenance(Provenance::MEASURED);
                        o.set_residency(ResidencyState::DEVICE_RESIDENT);
                        observed_[cmd.object] = std::move(o);
                    } else {
                        WarmthObject& o = it->second;
                        o.transition(WarmthState::WARM, "warm_completed");
                        o.set_measured_cost_bytes(bytes);
                        o.set_observed_ttr_ms(duration);
                        o.mark_used(now_ms());
                        o.set_provenance(Provenance::MEASURED);
                    }
                }
                send_warm_result(res);
            }
            return;
        }
        case proto::MessageType::PING: {
            proto::Frame pong; pong.type = proto::MessageType::PONG;
            proto::FrameWriter fw(conn_); fw.write(pong);
            return;
        }
        case proto::MessageType::SHUTDOWN:
            shutdown_requested_ = true;
            return;
        case proto::MessageType::ERROR:
            return;
        default:
            return;
    }
}

void Worker::send_warm_result(const WarmResult& res) {
    proto::WireEncoder w; encode_warm_result(w, res);
    proto::Frame f; f.type = proto::MessageType::WARM_RESULT; f.payload = w.done();
    proto::FrameWriter fw(conn_);
    fw.write(f);
}

} // namespace warmth
