#include "providers/PerplexityProvider.hpp"

#include <utility>

namespace superapi::providers {

PerplexityProvider::PerplexityProvider(ProviderConfig config)
    : HttpProviderBase(std::move(config), AuthStrategy::BearerAuthorization) {}

}  // namespace superapi::providers
