#include "providers/HuggingFaceProvider.hpp"

#include <utility>

namespace superapi::providers {

HuggingFaceProvider::HuggingFaceProvider(ProviderConfig config)
    : HttpProviderBase(std::move(config), AuthStrategy::BearerAuthorization) {}

}  // namespace superapi::providers
