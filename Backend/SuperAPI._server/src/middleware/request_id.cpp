#include "superapi/middleware/request_id.h"

#include "core/Metrics.hpp"
#include "core/Tracing.hpp"
#include "superapi/logging.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/utils/Utilities.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <exception>
#include <unordered_map>

namespace superapi::middleware {

namespace {

std::string sanitizeLabel(std::string value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (char ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-' || ch == '.') {
            sanitized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        } else if (std::isspace(static_cast<unsigned char>(ch))) {
            sanitized.push_back('_');
        }
    }
    if (sanitized.empty()) {
        sanitized = "unknown";
    }
    return sanitized;
}

std::uint64_t parseUnsigned(const std::string &value) {
    if (value.empty()) {
        return 0;
    }
    try {
        return std::stoull(value);
    } catch (const std::exception &) {
        return 0;
    }
}

std::string resolveCompany(const drogon::HttpRequestPtr &req) {
    static const std::array<std::string, 3> headers{"X-Company", "X-Company-Id", "X-Tenant"};
    for (const auto &header : headers) {
        auto value = req->getHeader(header);
        if (!value.empty()) {
            return sanitizeLabel(value);
        }
    }
    const auto &params = req->getParameters();
    if (auto it = params.find("company"); it != params.end()) {
        if (!it->second.empty()) {
            return sanitizeLabel(it->second);
        }
    }
    return std::string{"unknown"};
}

std::string resolveEndpoint(const drogon::HttpRequestPtr &req) {
    auto path = req->path();
    if (path.empty()) {
        return "unknown";
    }
    return path;
}

}  // namespace

void RequestIdMiddleware::doFilter(const drogon::HttpRequestPtr &req,
                                   drogon::FilterCallback &&fcb,
                                   drogon::FilterChainCallback &&fccb) {
    std::string requestId = req->getHeader("X-Request-ID");
    if (requestId.empty()) {
        requestId = generateRequestId();
    }

    req->attributes()->insert("request_id", requestId);
    req->addHeader("X-Request-ID", requestId);

    const auto company = resolveCompany(req);
    const auto endpoint = resolveEndpoint(req);
    const auto bodySize = static_cast<std::uint64_t>(req->bodyLength());

    auto metricsContext = core::MetricsRegistry::instance().startRequest(company, endpoint, bodySize);
    req->attributes()->insert("observability.metrics", metricsContext);
    req->attributes()->insert("observability.company", company);
    req->attributes()->insert("observability.endpoint", endpoint);

    if (auto tokensHeader = req->getHeader("X-Tokens-In"); !tokensHeader.empty()) {
        auto tokens = parseUnsigned(tokensHeader);
        if (tokens > 0) {
            metricsContext->addTokensIn(tokens);
            req->attributes()->insert("observability.tokens_in", tokens);
        }
    }

    auto &tracer = core::Tracer::instance();
    const auto parentContext = tracer.extractTraceparent(req->getHeader("traceparent"));
    std::unordered_map<std::string, core::AttributeValue> spanAttributes = {
        {"http.method", std::string(req->methodString())},
        {"http.target", req->path()},
        {"http.scheme", std::string(req->scheme())},
        {"http.host", req->getHeader("Host")},
        {"company", company},
        {"http.user_agent", req->getHeader("User-Agent")},
    };
    if (!req->peerAddr().toIp().empty()) {
        spanAttributes.emplace("net.peer.ip", req->peerAddr().toIp());
        spanAttributes.emplace("net.peer.port", static_cast<std::int64_t>(req->peerAddr().toPort()));
    }

    auto span = tracer.startSpan(std::string(req->methodString()) + " " + req->path(), parentContext, spanAttributes, "SERVER");
    req->attributes()->insert("observability.span", span);
    req->addHeader("traceparent", tracer.buildTraceparent(span->context()));

    LogContext logContext{};
    logContext.requestId = requestId;
    logContext.company = company;
    logContext.endpoint = endpoint;
    logContext.hasRequest = true;
    setLogContext(logContext);

    (void)fcb;
    fccb();
}

std::string RequestIdMiddleware::generateRequestId() {
    return drogon::utils::genRandomString(16);
}

}  // namespace superapi::middleware
