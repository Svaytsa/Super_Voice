#pragma once

#include "providers/HttpProviderBase.hpp"

namespace superapi::providers {

class PerplexityProvider : public HttpProviderBase {
   public:
    explicit PerplexityProvider(ProviderConfig config);
};

}  // namespace superapi::providers
