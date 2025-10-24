#pragma once

#include <drogon/HttpFilter.h>
#include <string>

namespace superapi::middleware {

class RequestIdMiddleware : public drogon::HttpFilter<RequestIdMiddleware, false> {
  public:
    void doFilter(const drogon::HttpRequestPtr &req,
                  drogon::FilterCallback &&fcb,
                  drogon::FilterChainCallback &&fccb) override;

  private:
    static std::string generateRequestId();
};

}  // namespace superapi::middleware
