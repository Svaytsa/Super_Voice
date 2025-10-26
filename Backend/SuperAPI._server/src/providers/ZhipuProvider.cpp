#include "providers/ZhipuProvider.hpp"

#include <utility>

namespace superapi::providers {

ZhipuProvider::ZhipuProvider(ProviderConfig config)
    : HttpProviderBase(std::move(config), AuthStrategy::BearerAuthorization) {}

}  // namespace superapi::providers
