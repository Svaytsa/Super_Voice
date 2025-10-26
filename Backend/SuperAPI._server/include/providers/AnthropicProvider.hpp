#pragma once

#include "providers/HttpProviderBase.hpp"

namespace superapi::providers {

class AnthropicProvider : public HttpProviderBase {
   public:
    explicit AnthropicProvider(ProviderConfig config);
};

}  // namespace superapi::providers
