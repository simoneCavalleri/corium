#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace corium::safety {

/// @brief Circuit Breaker operational state.
enum class CircuitState : uint8_t {
    Closed,   ///< Normal operation: all calls execute.
    Open,     ///< Tripped/Faulty: calls are fast-failed without execution.
    HalfOpen  ///< Recovery probing: allowing a single canary call to verify health.
};

/// @brief Zero-allocation Circuit Breaker pattern for isolating faulty handlers or peripheral links.
/// Thread-safe lock-free state transitions.
/// @tparam FailureThreshold Consecutive failures before tripping open (default: 3).
/// @tparam RecoveryTimeoutMs Cooldown duration in milliseconds before moving to HalfOpen (default: 500ms).
template <
    uint32_t FailureThreshold = 3,
    uint32_t RecoveryTimeoutMs = 500
>
class CircuitBreaker {
public:
    CircuitBreaker() noexcept = default;

    /// @brief Check if execution is permitted under current circuit state.
    /// Automatically transitions from Open to HalfOpen if recovery cooldown has elapsed.
    [[nodiscard]] bool allowExecution() noexcept
    {
        const CircuitState currentState = _state.load(std::memory_order_acquire);

        if (currentState == CircuitState::Closed) {
            return true;
        }

        if (currentState == CircuitState::Open) {
            const uint64_t now = nowMs();
            const uint64_t trippedAt = _trippedAtMs.load(std::memory_order_acquire);
            if (now >= trippedAt && (now - trippedAt) >= RecoveryTimeoutMs) {
                // Attempt transition to HalfOpen
                CircuitState expected = CircuitState::Open;
                if (_state.compare_exchange_strong(expected, CircuitState::HalfOpen, std::memory_order_acq_rel)) {
                    return true;
                }
            }
            return false;
        }

        // HalfOpen: allow execution for probing
        return true;
    }

    /// @brief Record a successful operation execution.
    /// Resets consecutive failure counter and restores Closed state from HalfOpen.
    void recordSuccess() noexcept
    {
        _failureCount.store(0, std::memory_order_relaxed);
        _state.store(CircuitState::Closed, std::memory_order_release);
    }

    /// @brief Record a failed operation.
    /// Increments failure counter and trips circuit Open if threshold is reached.
    void recordFailure() noexcept
    {
        const uint32_t count = _failureCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count >= FailureThreshold) {
            _trippedAtMs.store(nowMs(), std::memory_order_release);
            _state.store(CircuitState::Open, std::memory_order_release);
        }
    }

    /// @brief Manually reset the circuit breaker to normal Closed state.
    void reset() noexcept
    {
        _failureCount.store(0, std::memory_order_relaxed);
        _trippedAtMs.store(0, std::memory_order_relaxed);
        _state.store(CircuitState::Closed, std::memory_order_release);
    }

    /// @brief Manually trip the circuit breaker open.
    void trip() noexcept
    {
        _trippedAtMs.store(nowMs(), std::memory_order_release);
        _state.store(CircuitState::Open, std::memory_order_release);
    }

    /// @brief Current state of the circuit breaker.
    [[nodiscard]] CircuitState state() const noexcept
    {
        const CircuitState s = _state.load(std::memory_order_acquire);
        if (s == CircuitState::Open) {
            const uint64_t now = nowMs();
            const uint64_t tripped = _trippedAtMs.load(std::memory_order_acquire);
            if (now >= tripped && (now - tripped) >= RecoveryTimeoutMs) {
                return CircuitState::HalfOpen;
            }
        }
        return s;
    }

    /// @brief Current consecutive failure count.
    [[nodiscard]] uint32_t failureCount() const noexcept
    {
        return _failureCount.load(std::memory_order_relaxed);
    }

    /// @brief Execute a protected callable through the circuit breaker.
    /// @tparam Callable Function or lambda returning bool (true = success, false = failure).
    /// @param fn Callable to invoke if circuit is not open.
    /// @return true if executed and succeeded, false if rejected or failed.
    template <typename Callable>
    bool execute(Callable&& fn)
    {
        if (!allowExecution()) {
            return false;
        }

        bool ok = false;
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
        try {
            ok = fn();
        } catch (...) {
            ok = false;
        }
#else
        ok = fn();
#endif

        if (ok) {
            recordSuccess();
        } else {
            recordFailure();
        }

        return ok;
    }

private:
    [[nodiscard]] static uint64_t nowMs() noexcept
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
    }

    std::atomic<CircuitState> _state{CircuitState::Closed};
    std::atomic<uint32_t> _failureCount{0};
    std::atomic<uint64_t> _trippedAtMs{0};
};

} // namespace corium::safety
