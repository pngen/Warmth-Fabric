#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "warmth/identity.hpp"
#include "warmth/warmth_object.hpp"
#include "warmth/policy.hpp"

namespace warmth {

// Authoritative persistence payload. Records durable warmth knowledge
// (metadata, generations, measured cost, decay/invalidation state, policy) but
// deliberately does not persist ephemeral live handles (CUDA contexts, loaded
// modules, graph instances, allocator pools). Those are never assumed alive
// across a restart.
struct Snapshot {
    std::uint32_t format_version = 1;
    CoordinatorEpoch epoch;
    WarmthGeneration warmth_generation;
    DependencyGeneration dependency_generation;
    PolicyGeneration policy_generation;
    WarmthPolicy policy;
    std::vector<WarmthObject> objects;
};

// Serialize a snapshot to the versioned, checksummed, big-endian binary
// encoding. Throws std::invalid_argument on NaN/Inf/overflow/unsupported
// content (callers should treat these as a persistence failure).
std::vector<std::uint8_t> encode_snapshot(const Snapshot& snapshot);

// Decode and fully validate a snapshot. Returns std::nullopt on any violation
// (malformed length, truncation, checksum corruption, duplicate id/field,
// invalid enum, impossible transition, invalid generation relation, NaN/Inf,
// overflow, trailing garbage, incompatible version). On failure *out_error (if
// non-null) contains a human-readable reason.
std::optional<Snapshot> decode_snapshot(std::span<const std::uint8_t> bytes, std::string* out_error = nullptr);

// Convenience file helpers.
bool save_snapshot_file(const std::string& path, const Snapshot& snapshot, std::string* out_error = nullptr);
std::optional<Snapshot> load_snapshot_file(const std::string& path, std::string* out_error = nullptr);

} // namespace warmth
