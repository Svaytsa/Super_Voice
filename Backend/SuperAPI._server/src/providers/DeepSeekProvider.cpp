#include "providers/DeepSeekProvider.hpp"

#include <utility>

namespace superapi::providers {

DeepSeekProvider::DeepSeekProvider(ProviderConfig config)
    : HttpProviderBase(std::move(config), AuthStrategy::BearerAuthorization) {}

}  // namespace superapi::providers
