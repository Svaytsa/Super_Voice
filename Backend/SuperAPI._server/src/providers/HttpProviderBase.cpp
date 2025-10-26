#include "providers/HttpProviderBase.hpp"

#include "superapi/environment.h"

#include <drogon/drogon.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <sstream>
#include <thread>

namespace superapi::providers {
namespace {

bool isPostOperation(ProviderOperation operation) {
    switch (operation) {
        case ProviderOperation::Chat:
        case ProviderOperation::Embeddings:
        case ProviderOperation::Images:
        case ProviderOperation::Asr:
        case ProviderOperation::Tts:
        case ProviderOperation::Video:
        case ProviderOperation::Batches:
            return true;
        case ProviderOperation::ListModels:
        case ProviderOperation::JobStatus:
            return false;
    }
    return true;
}

std::chrono::milliseconds computeBackoff(std::chrono::milliseconds base,
                                         std::chrono::milliseconds max,
                                         std::size_t attempt,
                                         std::mt19937 &rng) {
    auto capped = std::min<std::chrono::milliseconds>(max, base * (1LL << attempt));
    std::uniform_int_distribution<int> jitter(0, static_cast<int>(base.count()));
    auto jitterMs = std::chrono::milliseconds(jitter(rng));
    auto total = capped + jitterMs;
    if (total > max) {
        total = max;
    }
    return total;
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

}  // namespace

HttpProviderBase::HttpProviderBase(ProviderConfig config, AuthStrategy strategy)
    : config_(std::move(config)), authStrategy_(strategy), rng_(std::random_device{}()) {}

HttpProviderBase::~HttpProviderBase() = default;

ProviderResult<ChatResponse> HttpProviderBase::chat(const ChatRequest &request, const RequestContext &context) {
    ProviderResult<ChatResponse> result;
    auto operation = performJsonOperation(ProviderOperation::Chat, request.payload, context);
    if (!operation.ok()) {
        result.error = operation.error;
        return result;
    }

    ChatResponse response;
    response.payload = operation.data->payload;
    response.usage = operation.data->usage;
    response.providerRequestId = operation.data->providerRequestId;
    result.data = response;
    return result;
}

ProviderResult<EmbeddingsResponse> HttpProviderBase::embeddings(const EmbeddingsRequest &request,
                                                                const RequestContext &context) {
    ProviderResult<EmbeddingsResponse> result;
    auto operation = performJsonOperation(ProviderOperation::Embeddings, request.payload, context);
    if (!operation.ok()) {
        result.error = operation.error;
        return result;
    }

    EmbeddingsResponse response;
    response.payload = operation.data->payload;
    response.usage = operation.data->usage;
    response.providerRequestId = operation.data->providerRequestId;
    result.data = response;
    return result;
}

ProviderResult<ImageResponse> HttpProviderBase::images(const ImageRequest &request, const RequestContext &context) {
    ProviderResult<ImageResponse> result;
    auto operation = performJsonOperation(ProviderOperation::Images, request.payload, context);
    if (!operation.ok()) {
        result.error = operation.error;
        return result;
    }

    ImageResponse response;
    response.payload = operation.data->payload;
    response.providerRequestId = operation.data->providerRequestId;
    result.data = response;
    return result;
}

ProviderResult<AsrResponse> HttpProviderBase::asr(const AsrRequest &request, const RequestContext &context) {
    ProviderResult<AsrResponse> result;
    auto operation = performJsonOperation(ProviderOperation::Asr, request.payload, context);
    if (!operation.ok()) {
        result.error = operation.error;
        return result;
    }

    AsrResponse response;
    response.payload = operation.data->payload;
    response.usage = operation.data->usage;
    response.providerRequestId = operation.data->providerRequestId;
    result.data = response;
    return result;
}

ProviderResult<TtsResponse> HttpProviderBase::tts(const TtsRequest &request, const RequestContext &context) {
    ProviderResult<TtsResponse> result;
    auto operation = performJsonOperation(ProviderOperation::Tts, request.payload, context);
    if (!operation.ok()) {
        result.error = operation.error;
        return result;
    }

    TtsResponse response;
    response.payload = operation.data->payload;
    response.usage = operation.data->usage;
    response.providerRequestId = operation.data->providerRequestId;
    result.data = response;
    return result;
}

ProviderResult<VideoResponse> HttpProviderBase::video(const VideoRequest &request, const RequestContext &context) {
    ProviderResult<VideoResponse> result;
    auto operation = performJsonOperation(ProviderOperation::Video, request.payload, context);
    if (!operation.ok()) {
        result.error = operation.error;
        return result;
    }

    VideoResponse response;
    response.payload = operation.data->payload;
    response.usage = operation.data->usage;
    response.providerRequestId = operation.data->providerRequestId;
    result.data = response;
    return result;
}

ProviderResult<ListModelsResponse> HttpProviderBase::listModels(const ListModelsRequest &request,
                                                                const RequestContext &context) {
    ProviderResult<ListModelsResponse> result;
    auto operation = performJsonOperation(ProviderOperation::ListModels, request.payload, context);
    if (!operation.ok()) {
        result.error = operation.error;
        return result;
    }

    ListModelsResponse response;
    response.payload = operation.data->payload;
    response.providerRequestId = operation.data->providerRequestId;
    result.data = response;
    return result;
}

ProviderResult<BatchesResponse> HttpProviderBase::batches(const BatchesRequest &request,
                                                          const RequestContext &context) {
    ProviderResult<BatchesResponse> result;
    auto operation = performJsonOperation(ProviderOperation::Batches, request.payload, context);
    if (!operation.ok()) {
        result.error = operation.error;
        return result;
    }

    BatchesResponse response;
    response.payload = operation.data->payload;
    response.providerRequestId = operation.data->providerRequestId;
    result.data = response;
    return result;
}

ProviderResult<JobStatusResponse> HttpProviderBase::jobStatus(const JobStatusRequest &request,
                                                              const RequestContext &context) {
    ProviderResult<JobStatusResponse> result;
    auto operation = performJsonOperation(ProviderOperation::JobStatus, Json::Value(Json::objectValue), context, request.jobId);
    if (!operation.ok()) {
        result.error = operation.error;
        return result;
    }

    JobStatusResponse response;
    response.payload = operation.data->payload;
    response.providerRequestId = operation.data->providerRequestId;
    result.data = response;
    return result;
}

ProviderResult<HttpProviderBase::JsonOperationResult> HttpProviderBase::performJsonOperation(
    ProviderOperation operation, const Json::Value &payload, const RequestContext &context, const std::string &resourceId) {
    auto transformedRequest = transformRequest(operation, payload);
    auto result = performHttpRequest(operation, transformedRequest, context, resourceId);
    if (!result.ok()) {
        return result;
    }

    auto transformedResponse = transformResponse(operation, result.data->payload);
    result.data->payload = transformedResponse;

    auto usage = extractUsage(result.data->payload);
    if (!result.data->payload.isMember("usage") || !result.data->payload["usage"].isObject()) {
        Json::Value usageJson(Json::objectValue);
        usageJson["prompt_tokens"] = static_cast<Json::UInt64>(usage.promptTokens);
        usageJson["completion_tokens"] = static_cast<Json::UInt64>(usage.completionTokens);
        usageJson["total_tokens"] = static_cast<Json::UInt64>(usage.totalTokens);
        usageJson["audio_tokens"] = static_cast<Json::UInt64>(usage.audioTokens);
        usageJson["cached_tokens"] = static_cast<Json::UInt64>(usage.cachedTokens);
        result.data->payload["usage"] = usageJson;
    }
    if (!usage.note.empty()) {
        if (!result.data->payload.isMember("metadata") || !result.data->payload["metadata"].isObject()) {
            result.data->payload["metadata"] = Json::Value(Json::objectValue);
        }
        result.data->payload["metadata"]["usage_note"] = usage.note;
    }
    result.data->usage = usage;
    return result;
}

Json::Value HttpProviderBase::transformRequest(ProviderOperation, const Json::Value &payload) const {
    return payload;
}

Json::Value HttpProviderBase::transformResponse(ProviderOperation, const Json::Value &payload) const {
    return payload;
}

void HttpProviderBase::augmentHeaders(cpr::Header &) const {}

ProviderResult<HttpProviderBase::JsonOperationResult> HttpProviderBase::performHttpRequest(
    ProviderOperation operation, const Json::Value &payload, const RequestContext &context, const std::string &resourceId) {
    ProviderResult<JsonOperationResult> result;

    if (config_.dryRun) {
        result.error = makeDryRunError(context);
        return result;
    }

    if (config_.baseUrl.empty()) {
        result.error = makeProviderError("missing_base_url",
                                         "Provider base URL is not configured. Please update providers.yaml.",
                                         context.requestId,
                                         0.0);
        return result;
    }

    if (config_.apiKeyEnv.empty()) {
        result.error = makeProviderError("missing_api_key_env",
                                         "Provider API key environment variable is not configured.",
                                         context.requestId,
                                         0.0);
        return result;
    }

    auto apiKey = superapi::getEnv(config_.apiKeyEnv);
    if (!apiKey.has_value() || apiKey->empty()) {
        result.error = makeAuthError("missing_api_key", "API key environment variable is empty or undefined.", context);
        return result;
    }

    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard guard(mutex_);
        if (breaker_.openUntil != std::chrono::steady_clock::time_point{} && now < breaker_.openUntil) {
            return makeCircuitOpenError(context);
        }
        if (breaker_.openUntil != std::chrono::steady_clock::time_point{} && now >= breaker_.openUntil) {
            breaker_.openUntil = {};
            breaker_.failures = 0;
        }
    }

    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    auto body = Json::writeString(writerBuilder, payload);

