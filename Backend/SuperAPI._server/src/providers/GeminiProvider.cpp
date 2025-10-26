#include "providers/GeminiProvider.hpp"

#include <utility>

namespace superapi::providers {

GeminiProvider::GeminiProvider(ProviderConfig config)
    : HttpProviderBase(std::move(config), AuthStrategy::XGoogApiKey) {}

}  // namespace superapi::providers
