#include "providers/AgentRouterProvider.hpp"

#include <utility>

namespace superapi::providers {

AgentRouterProvider::AgentRouterProvider(ProviderConfig config)
    : HttpProviderBase(std::move(config), AuthStrategy::BearerAuthorization) {}

}  // namespace superapi::providers
