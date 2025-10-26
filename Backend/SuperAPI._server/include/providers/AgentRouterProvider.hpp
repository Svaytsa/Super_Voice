#pragma once

#include "providers/HttpProviderBase.hpp"

namespace superapi::providers {

class AgentRouterProvider : public HttpProviderBase {
   public:
    explicit AgentRouterProvider(ProviderConfig config);
};

}  // namespace superapi::providers
