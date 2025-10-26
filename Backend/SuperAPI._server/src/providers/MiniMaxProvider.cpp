#include "providers/MiniMaxProvider.hpp"

#include <utility>

namespace superapi::providers {

MiniMaxProvider::MiniMaxProvider(ProviderConfig config)
    : HttpProviderBase(std::move(config), AuthStrategy::BearerAuthorization) {}

}  // namespace superapi::providers
