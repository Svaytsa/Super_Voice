#include "providers/VertexProvider.hpp"

#include <utility>

namespace superapi::providers {

VertexProvider::VertexProvider(ProviderConfig config)
    : HttpProviderBase(std::move(config), AuthStrategy::BearerAuthorization) {}

}  // namespace superapi::providers
