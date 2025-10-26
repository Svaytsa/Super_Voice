#pragma once

#include "providers/HttpProviderBase.hpp"

namespace superapi::providers {

class ZhipuProvider : public HttpProviderBase {
   public:
    explicit ZhipuProvider(ProviderConfig config);
};

}  // namespace superapi::providers
