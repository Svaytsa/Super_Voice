#pragma once

#include "providers/HttpProviderBase.hpp"

namespace superapi::providers {

class GeminiProvider : public HttpProviderBase {
   public:
    explicit GeminiProvider(ProviderConfig config);
};

}  // namespace superapi::providers
