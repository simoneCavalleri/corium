#pragma once

#include "corium/ServiceRegistry.hpp"
#include "corium/ServiceContext.hpp"

#include <memory>
#include <thread>
#include <vector>

namespace corium {

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

    void build(ServiceRegistry& registry, ServiceContext& context)
    {
        for (auto& factory : registry._serviceFactories) {
            _services.push_back(factory(context));
        }
        _state = State::Built;
    }

    void start()
    {
        if (_state == State::Started) {
            return;
        }

        for (auto& service : _services) {
            IBackgroundService* rawService = service.get();

            _threads.emplace_back([rawService](std::stop_token stopToken) {
                rawService->run(stopToken);
            });
        }

        _state = State::Started;
    }

    void stop()
    {
        if (_state != State::Started) {
            return;
        }

        for (auto& thread : _threads) {
            thread.request_stop();
        }

        _state = State::Stopped;
    }

    void join()
    {
        _threads.clear();
    }

private:
    std::vector<std::unique_ptr<IBackgroundService>> _services;
    std::vector<std::jthread> _threads;
    State _state = State::Created;
};

} // namespace corium
