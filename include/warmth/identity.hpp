#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <functional>
#include <optional>
#include <compare>
#include <array>
#include <limits>

#include "warmth/detail/hash.hpp"

namespace warmth {

class Id128 {
public:
    constexpr Id128() noexcept = default;
    constexpr Id128(std::uint64_t hi, std::uint64_t lo) noexcept : hi_(hi), lo_(lo) {}

    [[nodiscard]] bool is_valid() const noexcept { return hi_ != 0 || lo_ != 0; }
    [[nodiscard]] constexpr std::uint64_t high() const noexcept { return hi_; }
    [[nodiscard]] constexpr std::uint64_t low() const noexcept { return lo_; }

    static Id128 derive(std::string_view text) {
        const auto d = detail::digest16(text);
        std::uint64_t hi = 0, lo = 0;
        for (int i = 0; i < 8; ++i) { hi = (hi << 8) | d[static_cast<std::size_t>(i)]; }
        for (int i = 8; i < 16; ++i) { lo = (lo << 8) | d[static_cast<std::size_t>(i)]; }
        return Id128(hi, lo);
    }

    static Id128 random();

    static std::optional<Id128> from_string(std::string_view s);

    [[nodiscard]] std::string to_string() const;

    std::array<std::uint8_t, 16> to_bytes() const noexcept {
        std::array<std::uint8_t, 16> b{};
        for (int i = 0; i < 8; ++i) b[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(hi_ >> (56 - i * 8));
        for (int i = 0; i < 8; ++i) b[static_cast<std::size_t>(8 + i)] = static_cast<std::uint8_t>(lo_ >> (56 - i * 8));
        return b;
    }

    static Id128 from_bytes(const std::uint8_t* b) noexcept {
        std::uint64_t hi = 0, lo = 0;
        for (int i = 0; i < 8; ++i) hi = (hi << 8) | b[static_cast<std::size_t>(i)];
        for (int i = 8; i < 16; ++i) lo = (lo << 8) | b[static_cast<std::size_t>(i)];
        return Id128(hi, lo);
    }

    friend constexpr bool operator==(const Id128&, const Id128&) = default;
    friend constexpr auto operator<=>(const Id128&, const Id128&) = default;

private:
    std::uint64_t hi_ = 0;
    std::uint64_t lo_ = 0;
};

inline std::size_t hash_value(const Id128& id) {
    std::uint64_t x = id.high();
    x ^= id.low() + 0x9e3779b97f4a7c15ULL + (x << 6) + (x >> 2);
    return static_cast<std::size_t>(x);
}

template <typename Tag>
class BasicGeneration {
public:
    constexpr BasicGeneration() noexcept = default;
    explicit constexpr BasicGeneration(std::uint64_t v) noexcept : value_(v) {}
    static constexpr BasicGeneration initial() noexcept { return BasicGeneration(0); }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != 0; }

    constexpr BasicGeneration next() const noexcept {
        std::uint64_t v = value_ == (std::numeric_limits<std::uint64_t>::max)() ? value_ : value_ + 1;
        return BasicGeneration(v);
    }

    friend constexpr bool operator==(const BasicGeneration&, const BasicGeneration&) = default;
    friend constexpr auto operator<=>(const BasicGeneration&, const BasicGeneration&) = default;

private:
    std::uint64_t value_ = 0;
};

struct WarmthGenerationTag {};
struct DependencyGenerationTag {};
struct PolicyGenerationTag {};
struct CoordinatorEpochTag {};
struct ReplicaGenerationTag {};
struct ModelGenerationTag {};
struct KernelGenerationTag {};
struct GraphGenerationTag {};
struct AttemptIdTag {};

using WarmthGeneration      = BasicGeneration<WarmthGenerationTag>;
using DependencyGeneration  = BasicGeneration<DependencyGenerationTag>;
using PolicyGeneration      = BasicGeneration<PolicyGenerationTag>;
using CoordinatorEpoch      = BasicGeneration<CoordinatorEpochTag>;
using ReplicaGeneration     = BasicGeneration<ReplicaGenerationTag>;
using ModelGeneration       = BasicGeneration<ModelGenerationTag>;
using KernelGeneration      = BasicGeneration<KernelGenerationTag>;
using GraphGeneration       = BasicGeneration<GraphGenerationTag>;
using AttemptId             = BasicGeneration<AttemptIdTag>;

using WarmthObjectId = Id128;
using WorkloadId     = Id128;
using TenantId       = Id128;
using ModelId        = Id128;
using ArtifactId     = Id128;
using AdapterSetId   = Id128;
using KernelSetId    = Id128;
using GraphSetId     = Id128;
using PrefixStateId  = Id128;
using ReplicaId      = Id128;
using EngineId       = Id128;
using NodeId         = Id128;
using WorkerId       = Id128;
using WorkerBootId   = Id128;

} // namespace warmth

namespace std {
template <> struct hash<warmth::Id128> {
    std::size_t operator()(const warmth::Id128& id) const noexcept { return warmth::hash_value(id); }
};
} // namespace std
