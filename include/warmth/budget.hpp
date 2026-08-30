#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "warmth/detail/macros.hpp"

namespace warmth {

enum class WarmthBudgetKind : std::uint8_t {
    DEVICE_MEMORY = 0,
    PINNED_HOST_MEMORY = 1,
    HOST_MEMORY = 2,
    STORAGE_FOOTPRINT = 3,
    CONCURRENT_WARMING_OPS = 4,
    TRANSFER_BANDWIDTH = 5,
    ACTIVE_ENGINES = 6,
    WARM_REPLICAS = 7
};

constexpr const char* budget_kind_name(WarmthBudgetKind k) noexcept {
    switch (k) {
        case WarmthBudgetKind::DEVICE_MEMORY:          return "device_memory";
        case WarmthBudgetKind::PINNED_HOST_MEMORY:     return "pinned_host_memory";
        case WarmthBudgetKind::HOST_MEMORY:            return "host_memory";
        case WarmthBudgetKind::STORAGE_FOOTPRINT:      return "storage_footprint";
        case WarmthBudgetKind::CONCURRENT_WARMING_OPS: return "concurrent_warming_ops";
        case WarmthBudgetKind::TRANSFER_BANDWIDTH:     return "transfer_bandwidth";
        case WarmthBudgetKind::ACTIVE_ENGINES:         return "active_engines";
        case WarmthBudgetKind::WARM_REPLICAS:          return "warm_replicas";
    }
    return "budget";
}

struct BudgetLimits {
    std::uint64_t device_memory_bytes       = 0;
    std::uint64_t pinned_host_memory_bytes  = 0;
    std::uint64_t host_memory_bytes         = 0;
    std::uint64_t storage_footprint_bytes   = 0;
    std::uint64_t concurrent_warming_ops    = 0;
    std::uint64_t transfer_bandwidth_bps    = 0;
    std::uint64_t active_engines            = 0;
    std::uint64_t warm_replicas             = 0;

    void set(WarmthBudgetKind kind, std::uint64_t value) {
        switch (kind) {
            case WarmthBudgetKind::DEVICE_MEMORY:          device_memory_bytes = value; break;
            case WarmthBudgetKind::PINNED_HOST_MEMORY:     pinned_host_memory_bytes = value; break;
            case WarmthBudgetKind::HOST_MEMORY:            host_memory_bytes = value; break;
            case WarmthBudgetKind::STORAGE_FOOTPRINT:      storage_footprint_bytes = value; break;
            case WarmthBudgetKind::CONCURRENT_WARMING_OPS: concurrent_warming_ops = value; break;
            case WarmthBudgetKind::TRANSFER_BANDWIDTH:     transfer_bandwidth_bps = value; break;
            case WarmthBudgetKind::ACTIVE_ENGINES:         active_engines = value; break;
            case WarmthBudgetKind::WARM_REPLICAS:          warm_replicas = value; break;
        }
    }
    [[nodiscard]] std::uint64_t get(WarmthBudgetKind kind) const noexcept {
        switch (kind) {
            case WarmthBudgetKind::DEVICE_MEMORY:          return device_memory_bytes;
            case WarmthBudgetKind::PINNED_HOST_MEMORY:     return pinned_host_memory_bytes;
            case WarmthBudgetKind::HOST_MEMORY:            return host_memory_bytes;
            case WarmthBudgetKind::STORAGE_FOOTPRINT:      return storage_footprint_bytes;
            case WarmthBudgetKind::CONCURRENT_WARMING_OPS: return concurrent_warming_ops;
            case WarmthBudgetKind::TRANSFER_BANDWIDTH:     return transfer_bandwidth_bps;
            case WarmthBudgetKind::ACTIVE_ENGINES:         return active_engines;
            case WarmthBudgetKind::WARM_REPLICAS:          return warm_replicas;
        }
        return 0;
    }
};

enum class BudgetError : std::uint8_t {
    NONE = 0,
    LIMIT_EXCEEDED = 1,
    UNKNOWN_RESERVATION = 2,
    DOUBLE_RELEASE = 3,
    OVERFLOW = 4
};

constexpr const char* budget_error_name(BudgetError e) noexcept {
    switch (e) {
        case BudgetError::NONE:                return "NONE";
        case BudgetError::LIMIT_EXCEEDED:      return "LIMIT_EXCEEDED";
        case BudgetError::UNKNOWN_RESERVATION: return "UNKNOWN_RESERVATION";
        case BudgetError::DOUBLE_RELEASE:      return "DOUBLE_RELEASE";
        case BudgetError::OVERFLOW:            return "OVERFLOW";
    }
    return "UNKNOWN";
}

class BudgetTracker {
public:
    explicit BudgetTracker(BudgetLimits limits = BudgetLimits{}) : limits_(limits) {}

