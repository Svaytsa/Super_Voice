#pragma once

#include "providers/HttpProviderBase.hpp"

namespace superapi::providers {

class DeepSeekProvider : public HttpProviderBase {
   public:
    explicit DeepSeekProvider(ProviderConfig config);
};

}  // namespace superapi::providers
