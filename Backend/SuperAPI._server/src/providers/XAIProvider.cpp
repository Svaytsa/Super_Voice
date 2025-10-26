#include "providers/XAIProvider.hpp"

#include <utility>

namespace superapi::providers {

XAIProvider::XAIProvider(ProviderConfig config)
    : HttpProviderBase(std::move(config), AuthStrategy::BearerAuthorization) {}

}  // namespace superapi::providers