    void set_limit(WarmthBudgetKind kind, std::uint64_t value) { limits_.set(kind, value); }
    [[nodiscard]] BudgetLimits limits() const noexcept { return limits_; }

    // Reserve amount of kind. Returns a nonzero reservation id on success,
    // or 0 when the reservation would exceed the limit.
    std::uint64_t reserve(WarmthBudgetKind kind, std::uint64_t amount, std::string tag = {},
                          BudgetError* error = nullptr) {
        if (error) *error = BudgetError::NONE;
        if (amount == 0) return 0;
        const std::uint64_t lim = limits_.get(kind);
        if (lim != 0 && amount > lim) { if (error) *error = BudgetError::LIMIT_EXCEEDED; return 0; }
        if (!fits(kind, amount)) { if (error) *error = BudgetError::LIMIT_EXCEEDED; return 0; }
        const std::uint64_t id = next_id();
        reservations_.emplace(id, Res{kind, amount, std::move(tag)});
        usage_[static_cast<std::uint8_t>(kind)] += amount;
        return id;
    }

    BudgetError release(std::uint64_t reservation_id) {
        if (reservation_id == 0) return BudgetError::UNKNOWN_RESERVATION;
        const auto it = reservations_.find(reservation_id);
        if (it == reservations_.end()) return BudgetError::DOUBLE_RELEASE;
        const std::uint64_t amount = it->second.amount;
        const auto key = static_cast<std::uint8_t>(it->second.kind);
        const auto u = usage_[key];
        usage_[key] = (amount >= u) ? std::uint64_t(0) : (u - amount);
        reservations_.erase(it);
        return BudgetError::NONE;
    }

    [[nodiscard]] std::uint64_t usage(WarmthBudgetKind kind) const {
        auto it = usage_.find(static_cast<std::uint8_t>(kind));
        return it == usage_.end() ? 0 : it->second;
    }
    [[nodiscard]] bool any_usage() const noexcept { return !usage_.empty(); }
    [[nodiscard]] std::size_t outstanding() const noexcept { return reservations_.size(); }
    [[nodiscard]] bool can_reserve(WarmthBudgetKind kind, std::uint64_t amount) const { return fits(kind, amount); }
    [[nodiscard]] bool balanced() const noexcept { return reservations_.empty(); }

private:
    struct Res { WarmthBudgetKind kind; std::uint64_t amount; std::string tag; };

    [[nodiscard]] bool fits(WarmthBudgetKind kind, std::uint64_t amount) const {
        const auto u = usage(kind);
        const std::uint64_t lim = limits_.get(kind);
        if (lim == 0) return true;
        return amount <= lim && u <= lim - amount;
    }
    std::uint64_t next_id() noexcept {
        std::uint64_t v = next_id_++;
        if (v == 0) v = next_id_++;
        return v;
    }

    BudgetLimits limits_;
    std::map<std::uint64_t, Res> reservations_;
    std::map<std::uint8_t, std::uint64_t> usage_;
    std::uint64_t next_id_ = 1;
};

} // namespace warmth
