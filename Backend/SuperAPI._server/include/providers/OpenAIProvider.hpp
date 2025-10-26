#pragma once

#include "providers/HttpProviderBase.hpp"

namespace superapi::providers {

class OpenAIProvider : public HttpProviderBase {
   public:
    explicit OpenAIProvider(ProviderConfig config);
};

}  // namespace superapi::providers
