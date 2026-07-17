#pragma once

#include "corium/ServiceRegistry.h"
#include "corium/ServiceContext.h"

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
    ~ServiceManager();

    ServiceManager(const ServiceManager&) = delete;
    ServiceManager& operator=(const ServiceManager&) = delete;

    void build(ServiceRegistry& registry, ServiceContext& context);

    void start();
    void stop();
    void join();

private:
    std::vector<std::unique_ptr<IBackgroundService>> _services;

    std::vector<std::jthread> _threads;

    State _state = State::Created;
};

} // namespace corium
