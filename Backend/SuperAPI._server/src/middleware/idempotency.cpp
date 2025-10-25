#include "superapi/middleware/idempotency.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <chrono>
#include <exception>
#include <string>
#include <utility>

namespace superapi::middleware {
namespace {

constexpr std::chrono::minutes kRetention{10};

std::string buildFingerprint(const drogon::HttpRequestPtr &req) {
    if (!req) {
        return {};
    }

    std::string fingerprint{req->methodString()};
    fingerprint.push_back('|');
    fingerprint.append(std::string{req->path()});
    fingerprint.push_back('|');
    fingerprint.append(req->getBody());
    return fingerprint;
}

std::string getAttributeString(const drogon::HttpRequestPtr &req, const std::string &key) {
    if (!req) {
        return {};
    }
    try {
        auto attributes = req->attributes();
        if (!attributes) {
            return {};
        }
        return attributes->get<std::string>(key);
    } catch (const std::exception &) {
        return {};
    }
}

Json::Value buildConflictPayload(const std::string &company, const std::string &requestId) {
    Json::Value root(Json::objectValue);
    Json::Value error(Json::objectValue);
    error["type"] = "idempotency_conflict";
    error["message"] = "Idempotency-Key reuse detected with different payload.";
    error["provider"] = company;
    error["code"] = 409;
    error["request_id"] = requestId;
    error["retry_after"] = 0.0;
    root["error"] = std::move(error);
    return root;
}

}  // namespace

IdempotencyStore &IdempotencyStore::instance() {
    static IdempotencyStore store;
    return store;
}

void IdempotencyStore::cleanupLocked() {
    const auto now = std::chrono::steady_clock::now();
    for (auto it = responses_.begin(); it != responses_.end();) {
        if (now - it->second.storedAt > kRetention) {
            it = responses_.erase(it);
        } else {
            ++it;
        }
    }
}

std::optional<IdempotencyStore::StoredResponse> IdempotencyStore::find(const std::string &key) {
    std::lock_guard<std::mutex> lock(mutex_);
    cleanupLocked();
    if (auto it = responses_.find(key); it != responses_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void IdempotencyStore::put(const std::string &key, const std::string &fingerprint, const drogon::HttpResponsePtr &response) {
    if (!response) {
        return;
    }
    if (response->getHeader("Idempotent-Replayed") == "true") {
        return;
    }
    const auto contentType = response->getHeader("Content-Type");
    if (contentType.find("text/event-stream") != std::string::npos) {
        return;
    }
    StoredResponse stored;
    stored.fingerprint = fingerprint;
    stored.status = static_cast<unsigned>(response->getStatusCode());
    stored.body = response->body();
    stored.contentType = contentType;
    stored.headers.emplace_back("X-Tokens-Out", response->getHeader("X-Tokens-Out"));
    stored.headers.emplace_back("X-Request-ID", response->getHeader("X-Request-ID"));
    stored.storedAt = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(mutex_);
    cleanupLocked();
    responses_[key] = std::move(stored);
}

void IdempotencyStore::erase(const std::string &key) {
    std::lock_guard<std::mutex> lock(mutex_);
    responses_.erase(key);
}

void IdempotencyMiddleware::doFilter(const drogon::HttpRequestPtr &req,
                                     drogon::FilterCallback &&fcb,
                                     drogon::FilterChainCallback &&fccb) {
    const auto key = req->getHeader("Idempotency-Key");
    if (key.empty()) {
        fccb();
        return;
    }

    const auto fingerprint = buildFingerprint(req);
    auto stored = IdempotencyStore::instance().find(key);
    if (stored) {
        if (stored->fingerprint != fingerprint) {
            auto company = getAttributeString(req, "observability.company");
            if (company.empty()) {
                company = "superapi";
            }
            auto requestId = getAttributeString(req, "request_id");
            if (requestId.empty()) {
                requestId = key;
            }
            auto conflict = drogon::HttpResponse::newHttpJsonResponse(buildConflictPayload(company, requestId));
            conflict->setStatusCode(drogon::k409Conflict);
            fcb(conflict);
            return;
        }
        auto replay = drogon::HttpResponse::newHttpResponse();
        replay->setStatusCode(static_cast<drogon::HttpStatusCode>(stored->status));
        replay->setBody(stored->body);
        if (!stored->contentType.empty()) {
            replay->addHeader("Content-Type", stored->contentType);
        }
        for (const auto &[header, value] : stored->headers) {
            if (!value.empty()) {
                replay->addHeader(header, value);
            }
        }
        replay->addHeader("Idempotent-Replayed", "true");
        fcb(replay);
        return;
    }

    if (auto attributes = req->attributes()) {
        attributes->insert("idempotency.key", key);
        attributes->insert("idempotency.fingerprint", fingerprint);
    }
    fccb();
}

}  // namespace superapi::middleware
