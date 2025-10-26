#pragma once

#include "providers/HttpProviderBase.hpp"

namespace superapi::providers {

class HuggingFaceProvider : public HttpProviderBase {
   public:
    explicit HuggingFaceProvider(ProviderConfig config);
};

}  // namespace superapi::providers
