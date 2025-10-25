#pragma once

#include <drogon/HttpFilter.h>
#include <drogon/HttpResponse.h>

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace superapi::middleware {

class IdempotencyStore {
  public:
    struct StoredResponse {
        std::string fingerprint;
        unsigned status{200};
        std::string body;
        std::string contentType;
        std::vector<std::pair<std::string, std::string>> headers;
        std::chrono::steady_clock::time_point storedAt;
    };

    static IdempotencyStore &instance();

    std::optional<StoredResponse> find(const std::string &key);
    void put(const std::string &key, const std::string &fingerprint, const drogon::HttpResponsePtr &response);
    void erase(const std::string &key);

  private:
    void cleanupLocked();

    std::mutex mutex_;
    std::unordered_map<std::string, StoredResponse> responses_;
};

class IdempotencyMiddleware : public drogon::HttpFilter<IdempotencyMiddleware, false> {
  public:
    void doFilter(const drogon::HttpRequestPtr &req, drogon::FilterCallback &&fcb, drogon::FilterChainCallback &&fccb) override;
};

}  // namespace superapi::middleware
