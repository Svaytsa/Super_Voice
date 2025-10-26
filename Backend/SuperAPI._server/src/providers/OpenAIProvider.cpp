#include "providers/OpenAIProvider.hpp"

#include <utility>

namespace superapi::providers {

OpenAIProvider::OpenAIProvider(ProviderConfig config)
    : HttpProviderBase(std::move(config), AuthStrategy::BearerAuthorization) {}

}  // namespace superapi::providers
