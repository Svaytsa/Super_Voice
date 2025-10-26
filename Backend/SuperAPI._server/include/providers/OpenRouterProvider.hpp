#pragma once

#include "providers/HttpProviderBase.hpp"

namespace superapi::providers {

class OpenRouterProvider : public HttpProviderBase {
   public:
    explicit OpenRouterProvider(ProviderConfig config);
};

}  // namespace superapi::providers
