#pragma once

#include "providers/HttpProviderBase.hpp"

namespace superapi::providers {

class LamaProvider : public HttpProviderBase {
   public:
    explicit LamaProvider(ProviderConfig config);
};

}  // namespace superapi::providers
