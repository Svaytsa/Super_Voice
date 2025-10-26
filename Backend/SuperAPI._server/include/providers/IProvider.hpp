#pragma once

#include <json/json.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace superapi::providers {

struct RequestContext {
    std::string requestId;
    std::string vendor;
};

struct Usage {
    std::uint64_t promptTokens{0};
    std::uint64_t completionTokens{0};
    std::uint64_t totalTokens{0};
    std::uint64_t audioTokens{0};
    std::uint64_t cachedTokens{0};
    std::string note;
};

struct ProviderError {
    std::string type;
    std::string message;
    std::string provider;
    std::string code;
    std::string requestId;
    double retryAfter{0.0};
};

template <typename T>
struct ProviderResult {
    std::optional<T> data;
    std::optional<ProviderError> error;

    [[nodiscard]] bool ok() const { return data.has_value(); }
};

struct ChatRequest {
    Json::Value payload;
};

struct ChatResponse {
    Json::Value payload;
    Usage usage;
    std::string providerRequestId;
};

struct EmbeddingsRequest {
    Json::Value payload;
};

struct EmbeddingsResponse {
    Json::Value payload;
    Usage usage;
    std::string providerRequestId;
};

struct ImageRequest {
    Json::Value payload;
};

struct ImageResponse {
    Json::Value payload;
    std::string providerRequestId;
};

struct AsrRequest {
    Json::Value payload;
};

struct AsrResponse {
    Json::Value payload;
    Usage usage;
    std::string providerRequestId;
};

struct TtsRequest {
    Json::Value payload;
};

struct TtsResponse {
    Json::Value payload;
    Usage usage;
    std::string providerRequestId;
};

struct VideoRequest {
    Json::Value payload;
};

struct VideoResponse {
    Json::Value payload;
    Usage usage;
    std::string providerRequestId;
};

struct ListModelsRequest {
    Json::Value payload;
};

struct ListModelsResponse {
    Json::Value payload;
    std::string providerRequestId;
};

struct BatchesRequest {
    Json::Value payload;
};

struct BatchesResponse {
    Json::Value payload;
    std::string providerRequestId;
};

struct JobStatusRequest {
    std::string jobId;
};

struct JobStatusResponse {
    Json::Value payload;
    std::string providerRequestId;
};

class IProvider {
   public:
    virtual ~IProvider() = default;

    virtual ProviderResult<ChatResponse> chat(const ChatRequest &request, const RequestContext &context) = 0;
    virtual ProviderResult<EmbeddingsResponse> embeddings(const EmbeddingsRequest &request, const RequestContext &context) = 0;
    virtual ProviderResult<ImageResponse> images(const ImageRequest &request, const RequestContext &context) = 0;
    virtual ProviderResult<AsrResponse> asr(const AsrRequest &request, const RequestContext &context) = 0;
    virtual ProviderResult<TtsResponse> tts(const TtsRequest &request, const RequestContext &context) = 0;
    virtual ProviderResult<VideoResponse> video(const VideoRequest &request, const RequestContext &context) = 0;
    virtual ProviderResult<ListModelsResponse> listModels(const ListModelsRequest &request, const RequestContext &context) = 0;
    virtual ProviderResult<BatchesResponse> batches(const BatchesRequest &request, const RequestContext &context) = 0;
    virtual ProviderResult<JobStatusResponse> jobStatus(const JobStatusRequest &request, const RequestContext &context) = 0;
};

}  // namespace superapi::providers
