#include "providers/LamaProvider.hpp"

#include <utility>

namespace superapi::providers {

LamaProvider::LamaProvider(ProviderConfig config)
    : HttpProviderBase(std::move(config), AuthStrategy::BearerAuthorization) {}

}  // namespace superapi::providers
