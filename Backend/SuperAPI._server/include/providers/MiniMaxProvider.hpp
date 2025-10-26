#pragma once

#include "providers/HttpProviderBase.hpp"

namespace superapi::providers {

class MiniMaxProvider : public HttpProviderBase {
   public:
    explicit MiniMaxProvider(ProviderConfig config);
};

}  // namespace superapi::providers
