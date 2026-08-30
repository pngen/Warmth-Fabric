#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "warmth/protocol.hpp"
#include "warmth/wire.hpp"
#include "warmth/scheduler.hpp"

namespace warmth {

constexpr std::uint32_t kProtocolVersion = 1u;

// Message payloads. Each type has a fixed, documented encoding; both the
// coordinator and worker use these helpers so there is a single compatibility
// rule.

struct Hello {
    std::uint32_t protocol_version = kProtocolVersion;
    WorkerId worker;
    WorkerBootId boot;
    NodeId node;
    std::uint64_t epoch_guess = 0;
    std::string backend;
    std::vector<std::string> devices;
    std::vector<std::uint8_t> actions;
    std::vector<WarmthObject> observed;
};

struct HelloAck {
    bool accepted = false;
    std::string reason;
    std::uint64_t epoch = 0;
    std::uint64_t warmth_gen = 0;
    std::uint64_t dep_gen = 0;
    std::uint64_t policy_gen = 0;
};

struct ReportPayload {
    WorkerBootId boot;
    std::uint64_t epoch = 0;
    std::uint64_t warmth_gen = 0;
    std::uint64_t dep_gen = 0;
    std::vector<WarmthObject> objects;
};

struct WarmCmd {
    WarmthObjectId object;
    WarmthAction action = WarmthAction::INIT_ENGINE;
    std::uint64_t attempt = 0;
};

struct WarmTopic {
    std::uint64_t epoch = 0;
    std::uint64_t warmth_gen = 0;
    std::vector<WarmCmd> cmds;
};

struct WarmResult {
    WorkerBootId boot;
    std::uint64_t epoch = 0;
    std::uint64_t warmth_gen = 0;
    std::uint64_t dep_gen = 0;
    WarmthObjectId object;
    WarmthAction action = WarmthAction::INIT_ENGINE;
    std::uint64_t attempt = 0;
    bool success = false;
    double duration_ms = 0.0;
    std::uint64_t bytes = 0;
    std::string error;
};

struct ErrorPayload { std::string message; };

// ---- encoders ----
inline void encode_hello(proto::WireEncoder& w, const Hello& h) {
    w.u32(h.protocol_version);
    w.id(h.worker); w.id(h.boot); w.id(h.node);
    w.u64(h.epoch_guess);
    w.str(h.backend);
    w.u32(static_cast<std::uint32_t>(h.devices.size()));
    for (const auto& d : h.devices) w.str(d);
    w.u32(static_cast<std::uint32_t>(h.actions.size()));
    for (const auto& a : h.actions) w.u8(a);
    w.u32(static_cast<std::uint32_t>(h.observed.size()));
    for (const auto& o : h.observed) encode_object(w, o);
}
inline void encode_hello_ack(proto::WireEncoder& w, const HelloAck& a) {
    w.u8(a.accepted ? 1 : 0); w.str(a.reason);
    w.u64(a.epoch); w.u64(a.warmth_gen); w.u64(a.dep_gen); w.u64(a.policy_gen);
}
inline void encode_report(proto::WireEncoder& w, const ReportPayload& p) {
    w.id(p.boot); w.u64(p.epoch); w.u64(p.warmth_gen); w.u64(p.dep_gen);
    w.u32(static_cast<std::uint32_t>(p.objects.size()));
    for (const auto& o : p.objects) encode_object(w, o);
}
inline void encode_warm_topic(proto::WireEncoder& w, const WarmTopic& t) {
    w.u64(t.epoch); w.u64(t.warmth_gen);
    w.u32(static_cast<std::uint32_t>(t.cmds.size()));
    for (const auto& c : t.cmds) { w.id(c.object); w.u8(static_cast<std::uint8_t>(c.action)); w.u64(c.attempt); }
}
inline void encode_warm_result(proto::WireEncoder& w, const WarmResult& r) {
    w.id(r.boot); w.u64(r.epoch); w.u64(r.warmth_gen); w.u64(r.dep_gen);
    w.id(r.object); w.u8(static_cast<std::uint8_t>(r.action)); w.u64(r.attempt);
    w.u8(r.success ? 1 : 0); w.f64(r.duration_ms); w.u64(r.bytes); w.str(r.error);
}
inline void encode_error(proto::WireEncoder& w, const ErrorPayload& e) { w.str(e.message); }

// ---- decoders ----
inline bool decode_hello(proto::WireDecoder& r, Hello& h) {
    if (!r.u32(h.protocol_version)) return false;
    if (!r.id(h.worker) || !r.id(h.boot) || !r.id(h.node)) return false;
    if (!r.u64(h.epoch_guess)) return false;
    if (!r.str(h.backend)) return false;
    std::uint32_t nd = 0; if (!r.u32(nd)) return false;
    if (nd > 4096) return false;
    for (std::uint32_t i = 0; i < nd; ++i) { std::string d; if (!r.str(d)) return false; h.devices.push_back(std::move(d)); }
    std::uint32_t na = 0; if (!r.u32(na)) return false;
    if (na > 64) return false;
    for (std::uint32_t i = 0; i < na; ++i) { std::uint8_t a = 0; if (!r.u8(a)) return false; h.actions.push_back(a); }
    std::uint32_t no = 0; if (!r.u32(no)) return false;
    if (no > 4096) return false;
    for (std::uint32_t i = 0; i < no; ++i) { WarmthObject o; if (!decode_object(r, o)) return false; h.observed.push_back(std::move(o)); }
    return true;
}
inline bool decode_hello_ack(proto::WireDecoder& r, HelloAck& a) {
    std::uint8_t acc = 0; if (!r.u8(acc)) return false; a.accepted = acc != 0;
    if (!r.str(a.reason)) return false;
    if (!r.u64(a.epoch)) return false; if (!r.u64(a.warmth_gen)) return false;
    if (!r.u64(a.dep_gen)) return false; if (!r.u64(a.policy_gen)) return false;
    return true;
}
inline bool decode_report(proto::WireDecoder& r, ReportPayload& p) {
    if (!r.id(p.boot)) return false;
    if (!r.u64(p.epoch) || !r.u64(p.warmth_gen) || !r.u64(p.dep_gen)) return false;
    std::uint32_t no = 0; if (!r.u32(no)) return false;
    if (no > 8192) return false;
    for (std::uint32_t i = 0; i < no; ++i) { WarmthObject o; if (!decode_object(r, o)) return false; p.objects.push_back(std::move(o)); }
    return true;
}
inline bool decode_warm_topic(proto::WireDecoder& r, WarmTopic& t) {
    if (!r.u64(t.epoch) || !r.u64(t.warmth_gen)) return false;
    std::uint32_t nc = 0; if (!r.u32(nc)) return false;
    if (nc > 4096) return false;
    for (std::uint32_t i = 0; i < nc; ++i) {
        WarmCmd c; std::uint8_t act = 0;
        if (!r.id(c.object)) return false; if (!r.u8(act)) return false; if (!r.u64(c.attempt)) return false;
        c.action = static_cast<WarmthAction>(act);
        t.cmds.push_back(c);
    }
    return true;
}
inline bool decode_warm_result(proto::WireDecoder& r, WarmResult& res) {
    if (!r.id(res.boot)) return false;
    if (!r.u64(res.epoch) || !r.u64(res.warmth_gen) || !r.u64(res.dep_gen)) return false;
    if (!r.id(res.object)) return false;
    std::uint8_t act = 0; if (!r.u8(act)) return false; res.action = static_cast<WarmthAction>(act);
    if (!r.u64(res.attempt)) return false;
    std::uint8_t ok = 0; if (!r.u8(ok)) return false; res.success = ok != 0;
    if (!r.f64(res.duration_ms)) return false;
    if (!r.u64(res.bytes)) return false;
    if (!r.str(res.error)) return false;
    return true;
}
inline bool decode_error(proto::WireDecoder& r, ErrorPayload& e) { return r.str(e.message); }

} // namespace warmth
