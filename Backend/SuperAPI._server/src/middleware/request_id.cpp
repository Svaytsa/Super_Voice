#include "superapi/middleware/request_id.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/utils/Utilities.h>

namespace superapi::middleware {

void RequestIdMiddleware::doFilter(const drogon::HttpRequestPtr &req,
                                   drogon::FilterCallback &&fcb,
                                   drogon::FilterChainCallback &&fccb) {
    std::string requestId = req->getHeader("X-Request-ID");
    if (requestId.empty()) {
        requestId = generateRequestId();
    }

    req->attributes()->insert("request_id", requestId);
    req->addHeader("X-Request-ID", requestId);

    (void)fcb;
    fccb();
}

std::string RequestIdMiddleware::generateRequestId() {
    return drogon::utils::genRandomString(16);
}

}  // namespace superapi::middleware
