#pragma once

#include "providers/IProvider.hpp"

#include <cpr/cpr.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace superapi::providers {

enum class ProviderOperation {
    Chat,
    Embeddings,
    Images,
    Asr,
    Tts,
    Video,
    ListModels,
    Batches,
    JobStatus,
};

enum class AuthStrategy {
    BearerAuthorization,
    XApiKey,
    XGoogApiKey,
    None,
};

struct ProviderConfig {
    std::string name;
    std::string baseUrl;
    std::string apiKeyEnv;
    std::string organizationEnv;
    std::unordered_map<std::string, std::string> defaultHeaders;
    std::size_t maxRetries{3};
    std::chrono::milliseconds baseBackoff{200};
    std::chrono::milliseconds maxBackoff{2000};
    std::chrono::milliseconds timeout{60000};
    std::chrono::milliseconds connectTimeout{5000};
    bool dryRun{false};
    std::size_t circuitBreakerThreshold{3};
    std::chrono::milliseconds circuitBreakerCooldown{std::chrono::seconds(10)};
};

class HttpProviderBase : public IProvider {
   public:
    HttpProviderBase(ProviderConfig config, AuthStrategy strategy);
    ~HttpProviderBase() override;

    ProviderResult<ChatResponse> chat(const ChatRequest &request, const RequestContext &context) override;
    ProviderResult<EmbeddingsResponse> embeddings(const EmbeddingsRequest &request, const RequestContext &context) override;
    ProviderResult<ImageResponse> images(const ImageRequest &request, const RequestContext &context) override;
    ProviderResult<AsrResponse> asr(const AsrRequest &request, const RequestContext &context) override;
    ProviderResult<TtsResponse> tts(const TtsRequest &request, const RequestContext &context) override;
    ProviderResult<VideoResponse> video(const VideoRequest &request, const RequestContext &context) override;
    ProviderResult<ListModelsResponse> listModels(const ListModelsRequest &request, const RequestContext &context) override;
    ProviderResult<BatchesResponse> batches(const BatchesRequest &request, const RequestContext &context) override;
    ProviderResult<JobStatusResponse> jobStatus(const JobStatusRequest &request, const RequestContext &context) override;

   protected:
    struct JsonOperationResult {
        Json::Value payload;
        Usage usage;
        std::string providerRequestId;
    };

    ProviderResult<JsonOperationResult> performJsonOperation(ProviderOperation operation,
                                                              const Json::Value &payload,
                                                              const RequestContext &context,
                                                              const std::string &resourceId = {});

    [[nodiscard]] const ProviderConfig &config() const { return config_; }

    virtual Json::Value transformRequest(ProviderOperation operation, const Json::Value &payload) const;
    virtual Json::Value transformResponse(ProviderOperation operation, const Json::Value &payload) const;
    virtual void augmentHeaders(cpr::Header &header) const;

    cpr::Header buildHeaders(const std::string &apiKey, const RequestContext &context) const;

   private:
    struct CircuitBreakerState {
        std::size_t failures{0};
        std::chrono::steady_clock::time_point openUntil{};
    };

    ProviderResult<JsonOperationResult> performHttpRequest(ProviderOperation operation,
                                                            const Json::Value &payload,
                                                            const RequestContext &context,
                                                            const std::string &resourceId);

    ProviderError makeDryRunError(const RequestContext &context) const;
    ProviderError makeAuthError(const std::string &code, const std::string &message, const RequestContext &context) const;
    ProviderError makeProviderError(const std::string &code,
                                    const std::string &message,
                                    const std::string &requestId,
                                    double retryAfter) const;

    std::string buildUrl(ProviderOperation operation, const std::string &resourceId) const;
    std::string resolvePath(ProviderOperation operation, const std::string &resourceId) const;
    ProviderResult<JsonOperationResult> makeCircuitOpenError(const RequestContext &context) const;
    std::string extractRequestId(const cpr::Response &response) const;
    double parseRetryAfter(const cpr::Response &response) const;
    Usage extractUsage(const Json::Value &payload) const;

    ProviderConfig config_;
    AuthStrategy authStrategy_;
    mutable std::mutex mutex_;
    mutable std::mt19937 rng_;
    mutable CircuitBreakerState breaker_;
};

}  // namespace superapi::providers
