#pragma once

#include <stop_token>

namespace corium {

class IBackgroundService
{
public:
    virtual ~IBackgroundService() = default;

    virtual void run(std::stop_token stopToken) = 0;
};

} // namespace corium
