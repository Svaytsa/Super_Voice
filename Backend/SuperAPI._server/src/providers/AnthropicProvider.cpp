#include "providers/AnthropicProvider.hpp"

#include <utility>

namespace superapi::providers {

AnthropicProvider::AnthropicProvider(ProviderConfig config)
    : HttpProviderBase(std::move(config), AuthStrategy::XApiKey) {}

}  // namespace superapi::providers
