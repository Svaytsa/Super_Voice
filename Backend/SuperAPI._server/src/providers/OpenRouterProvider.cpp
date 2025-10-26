#include "providers/OpenRouterProvider.hpp"

#include <utility>

namespace superapi::providers {

OpenRouterProvider::OpenRouterProvider(ProviderConfig config)
    : HttpProviderBase(std::move(config), AuthStrategy::BearerAuthorization) {}

}  // namespace superapi::providers
