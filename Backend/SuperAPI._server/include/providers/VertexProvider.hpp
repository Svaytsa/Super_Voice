#pragma once

#include "providers/HttpProviderBase.hpp"

namespace superapi::providers {

class VertexProvider : public HttpProviderBase {
   public:
    explicit VertexProvider(ProviderConfig config);
};

}  // namespace superapi::providers
