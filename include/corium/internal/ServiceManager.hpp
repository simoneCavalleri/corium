#pragma once

#include "corium/ServiceRegistry.hpp"
#include "corium/ServiceContext.hpp"

#include <memory>
#include <thread>
#include <vector>

namespace corium {

/// @brief Manager for creating, starting, stopping, and joining background service jthreads.
class ServiceManager {
public:
    enum class State {
        Created,
        Built,
        Started,
        Stopped
    };

    ServiceManager() = default;

    ~ServiceManager()
    {
        stop();
        join();
    }

    ServiceManager(const ServiceManager&) = delete;
    ServiceManager& operator=(const ServiceManager&) = delete;

    /// @brief Build background services from registry factories.
    template <typename EventVariant>
    void build(ServiceRegistryT<EventVariant>& registry, ServiceContextT<EventVariant>& context)
    {
        for (auto& factory : registry._serviceFactories) {
            _services.push_back(factory(context));
        }
        _state = State::Built;
    }

    /// @brief Start background service threads.
    void start()
    {
        if (_state == State::Started) {
            return;
        }

        for (auto& service : _services) {
            IBackgroundService* rawService = service.get();

            _threads.emplace_back([rawService](std::stop_token stopToken) {
                try {
                    rawService->run(stopToken);
                } catch (...) {
                    // Prevent uncaught worker thread exceptions from terminating the process
                }
            });
        }

        _state = State::Started;
    }

    /// @brief Request graceful stop of background service threads via stop_token.
    void stop() noexcept
    {
        if (_state != State::Started) {
            return;
        }

        for (auto& thread : _threads) {
            try {
                thread.request_stop();
            } catch (...) {
                // Ignore errors during stop request
            }
        }

        _state = State::Stopped;
    }

    /// @brief Join background service jthreads cleanly.
    void join() noexcept
    {
        try {
            _threads.clear();
        } catch (...) {
            // Ignore errors during thread join
        }
    }

private:
    std::vector<std::unique_ptr<IBackgroundService>> _services;
    std::vector<std::jthread> _threads;
    State _state = State::Created;
};

} // namespace corium
