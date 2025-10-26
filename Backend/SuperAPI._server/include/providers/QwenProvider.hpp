#pragma once

#include "providers/HttpProviderBase.hpp"

namespace superapi::providers {

class QwenProvider : public HttpProviderBase {
   public:
    explicit QwenProvider(ProviderConfig config);
};

}  // namespace superapi::providers
