#pragma once
/// @file platform_util.hpp
/// Private to the platform library's backends (docs/PLATFORM.md), on every system: a deadline from an optional timeout, the one place that turns
/// "wait up to this long" into the milliseconds a system wait takes.

#include <algorithm>
#include <chrono>
#include <optional>

namespace sub0squad::platform::detail {

using Clock = std::chrono::steady_clock;

/// A point in time to wait until, or none (wait as long as it takes).
class Deadline
{
public:
    explicit Deadline(std::optional<std::chrono::milliseconds> timeout) : at_(timeout ? std::optional{Clock::now() + *timeout} : std::nullopt) {}

    [[nodiscard]] bool none() const { return !at_; }
    [[nodiscard]] bool passed() const { return at_ && Clock::now() >= *at_; }
    /// The time left in whole milliseconds, at least 0 and at most `cap`; `forever` when there is no deadline.
    [[nodiscard]] long long msLeft(long long forever = -1, long long cap = 1 << 30) const
    {
        if (!at_) return forever;
        return std::clamp<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(*at_ - Clock::now()).count(), 0, cap);
    }
    /// The time left as a duration (zero once it passed); nullopt when there is no deadline.
    [[nodiscard]] std::optional<std::chrono::milliseconds> left() const
    {
        if (!at_) return std::nullopt;
        return std::chrono::milliseconds(msLeft());
    }

private:
    std::optional<Clock::time_point> at_;
};

}  // namespace sub0squad::platform::detail