    auto url = buildUrl(operation, resourceId);

    bool isPost = isPostOperation(operation);

    std::size_t attempts = config_.maxRetries > 0 ? config_.maxRetries : 0;

    ProviderError lastError;
    std::string providerRequestId;
    double retryAfterSeconds = 0.0;

    for (std::size_t attempt = 0; attempt <= attempts; ++attempt) {
        cpr::Session session;
        session.SetUrl(cpr::Url{url});
        session.SetTimeout(cpr::Timeout{static_cast<int>(config_.timeout.count())});
        session.SetConnectTimeout(cpr::ConnectTimeout{static_cast<int>(config_.connectTimeout.count())});

        auto headers = buildHeaders(*apiKey, context);
        augmentHeaders(headers);
        session.SetHeader(headers);

        if (isPost) {
            session.SetBody(cpr::Body{body});
        }

        cpr::Response response = isPost ? session.Post() : session.Get();
        providerRequestId = extractRequestId(response);
        retryAfterSeconds = parseRetryAfter(response);

        if (response.error.code != cpr::ErrorCode::OK) {
            lastError = makeProviderError("network_error", response.error.message, providerRequestId.empty() ? context.requestId : providerRequestId, retryAfterSeconds);
        } else if (response.status_code == 401 || response.status_code == 403) {
            lastError = makeAuthError(std::to_string(response.status_code),
                                      response.text.empty() ? "Authentication with upstream provider failed." : response.text,
                                      context);
            lastError.requestId = providerRequestId.empty() ? context.requestId : providerRequestId;
            result.error = lastError;
            {
                std::lock_guard guard(mutex_);
                breaker_.failures += 1;
                if (breaker_.failures >= config_.circuitBreakerThreshold) {
                    breaker_.openUntil = std::chrono::steady_clock::now() + config_.circuitBreakerCooldown;
                }
            }
            return result;
        } else if (response.status_code >= 200 && response.status_code < 300) {
            Json::Value payloadResponse;
            std::string errs;
            auto reader = std::unique_ptr<Json::CharReader>(Json::CharReaderBuilder().newCharReader());
            if (!reader->parse(response.text.c_str(), response.text.c_str() + response.text.size(), &payloadResponse, &errs)) {
                lastError = makeProviderError("invalid_json",
                                              errs.empty() ? "Provider returned invalid JSON payload." : errs,
                                              providerRequestId.empty() ? context.requestId : providerRequestId,
                                              retryAfterSeconds);
            } else {
                JsonOperationResult operationResult;
                operationResult.payload = payloadResponse;
                operationResult.providerRequestId = providerRequestId;
                operationResult.usage = extractUsage(operationResult.payload);
                result.data = operationResult;
                {
                    std::lock_guard guard(mutex_);
                    breaker_.failures = 0;
                    breaker_.openUntil = {};
                }
                return result;
            }
        } else {
            std::string message = response.text.empty() ? "Provider returned an error response." : response.text;
            lastError = makeProviderError(std::to_string(response.status_code), message,
                                          providerRequestId.empty() ? context.requestId : providerRequestId,
                                          retryAfterSeconds);
        }

        bool shouldRetry = (response.error.code != cpr::ErrorCode::OK) || response.status_code == 429 ||
                           (response.status_code >= 500 && response.status_code < 600);
        if (attempt == attempts || !shouldRetry) {
            result.error = lastError;
            {
                std::lock_guard guard(mutex_);
                breaker_.failures += 1;
                if (breaker_.failures >= config_.circuitBreakerThreshold) {
                    breaker_.openUntil = std::chrono::steady_clock::now() + config_.circuitBreakerCooldown;
                }
            }
            return result;
        }

        auto delay = computeBackoff(config_.baseBackoff, config_.maxBackoff, attempt, rng_);
        std::this_thread::sleep_for(delay);
    }

