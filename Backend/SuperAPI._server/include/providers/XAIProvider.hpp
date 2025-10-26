#pragma once

#include "providers/HttpProviderBase.hpp"

namespace superapi::providers {

class XAIProvider : public HttpProviderBase {
   public:
    explicit XAIProvider(ProviderConfig config);
};

}  // namespace superapi::providers
