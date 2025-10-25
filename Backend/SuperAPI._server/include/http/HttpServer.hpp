#pragma once

#include "superapi/app_config.h"

namespace superapi::http {

class HttpServer {
  public:
    static void registerRoutes(const AppConfig &config);
};

}  // namespace superapi::http
