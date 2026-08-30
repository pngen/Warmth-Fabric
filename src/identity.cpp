// Warmth Fabric - src/identity.cpp
// Copyright 2026 Summon Software Labs
// SPDX-License-Identifier: Apache-2.0
#include "warmth/identity.hpp"

#include <chrono>
#include <random>
#include <array>
#include <cstdio>

namespace warmth {

namespace {
std::mt19937_64& rng() {
    static std::mt19937_64 engine = [] {
        std::random_device rd;
        const std::uint64_t a = static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        const std::uint64_t b = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        const std::uint64_t seed = a ^ (b << 21) ^ static_cast<std::uint64_t>(rd()) ^ (static_cast<std::uint64_t>(rd()) << 32);
        return std::mt19937_64(seed);
    }();
    return engine;
}
} // namespace

Id128 Id128::random() {
    std::uint64_t hi = rng()();
    std::uint64_t lo = rng()();
    if (hi == 0 && lo == 0) lo = 1; // ensure valid (non-zero)
    return Id128(hi, lo);
}

std::string Id128::to_string() const {
    char buf[33];
    std::snprintf(buf, sizeof(buf), "%.016llx%.016llx",
                  static_cast<unsigned long long>(hi_),
                  static_cast<unsigned long long>(lo_));
    return std::string(buf, 32);
}

std::optional<Id128> Id128::from_string(std::string_view s) {
    std::string_view hex = s;
    if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex.remove_prefix(2);
    if (hex.size() != 32) return std::nullopt;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::uint64_t hi = 0, lo = 0;
    for (std::size_t i = 0; i < 16; ++i) {
        const int nh = nib(hex[i]);
        const int nl = nib(hex[i + 16]);
        if (nh < 0 || nl < 0) return std::nullopt;
        hi = (hi << 4) | static_cast<std::uint64_t>(nh);
        lo = (lo << 4) | static_cast<std::uint64_t>(nl);
    }
    return Id128(hi, lo);
}

} // namespace warmth
