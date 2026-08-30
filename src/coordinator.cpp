// Warmth Fabric - src/coordinator.cpp
// Copyright 2026 Summon Software Labs
// SPDX-License-Identifier: Apache-2.0
#include "warmth/coordinator.hpp"

#include <utility>
#include <algorithm>
#include <cstdio>

#include "warmth/messages.hpp"

namespace warmth {

namespace {
constexpr std::uint64_t kDefaultWarmBytes = 1ULL << 30; // 1 GiB estimate

// Move an object to WARM from any state, going through PREPARING (falling back
// through COLD) so the guarded state machine is always respected.
void reach_warm(WarmthObject& o, const char* why) {
    if (o.state() == WarmthState::WARM || o.state() == WarmthState::HOT) return;
    if (!o.transition(WarmthState::PREPARING, why)) {
        (void)o.transition(WarmthState::COLD, why);
        (void)o.transition(WarmthState::PREPARING, why);
    }
    (void)o.transition(WarmthState::WARM, why);
}
} // namespace

Coordinator::Coordinator(Config cfg) : cfg_(std::move(cfg)) {
    policy_ = WarmthPolicy::defaults(PolicyGeneration(1));
    policy_gen_ = PolicyGeneration(1);
}

Coordinator::~Coordinator() { stop(); }

bool Coordinator::start() { return listener_.listen(cfg_.host, cfg_.port); }

void Coordinator::stop() {
    if (stopping_.exchange(true)) return;
    listener_.close();
    // Close every live worker connection so the per-connection threads unblock.
    {
        std::lock_guard<std::mutex> g(mutex_);
        std::vector<net::TcpConnection*> conns;
        for (auto& kv : workers_) conns.push_back(&kv.second->conn);
        for (auto* c : conns) if (c && c->valid()) c->close();
    }
    for (auto& t : threads_) if (t.joinable()) t.join();
    threads_.clear();
}

std::uint16_t Coordinator::port() const noexcept { return listener_.port(); }

void Coordinator::run_accept_loop(std::atomic<bool>& stop) {
    while (!stop.load()) {
        auto conn = listener_.accept();
        if (!conn) { if (stop.load() || stopping_.load()) break; continue; }
        on_connection(std::move(*conn));
    }
}

void Coordinator::handle_one_connection() {
    auto conn = listener_.accept();
    if (!conn) return;
    on_connection(std::move(*conn));
}

void Coordinator::on_connection(net::TcpConnection conn) {
    auto cs = std::make_shared<ConnState>();
    cs->conn = std::move(conn);
    threads_.emplace_back([this, cs] { process_connection(cs); });
    // Detach is not used; threads are joined in stop(). A thread that finishes
    // leaves a joinable entry that stop() will join.
}

WarmthGeneration Coordinator::warmth_generation() const { return WarmthGeneration(warmth_gen_value_.load()); }
DependencyGeneration Coordinator::dependency_generation() const { return DependencyGeneration(dep_gen_value_.load()); }

void Coordinator::bump_dependency_generation() {
    std::lock_guard<std::mutex> g(mutex_);
    dep_gen_value_.store(dep_gen_value_.load() + 1);
}
void Coordinator::bump_warmth_generation() {
    std::lock_guard<std::mutex> g(mutex_);
    warmth_gen_value_.store(warmth_gen_value_.load() + 1);
}
void Coordinator::roll_epoch() {
    std::lock_guard<std::mutex> g(mutex_);
    epoch_.store(epoch_.load().next());
    warmth_gen_value_.store(warmth_gen_value_.load() + 1);
    dep_gen_value_.store(dep_gen_value_.load() + 1);
    for (auto& kv : objects_) {
        WarmthObject& o = kv.second;
        if (is_live_warm_state(o.state()) || o.state() == WarmthState::STALE) {
            o.transition(WarmthState::INVALIDATED, "authority roll");
        }
        o.set_invalidation_reason("coordinator_epoch_rolled");
        o.set_dimension(DimensionIndex::CudaContextReadiness, DimensionStatus::COLD);
        o.set_dimension(DimensionIndex::KernelReadiness, DimensionStatus::COLD);
        o.set_dimension(DimensionIndex::GraphReadiness, DimensionStatus::COLD);
        o.set_dimension(DimensionIndex::AllocatorReadiness, DimensionStatus::COLD);
    }
    for (const auto& kv : in_flight_) {
        budget_.release(kv.second.reservation_id);
        if (kv.second.op_reservation_id) budget_.release(kv.second.op_reservation_id);
    }
    if (!in_flight_.empty()) in_flight_count_.store(0);
    in_flight_.clear();
    warm_cv_.notify_all();
}

std::size_t Coordinator::object_count() const { std::lock_guard<std::mutex> g(mutex_); return objects_.size(); }
std::vector<WarmthObject> Coordinator::objects() const {
    std::lock_guard<std::mutex> g(mutex_);
    std::vector<WarmthObject> v; v.reserve(objects_.size());
    for (const auto& kv : objects_) v.push_back(kv.second);
    return v;
}
std::size_t Coordinator::outstanding_budget() const { std::lock_guard<std::mutex> g(mutex_); return budget_.outstanding(); }
std::size_t Coordinator::in_flight_attempts() const { std::lock_guard<std::mutex> g(mutex_); return in_flight_.size(); }
std::size_t Coordinator::registered_workers() const { std::lock_guard<std::mutex> g(mutex_); return workers_.size(); }
bool Coordinator::balanced() const { std::lock_guard<std::mutex> g(mutex_); return budget_.outstanding() == 0; }
std::optional<WarmthObject> Coordinator::find_object(const WarmthObjectId& id) const {
    std::lock_guard<std::mutex> g(mutex_);
    auto it = objects_.find(id);
    if (it == objects_.end()) return std::nullopt;
    return it->second;
}

void Coordinator::process_connection(std::shared_ptr<ConnState> cs) {
    proto::FrameReader reader(cs->conn);
    for (;;) {
        proto::Frame f;
        if (!reader.read(f)) { on_disconnect(cs); return; }
        handle_frame(cs, f);
    }
}

bool Coordinator::send_frame(net::TcpConnection& conn, std::mutex& send_mutex,
                             proto::MessageType type, std::vector<std::uint8_t> payload) {
    std::lock_guard<std::mutex> g(send_mutex);
    proto::Frame f; f.type = type; f.payload = std::move(payload);
    proto::FrameWriter fw(conn);
    return fw.write(f);
}
bool Coordinator::send_frame_plain(net::TcpConnection& conn, std::mutex& send_mutex, proto::MessageType type) {
    return send_frame(conn, send_mutex, type, {});
}

void Coordinator::handle_frame(std::shared_ptr<ConnState> cs, const proto::Frame& frame) {
    proto::WireDecoder dec(frame.payload);
    switch (frame.type) {
        case proto::MessageType::HELLO: {
            Hello h;
            if (!decode_hello(dec, h)) { send_frame_plain(cs->conn, cs->send_mutex, proto::MessageType::ERROR); return; }
            if (h.protocol_version != kProtocolVersion) {
                HelloAck a; a.accepted = false; a.reason = "protocol version mismatch";
                proto::WireEncoder w; encode_hello_ack(w, a);
                send_frame(cs->conn, cs->send_mutex, proto::MessageType::HELLO_ACK, w.done());
                return;
            }
            HelloAck ack;
            {
                std::lock_guard<std::mutex> g(mutex_);
                auto wit = workers_.find(h.worker);
                if (wit != workers_.end() && wit->second->boot != h.boot) {
                    // Stale/obsolete boot: the worker id is already live with a different
                    // boot. Reject rather than silently re-registering stale authority.
                    ack.accepted = false;
                    ack.reason = "stale boot (worker already registered)";
                } else {
                    auto it = last_boot_.find(h.worker);
                    if (it != last_boot_.end() && it->second != h.boot)
                        invalidate_worker_objects_locked(h.worker);
                    last_boot_[h.worker] = h.boot;
                    cs->id = h.worker; cs->boot = h.boot; cs->node = h.node;
                    cs->backend = h.backend; cs->registered = true;
                    workers_[h.worker] = cs;
                    for (const auto& o : h.observed) {
                        auto oit = objects_.find(o.id());
                        if (oit == objects_.end()) {
                            WarmthObject copy = o;
                            if (is_execution_ready(copy.state())) copy.transition(WarmthState::DISCOVERED, "observed-not-live");
                            copy.set_provenance(Provenance::REPORTED);
                            objects_[copy.id()] = std::move(copy);
                        }
                        worker_objects_[h.worker].insert(o.id());
                    }
                    ack.accepted = true;
                }
            }
            ack.epoch = epoch_.load().value(); ack.warmth_gen = warmth_gen_value_.load();
            ack.dep_gen = dep_gen_value_.load(); ack.policy_gen = policy_gen_.value();
            proto::WireEncoder w; encode_hello_ack(w, ack);
            send_frame(cs->conn, cs->send_mutex, proto::MessageType::HELLO_ACK, w.done());
            return;
        }
        case proto::MessageType::REPORT: {
            ReportPayload p;
            if (!decode_report(dec, p)) return;
            bool reject = false;
            {
                std::lock_guard<std::mutex> g(mutex_);
                if (!cs->registered || cs->boot != p.boot ||
                    p.epoch != epoch_.load().value() ||
                    p.warmth_gen != warmth_gen_value_.load() ||
                    p.dep_gen != dep_gen_value_.load()) {
                    reject = true;
                } else {
                    for (const auto& o : p.objects) {
                        auto oit = objects_.find(o.id());
                        if (oit == objects_.end()) {
                            WarmthObject copy = o;
                            if (is_execution_ready(copy.state())) copy.transition(WarmthState::DISCOVERED, "observed-not-live");
                            copy.set_provenance(Provenance::REPORTED);
                            objects_[copy.id()] = std::move(copy);
                        } else {
                            WarmthObject& cur = oit->second;
                            cur.set_dimensions(o.dimensions());
                            cur.mark_used(now_ms());
                            cur.set_observed_ttr_ms(o.observed_ttr_ms());
                            cur.set_residency(o.residency());
                            cur.set_provenance(Provenance::REPORTED);
                            if (is_execution_ready(o.state()) && !is_execution_ready(cur.state())) {
                                cur.transition(WarmthState::DISCOVERED, "observed-not-live");
                            }
                            // No state promotion to execution-ready from a report alone.
                        }
                        worker_objects_[cs->id].insert(o.id());
                    }
                }
            }
            if (reject) send_frame_plain(cs->conn, cs->send_mutex, proto::MessageType::ERROR);
            return;
        }
        case proto::MessageType::WARM_RESULT: {
            WarmResult res;
            if (!decode_warm_result(dec, res)) return;
            bool notify = false;
            {
                std::lock_guard<std::mutex> g(mutex_);
                if (cs->registered && cs->boot == res.boot &&
                    res.epoch == epoch_.load().value() &&
                    res.warmth_gen == warmth_gen_value_.load() &&
                    res.dep_gen == dep_gen_value_.load()) {
                    auto ait = in_flight_.find(AttemptId(res.attempt));
                    if (ait != in_flight_.end() && ait->second.worker == cs->id && ait->second.object == res.object) {
                        const auto& info = ait->second;
                        budget_.release(info.reservation_id);
                        if (info.op_reservation_id) budget_.release(info.op_reservation_id);
                        if (res.success) {
                            auto oit = objects_.find(res.object);
                            if (oit == objects_.end()) {
                                WarmthObject o(res.object, WarmthCategory::APPLICATION_DEFINED, WorkloadId{}, NodeId{}, "cuda:0", "cuda");
                                reach_warm(o, "warm_completed");
                                o.set_warmth_generation(WarmthGeneration(warmth_gen_value_.load()));
                                o.set_measured_cost_bytes(res.bytes);
                                o.set_observed_ttr_ms(res.duration_ms);
                                o.mark_prepared(now_ms()); o.mark_used(now_ms());
                                o.set_provenance(Provenance::MEASURED);
                                o.set_residency(ResidencyState::DEVICE_RESIDENT);
                                objects_[res.object] = std::move(o);
                            } else {
                                WarmthObject& o = oit->second;
                                if (o.state() == WarmthState::INVALIDATED) {
                                    o.transition(WarmthState::DISCOVERED, "invalidated");
                                    o.clear_invalidation_reason();
                                }
                                reach_warm(o, "warm_completed");
                                o.set_warmth_generation(WarmthGeneration(warmth_gen_value_.load()));
                                o.set_measured_cost_bytes(res.bytes);
                                o.set_observed_ttr_ms(res.duration_ms);
                                o.mark_prepared(now_ms()); o.mark_used(now_ms());
                                o.set_provenance(Provenance::MEASURED);
                                o.set_residency(ResidencyState::DEVICE_RESIDENT);
                            }
                            worker_objects_[cs->id].insert(res.object);
                        } else {
                            auto oit = objects_.find(res.object);
                            if (oit != objects_.end()) oit->second.transition(WarmthState::FAILED, res.error);
                        }
                        in_flight_.erase(ait);
                        if (in_flight_count_.load() > 0) in_flight_count_.fetch_sub(1);
                        notify = true;
                    }
                }
            }
            if (notify) warm_cv_.notify_all();
            return;
        }
        case proto::MessageType::PING:
            send_frame_plain(cs->conn, cs->send_mutex, proto::MessageType::PONG);
            return;
        case proto::MessageType::SHUTDOWN:
            return;
        default:
            return;
    }
}

void Coordinator::on_disconnect(std::shared_ptr<ConnState> cs) {
    std::lock_guard<std::mutex> g(mutex_);
    if (!cs->registered) return;
    auto it = workers_.find(cs->id);
    if (it != workers_.end() && it->second == cs) workers_.erase(it);
    auto oit = worker_objects_.find(cs->id);
    if (oit != worker_objects_.end()) {
        for (const auto& oid : oit->second) {
            auto obj = objects_.find(oid);
            if (obj == objects_.end()) continue;
            WarmthObject& o = obj->second;
            const auto next = decay_object(o, policy_.decay, now_ms(), false, false, false, true);
            if (next.changed) o.transition(next.state, "worker_disconnect");
            o.set_dimension(DimensionIndex::CudaContextReadiness, DimensionStatus::SUSPECT);
            o.set_dimension(DimensionIndex::KernelReadiness, DimensionStatus::SUSPECT);
            o.set_dimension(DimensionIndex::GraphReadiness, DimensionStatus::SUSPECT);
            o.set_dimension(DimensionIndex::AllocatorReadiness, DimensionStatus::SUSPECT);
        }
    }
}

void Coordinator::invalidate_worker_objects_locked(const WorkerId& worker) {
    auto oit = worker_objects_.find(worker);
    if (oit == worker_objects_.end()) return;
    for (const auto& oid : oit->second) {
        auto obj = objects_.find(oid);
        if (obj == objects_.end()) continue;
        WarmthObject& o = obj->second;
        if (is_live_warm_state(o.state()) || o.state() == WarmthState::STALE) {
            o.transition(WarmthState::INVALIDATED, "worker_boot_changed");
        }
        o.set_invalidation_reason("worker_boot_changed");
        o.set_dimension(DimensionIndex::CudaContextReadiness, DimensionStatus::COLD);
        o.set_dimension(DimensionIndex::KernelReadiness, DimensionStatus::COLD);
        o.set_dimension(DimensionIndex::GraphReadiness, DimensionStatus::COLD);
        o.set_dimension(DimensionIndex::AllocatorReadiness, DimensionStatus::COLD);
    }
    oit->second.clear();
}

bool Coordinator::issue_warm(const WarmthObjectId& object, WarmthAction action, const WorkerId* preferred_worker) {
    std::shared_ptr<ConnState> target;
    AttemptId attempt;
    std::uint64_t reservation = 0, op_reservation = 0;
    WarmTopic topic;
    {
        std::lock_guard<std::mutex> g(mutex_);
        if (workers_.empty()) return false;
        if (preferred_worker) {
            auto pit = workers_.find(*preferred_worker);
            if (pit == workers_.end() || !pit->second->registered) return false;
            target = pit->second;
        } else {
            target = workers_.begin()->second;
            if (!target->registered) return false;
        }
        BudgetError err;
        reservation = budget_.reserve(WarmthBudgetKind::DEVICE_MEMORY, kDefaultWarmBytes, "warm", &err);
        if (reservation == 0) return false; // budget exhausted -> infeasible
        op_reservation = budget_.reserve(WarmthBudgetKind::CONCURRENT_WARMING_OPS, 1, "warm", &err);
        attempt = AttemptId(attempt_counter_.fetch_add(1));
        in_flight_[attempt] = AttemptInfo{object, target->id, reservation, op_reservation};
        in_flight_count_.fetch_add(1);
        topic.epoch = epoch_.load().value();
        topic.warmth_gen = warmth_gen_value_.load();
        WarmCmd cmd; cmd.object = object; cmd.action = action; cmd.attempt = attempt.value();
        topic.cmds.push_back(cmd);
    }
    proto::WireEncoder w; encode_warm_topic(w, topic);
    const bool sent = send_frame(target->conn, target->send_mutex, proto::MessageType::WARM_TOPIC, w.done());
    if (!sent) {
        std::lock_guard<std::mutex> g(mutex_);
        auto it = in_flight_.find(attempt);
        if (it != in_flight_.end()) {
            budget_.release(it->second.reservation_id);
            if (it->second.op_reservation_id) budget_.release(it->second.op_reservation_id);
            in_flight_.erase(it);
            if (in_flight_count_.load() > 0) in_flight_count_.fetch_sub(1);
        }
        warm_cv_.notify_all();
        return false;
    }
    return true;
}

void Coordinator::wait_until_settled() {
    std::unique_lock<std::mutex> lk(warm_mutex_);
    warm_cv_.wait(lk, [this] { return in_flight_count_.load() == 0; });
}

} // namespace warmth
