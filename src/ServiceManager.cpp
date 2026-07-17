#include "corium/internal/ServiceManager.h"

namespace corium {

ServiceManager::~ServiceManager()
{
    stop();
    join();
}

void ServiceManager::build(ServiceRegistry& registry, ServiceContext& context)
{
    for (auto& factory : registry._serviceFactories) {
        _services.push_back(factory(context));
    }
}

void ServiceManager::start()
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

void ServiceManager::stop()
{
    if (_state != State::Started) {
        return;
    }

    for (auto& thread : _threads) {
        thread.request_stop();
    }

    _state = State::Stopped;
}

void ServiceManager::join()
{
    _threads.clear();
}

} // namespace corium