#include "providers/QwenProvider.hpp"

#include <utility>

namespace superapi::providers {

QwenProvider::QwenProvider(ProviderConfig config)
    : HttpProviderBase(std::move(config), AuthStrategy::BearerAuthorization) {}

}  // namespace superapi::providers