    result.error = lastError;
    return result;
}

ProviderError HttpProviderBase::makeDryRunError(const RequestContext &context) const {
    ProviderError error;
    error.type = "dry_run";
    error.message = "DRY_RUN is enabled; upstream call skipped.";
    error.provider = config_.name;
    error.code = "dry_run";
    error.requestId = context.requestId;
    error.retryAfter = 0.0;
    return error;
}

ProviderError HttpProviderBase::makeAuthError(const std::string &code,
                                              const std::string &message,
                                              const RequestContext &context) const {
    ProviderError error;
    error.type = "auth_error";
    error.message = message;
    error.provider = config_.name;
    error.code = code;
    error.requestId = context.requestId;
    error.retryAfter = 0.0;
    return error;
}

ProviderError HttpProviderBase::makeProviderError(const std::string &code,
                                                  const std::string &message,
                                                  const std::string &requestId,
                                                  double retryAfter) const {
    ProviderError error;
    error.type = "provider_error";
    error.message = message;
    error.provider = config_.name;
    error.code = code;
    error.requestId = requestId;
    error.retryAfter = retryAfter;
    return error;
}

std::string HttpProviderBase::buildUrl(ProviderOperation operation, const std::string &resourceId) const {
    std::string base = config_.baseUrl;
    if (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    auto path = resolvePath(operation, resourceId);
    if (!path.empty() && path.front() != '/') {
        base.push_back('/');
    }
    base.append(path);
    return base;
}

std::string HttpProviderBase::resolvePath(ProviderOperation operation, const std::string &resourceId) const {
    switch (operation) {
        case ProviderOperation::Chat:
            return "chat/completions";
        case ProviderOperation::Embeddings:
            return "embeddings";
        case ProviderOperation::Images:
            return "images/generations";
        case ProviderOperation::Asr:
            return "audio/transcriptions";
        case ProviderOperation::Tts:
            return "audio/speech";
        case ProviderOperation::Video:
            return "video/generations";
        case ProviderOperation::ListModels:
            return "models";
        case ProviderOperation::Batches:
            return "batches";
        case ProviderOperation::JobStatus:
            return "jobs/" + resourceId;
    }
    return {};
}

cpr::Header HttpProviderBase::buildHeaders(const std::string &apiKey, const RequestContext &context) const {
    cpr::Header header;
    header["Content-Type"] = "application/json";
    header["Accept"] = "application/json";
    header["User-Agent"] = "superapi_server/0.1.0";
    if (!context.requestId.empty()) {
        header["X-Request-ID"] = context.requestId;
    }

    for (const auto &entry : config_.defaultHeaders) {
        header[entry.first] = entry.second;
    }

    switch (authStrategy_) {
        case AuthStrategy::BearerAuthorization:
            header["Authorization"] = "Bearer " + apiKey;
            break;
        case AuthStrategy::XApiKey:
            header["x-api-key"] = apiKey;
            break;
        case AuthStrategy::XGoogApiKey:
            header["x-goog-api-key"] = apiKey;
            break;
        case AuthStrategy::None:
            break;
    }

    if (!config_.organizationEnv.empty()) {
        auto organization = superapi::getEnv(config_.organizationEnv);
        if (organization.has_value() && !organization->empty()) {
            header["OpenAI-Organization"] = *organization;
        }
    }

    return header;
}

ProviderResult<HttpProviderBase::JsonOperationResult> HttpProviderBase::makeCircuitOpenError(
    const RequestContext &context) const {
    ProviderResult<JsonOperationResult> result;
    result.error = makeProviderError("circuit_open",
                                     "Provider circuit breaker open after repeated failures.",
                                     context.requestId,
                                     std::chrono::duration<double>(config_.circuitBreakerCooldown).count());
    return result;
}

std::string HttpProviderBase::extractRequestId(const cpr::Response &response) const {
    static const std::vector<std::string> candidates = {"x-request-id", "x-requestid", "request-id"};
    for (const auto &candidate : candidates) {
        auto it = response.header.find(candidate);
        if (it != response.header.end()) {
            return it->second;
        }
    }

    for (const auto &entry : response.header) {
        if (toLower(entry.first) == "x-request-id") {
            return entry.second;
        }
    }
    return {};
}

double HttpProviderBase::parseRetryAfter(const cpr::Response &response) const {
    auto it = response.header.find("Retry-After");
    if (it != response.header.end()) {
        try {
            return std::stod(it->second);
        } catch (const std::exception &) {
        }
    }

    for (const auto &entry : response.header) {
        if (toLower(entry.first) == "retry-after") {
            try {
                return std::stod(entry.second);
            } catch (const std::exception &) {
            }
        }
    }
    return 0.0;
}

Usage HttpProviderBase::extractUsage(const Json::Value &payload) const {
    Usage usage;
    if (!payload.isObject()) {
        usage.note = "provider_did_not_return_usage";
        return usage;
    }

    if (!payload.isMember("usage")) {
        usage.note = "provider_did_not_return_usage";
        return usage;
    }

    const auto &rawUsage = payload["usage"];
    if (rawUsage.isMember("prompt_tokens")) {
        usage.promptTokens = rawUsage["prompt_tokens"].asUInt64();
    }
    if (rawUsage.isMember("completion_tokens")) {
        usage.completionTokens = rawUsage["completion_tokens"].asUInt64();
    }
    if (rawUsage.isMember("total_tokens")) {
        usage.totalTokens = rawUsage["total_tokens"].asUInt64();
    } else {
        usage.totalTokens = usage.promptTokens + usage.completionTokens;
    }
    if (rawUsage.isMember("audio_tokens")) {
        usage.audioTokens = rawUsage["audio_tokens"].asUInt64();
    }
    if (rawUsage.isMember("cached_tokens")) {
        usage.cachedTokens = rawUsage["cached_tokens"].asUInt64();
    }
    return usage;
}

}  // namespace superapi::providers
