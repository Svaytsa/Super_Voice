#include "superapi/providers.h"

#include "providers/AgentRouterProvider.hpp"
#include "providers/AnthropicProvider.hpp"
#include "providers/DeepSeekProvider.hpp"
#include "providers/GeminiProvider.hpp"
#include "providers/HuggingFaceProvider.hpp"
#include "providers/HttpProviderBase.hpp"
#include "providers/IProvider.hpp"
#include "providers/LamaProvider.hpp"
#include "providers/MiniMaxProvider.hpp"
#include "providers/OpenAIProvider.hpp"
#include "providers/OpenRouterProvider.hpp"
#include "providers/PerplexityProvider.hpp"
#include "providers/QwenProvider.hpp"
#include "providers/VertexProvider.hpp"
#include "providers/XAIProvider.hpp"
#include "providers/ZhipuProvider.hpp"

#include "superapi/environment.h"

#include <drogon/drogon.h>
#include <yaml-cpp/yaml.h>

#include <json/json.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace superapi {

namespace {

using providers::IProvider;
using providers::ProviderConfig;
using providers::ProviderOperation;
using providers::RequestContext;
using providers::Usage;

struct RegistryState {
    std::unordered_map<std::string, std::shared_ptr<IProvider>> providers;
    YAML::Node rawConfig;
    bool dryRun{false};
    std::mutex mutex;
};

RegistryState &registry() {
    static RegistryState state;
    return state;
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string resolveScalar(const YAML::Node &node) {
    if (!node || !node.IsScalar()) {
        return {};
    }
    auto text = node.as<std::string>("");
    if (text.size() >= 3 && text.front() == '$' && text[1] == '{' && text.back() == '}') {
        auto inner = text.substr(2, text.size() - 3);
        auto delim = inner.find(":-");
        std::string envKey = inner.substr(0, delim == std::string::npos ? inner.size() : delim);
        std::string defaultValue = delim == std::string::npos ? std::string{} : inner.substr(delim + 2);
        auto envValue = superapi::getEnv(envKey);
        if (envValue.has_value() && !envValue->empty()) {
            return *envValue;
        }
        return defaultValue;
    }
    return text;
}
std::unordered_map<std::string, std::string> parseHeaders(const YAML::Node &node) {
    std::unordered_map<std::string, std::string> headers;
    if (!node || !node.IsMap()) {
        return headers;
    }
    for (const auto &entry : node) {
        auto key = entry.first.as<std::string>("");
        auto value = resolveScalar(entry.second);
        if (!key.empty()) {
            headers.emplace(std::move(key), std::move(value));
        }
    }
    return headers;
}

std::chrono::milliseconds parseDuration(const YAML::Node &node, std::chrono::milliseconds fallback) {
    if (!node) {
        return fallback;
    }
    auto scalar = resolveScalar(node);
    if (scalar.empty()) {
        return fallback;
    }
    try {
        return std::chrono::milliseconds(std::stoll(scalar));
    } catch (const std::exception &) {
        return fallback;
    }
}

std::size_t parseSize(const YAML::Node &node, std::size_t fallback) {
    if (!node) {
        return fallback;
    }
    if (node.IsScalar()) {
        auto scalar = resolveScalar(node);
        if (scalar.empty()) {
            return fallback;
        }
        try {
            return static_cast<std::size_t>(std::stoull(scalar));
        } catch (const std::exception &) {
            return fallback;
        }
    }
    try {
        return node.as<std::size_t>(fallback);
    } catch (const std::exception &) {
        return fallback;
    }
}

ProviderConfig buildConfig(const std::string &providerKey, const YAML::Node &node, bool dryRun) {
    ProviderConfig config;
    config.name = providerKey;
    config.dryRun = dryRun;
    if (node) {
        config.baseUrl = resolveScalar(node["base_url"]);
        config.apiKeyEnv = resolveScalar(node["api_key_env"]);
        config.organizationEnv = resolveScalar(node["organization_env"]);
        config.defaultHeaders = parseHeaders(node["default_headers"]);
        config.maxRetries = parseSize(node["max_retries"], config.maxRetries);
        config.baseBackoff = parseDuration(node["base_backoff_ms"], config.baseBackoff);
        config.maxBackoff = parseDuration(node["max_backoff_ms"], config.maxBackoff);
        config.timeout = parseDuration(node["timeout_ms"], config.timeout);
        config.connectTimeout = parseDuration(node["connect_timeout_ms"], config.connectTimeout);
        config.circuitBreakerThreshold = parseSize(node["circuit_breaker_threshold"], config.circuitBreakerThreshold);
        config.circuitBreakerCooldown = parseDuration(node["circuit_breaker_cooldown_ms"], config.circuitBreakerCooldown);
    }
    return config;
}

std::string resolveRequestId(const drogon::HttpRequestPtr &request) {
    if (!request) {
        return {};
    }
    auto attributes = request->attributes();
    if (attributes) {
        try {
            return attributes->get<std::string>("request_id");
        } catch (const std::exception &) {
        }
    }
    return request->getHeader("X-Request-ID");
}

Json::Value buildErrorPayload(const providers::ProviderError &error) {
    Json::Value payload(Json::objectValue);
    Json::Value body(Json::objectValue);
    body["type"] = error.type;
    body["message"] = error.message;
    body["provider"] = error.provider;
    body["code"] = error.code;
    body["request_id"] = error.requestId;
    body["retry_after"] = error.retryAfter;
    payload["error"] = body;
    return payload;
}

drogon::HttpStatusCode statusFromErrorType(const providers::ProviderError &error) {
    const auto lowered = toLower(error.type);
    if (lowered == "auth_error") {
        return drogon::k401Unauthorized;
    }
    if (lowered == "validation_error") {
        return drogon::k400BadRequest;
    }
    if (lowered == "dry_run") {
        return drogon::k503ServiceUnavailable;
    }
    if (lowered == "provider_unavailable" || lowered == "circuit_open") {
        return drogon::k503ServiceUnavailable;
    }
    return drogon::k502BadGateway;
}

void recordUsageMetrics(const drogon::HttpRequestPtr &request, const Usage &usage) {
    if (!request) {
        return;
    }
    auto attributes = request->attributes();
    if (!attributes) {
        return;
    }
    attributes->insert("observability.tokens_out", static_cast<std::uint64_t>(usage.completionTokens));
}
void registerEndpoint(drogon::HttpAppFramework &app,
                      const std::string &routePrefix,
                      const std::string &providerKey,
                      const std::string &suffix,
                      ProviderOperation operation,
                      drogon::HttpMethod method) {
    const auto path = std::string("/") + routePrefix + "/" + suffix;
    app.registerHandler(
        path,
        [providerKey, route = std::string("/") + routePrefix + "/" + suffix, operation](
            const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            RequestContext context;
            context.requestId = resolveRequestId(req);
            auto attributes = req->attributes();
            if (attributes) {
                attributes->insert("observability.company", providerKey);
                attributes->insert("observability.endpoint", route);
            }

            auto provider = superapi::providers::getProvider(providerKey);
            if (!provider) {
                providers::ProviderError error{
                    .type = "provider_error",
                    .message = "Provider is not configured.",
                    .provider = providerKey,
                    .code = "provider_not_configured",
                    .requestId = context.requestId,
                    .retryAfter = 0.0,
                };
                auto response = drogon::HttpResponse::newHttpJsonResponse(buildErrorPayload(error));
                response->setStatusCode(drogon::k503ServiceUnavailable);
                callback(response);
                return;
            }

            auto respondWithError = [&](const providers::ProviderError &error) {
                auto payload = buildErrorPayload(error);
                auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
                response->setStatusCode(statusFromErrorType(error));
                if (error.retryAfter > 0.0) {
                    response->addHeader("Retry-After", std::to_string(error.retryAfter));
                }
                callback(response);
            };

            auto respondWithPayload = [&](const Json::Value &payload, const Usage &usage, const std::string &providerRequestId) {
                recordUsageMetrics(req, usage);
                auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
                response->setStatusCode(drogon::k200OK);
                if (!providerRequestId.empty()) {
                    response->addHeader("X-Provider-Request-ID", providerRequestId);
                }
                callback(response);
            };

            if (operation == ProviderOperation::JobStatus) {
                auto jobId = req->getParameter("id");
                if (jobId.empty()) {
                    providers::ProviderError error{
                        .type = "validation_error",
                        .message = "Missing required path parameter 'id'.",
                        .provider = providerKey,
                        .code = "missing_job_id",
                        .requestId = context.requestId,
                        .retryAfter = 0.0,
                    };
                    respondWithError(error);
                    return;
                }
                providers::JobStatusRequest request{.jobId = jobId};
                auto result = provider->jobStatus(request, context);
                if (!result.ok()) {
                    respondWithError(*result.error);
                    return;
                }
                respondWithPayload(result.data->payload, Usage{}, result.data->providerRequestId);
                return;
            }

            if (operation == ProviderOperation::ListModels) {
                providers::ListModelsRequest request{Json::Value(Json::objectValue)};
                auto result = provider->listModels(request, context);
                if (!result.ok()) {
                    respondWithError(*result.error);
                    return;
                }
                respondWithPayload(result.data->payload, Usage{}, result.data->providerRequestId);
                return;
            }

            auto jsonBody = req->getJsonObject();
            if (jsonBody == nullptr) {
                providers::ProviderError error{
                    .type = "validation_error",
                    .message = "Request body must be valid JSON.",
                    .provider = providerKey,
                    .code = "invalid_json",
                    .requestId = context.requestId,
                    .retryAfter = 0.0,
                };
                respondWithError(error);
                return;
            }

            Json::Value payload = *jsonBody;

            switch (operation) {
                case ProviderOperation::Chat: {
                    providers::ChatRequest request{payload};
                    auto result = provider->chat(request, context);
                    if (!result.ok()) {
                        respondWithError(*result.error);
                        return;
                    }
                    respondWithPayload(result.data->payload, result.data->usage, result.data->providerRequestId);
                    return;
                }
                case ProviderOperation::Embeddings: {
                    providers::EmbeddingsRequest request{payload};
                    auto result = provider->embeddings(request, context);
                    if (!result.ok()) {
                        respondWithError(*result.error);
                        return;
                    }
                    respondWithPayload(result.data->payload, result.data->usage, result.data->providerRequestId);
                    return;
                }
                case ProviderOperation::Images: {
                    providers::ImageRequest request{payload};
                    auto result = provider->images(request, context);
                    if (!result.ok()) {
                        respondWithError(*result.error);
                        return;
                    }
                    respondWithPayload(result.data->payload, Usage{}, result.data->providerRequestId);
                    return;
                }
                case ProviderOperation::Asr: {
                    providers::AsrRequest request{payload};
                    auto result = provider->asr(request, context);
                    if (!result.ok()) {
                        respondWithError(*result.error);
                        return;
                    }
                    respondWithPayload(result.data->payload, result.data->usage, result.data->providerRequestId);
                    return;
                }
                case ProviderOperation::Tts: {
                    providers::TtsRequest request{payload};
                    auto result = provider->tts(request, context);
                    if (!result.ok()) {
                        respondWithError(*result.error);
                        return;
                    }
                    respondWithPayload(result.data->payload, result.data->usage, result.data->providerRequestId);
                    return;
                }
                case ProviderOperation::Video: {
                    providers::VideoRequest request{payload};
                    auto result = provider->video(request, context);
                    if (!result.ok()) {
                        respondWithError(*result.error);
                        return;
                    }
                    respondWithPayload(result.data->payload, result.data->usage, result.data->providerRequestId);
                    return;
                }
                case ProviderOperation::Batches: {
                    providers::BatchesRequest request{payload};
                    auto result = provider->batches(request, context);
                    if (!result.ok()) {
                        respondWithError(*result.error);
                        return;
                    }
                    respondWithPayload(result.data->payload, Usage{}, result.data->providerRequestId);
                    return;
                }
                case ProviderOperation::ListModels:
                case ProviderOperation::JobStatus:
                    break;
            }

            providers::ProviderError error{
                .type = "provider_error",
                .message = "Unsupported operation.",
                .provider = providerKey,
                .code = "unsupported_operation",
                .requestId = context.requestId,
                .retryAfter = 0.0,
            };
            respondWithError(error);
        },
        {method});
}

void registerCompany(drogon::HttpAppFramework &app, const std::string &routePrefix, const std::string &providerKey) {
    registerEndpoint(app, routePrefix, providerKey, "chat/completions", ProviderOperation::Chat, drogon::Post);
    registerEndpoint(app, routePrefix, providerKey, "embeddings", ProviderOperation::Embeddings, drogon::Post);
    registerEndpoint(app, routePrefix, providerKey, "images/generations", ProviderOperation::Images, drogon::Post);
    registerEndpoint(app, routePrefix, providerKey, "audio/transcriptions", ProviderOperation::Asr, drogon::Post);
    registerEndpoint(app, routePrefix, providerKey, "audio/speech", ProviderOperation::Tts, drogon::Post);
    registerEndpoint(app, routePrefix, providerKey, "video/generations", ProviderOperation::Video, drogon::Post);
    registerEndpoint(app, routePrefix, providerKey, "models", ProviderOperation::ListModels, drogon::Get);
    registerEndpoint(app, routePrefix, providerKey, "batches", ProviderOperation::Batches, drogon::Post);
    registerEndpoint(app, routePrefix, providerKey, "jobs/{id}", ProviderOperation::JobStatus, drogon::Get);
}

}  // namespace
void validateProviderConfig(const std::filesystem::path &path) {
    try {
        const auto config = YAML::LoadFile(path.string());
        if (!config || !config["providers"]) {
            LOG_WARN << "Provider configuration is empty or missing; external integrations are disabled.";
            return;
        }

        const auto providersNode = config["providers"];
        if (!providersNode.IsMap()) {
            LOG_WARN << "Provider configuration is malformed; expected a map of providers.";
            return;
        }

        for (const auto &entry : providersNode) {
            const auto name = entry.first.as<std::string>("unknown");
            const auto provider = entry.second;
            if (!provider.IsMap()) {
                LOG_WARN << "Provider " << name << " must be an object in providers.yaml.";
                continue;
            }

            if (!provider["base_url"] || resolveScalar(provider["base_url"]).empty()) {
                LOG_WARN << "Provider " << name << " missing configuration value: base_url";
            }
            if (!provider["api_key_env"] || resolveScalar(provider["api_key_env"]).empty()) {
                LOG_WARN << "Provider " << name << " missing configuration value: api_key_env";
            }
        }
    } catch (const std::exception &ex) {
        LOG_WARN << "Unable to load provider configuration from " << path << ": " << ex.what();
    }
}

namespace providers {

void initialize(const std::filesystem::path &path, bool dryRun) {
    auto &state = registry();
    std::lock_guard guard(state.mutex);
    state.providers.clear();
    state.dryRun = dryRun;

    try {
        state.rawConfig = YAML::LoadFile(path.string());
    } catch (const std::exception &ex) {
        LOG_WARN << "Unable to load provider configuration from " << path << ": " << ex.what();
        state.rawConfig = {};
        return;
    }

    const auto providersNode = state.rawConfig["providers"];
    if (!providersNode || !providersNode.IsMap()) {
        LOG_WARN << "No providers defined in providers.yaml; upstream integrations disabled.";
        return;
    }

    const std::unordered_map<std::string, std::function<std::shared_ptr<IProvider>(ProviderConfig)>> factories = {
        {"openai", [](ProviderConfig cfg) { return std::make_shared<OpenAIProvider>(std::move(cfg)); }},
        {"anthropic", [](ProviderConfig cfg) { return std::make_shared<AnthropicProvider>(std::move(cfg)); }},
        {"xai", [](ProviderConfig cfg) { return std::make_shared<XAIProvider>(std::move(cfg)); }},
        {"perplexity", [](ProviderConfig cfg) { return std::make_shared<PerplexityProvider>(std::move(cfg)); }},
        {"lama", [](ProviderConfig cfg) { return std::make_shared<LamaProvider>(std::move(cfg)); }},
        {"vertex", [](ProviderConfig cfg) { return std::make_shared<VertexProvider>(std::move(cfg)); }},
        {"gemini", [](ProviderConfig cfg) { return std::make_shared<GeminiProvider>(std::move(cfg)); }},
        {"huggingface", [](ProviderConfig cfg) { return std::make_shared<HuggingFaceProvider>(std::move(cfg)); }},
        {"openrouter", [](ProviderConfig cfg) { return std::make_shared<OpenRouterProvider>(std::move(cfg)); }},
        {"agentrouter", [](ProviderConfig cfg) { return std::make_shared<AgentRouterProvider>(std::move(cfg)); }},
        {"deepseek", [](ProviderConfig cfg) { return std::make_shared<DeepSeekProvider>(std::move(cfg)); }},
        {"qwen", [](ProviderConfig cfg) { return std::make_shared<QwenProvider>(std::move(cfg)); }},
        {"zhipu", [](ProviderConfig cfg) { return std::make_shared<ZhipuProvider>(std::move(cfg)); }},
        {"minimax", [](ProviderConfig cfg) { return std::make_shared<MiniMaxProvider>(std::move(cfg)); }},
    };

    for (const auto &entry : factories) {
        const auto &key = entry.first;
        const auto &factory = entry.second;
        auto node = providersNode[key];
        auto config = buildConfig(key, node, dryRun);
        state.providers[key] = factory(std::move(config));
    }
}

void registerRoutes(drogon::HttpAppFramework &app) {
    registerCompany(app, "openai", "openai");
    registerCompany(app, "anthropic", "anthropic");
    registerCompany(app, "perplexity", "perplexity");
    registerCompany(app, "lama", "lama");
    registerCompany(app, "vertex", "vertex");
    registerCompany(app, "gemini", "gemini");
    registerCompany(app, "huggingface", "huggingface");
    registerCompany(app, "openrouter", "openrouter");
    registerCompany(app, "agentrouter", "agentrouter");
    registerCompany(app, "deepseek", "deepseek");
    registerCompany(app, "qwen", "qwen");
    registerCompany(app, "minimax", "minimax");

    const std::vector<std::pair<std::string, ProviderOperation>> xaiOperations = {
        {"chat/completions", ProviderOperation::Chat},
        {"embeddings", ProviderOperation::Embeddings},
        {"images/generations", ProviderOperation::Images},
        {"audio/transcriptions", ProviderOperation::Asr},
        {"audio/speech", ProviderOperation::Tts},
        {"video/generations", ProviderOperation::Video},
        {"models", ProviderOperation::ListModels},
        {"batches", ProviderOperation::Batches},
        {"jobs/{id}", ProviderOperation::JobStatus},
    };

    for (const auto &[suffix, operation] : xaiOperations) {
        const auto method = (operation == ProviderOperation::ListModels || operation == ProviderOperation::JobStatus)
                                ? drogon::Get
                                : drogon::Post;
        const auto route = std::string("/xai/") + suffix;
        app.registerHandler(
            route,
            [operation, suffix](const drogon::HttpRequestPtr &req,
                        std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
                RequestContext context;
                context.requestId = resolveRequestId(req);

                auto vendor = req->getParameter("vendor");
                if (vendor.empty()) {
                    vendor = req->getHeader("X-Vendor");
                }
                const auto vendorLower = toLower(vendor);
                std::string providerKey;
                if (vendorLower == "grok") {
                    providerKey = "xai";
                } else if (vendorLower == "zhipu") {
                    providerKey = "zhipu";
                } else {
                    providers::ProviderError error{
                        .type = "validation_error",
                        .message = "vendor query parameter or X-Vendor header must be 'grok' or 'zhipu'.",
                        .provider = "xai",
                        .code = "invalid_vendor",
                        .requestId = context.requestId,
                        .retryAfter = 0.0,
                    };
                    auto response = drogon::HttpResponse::newHttpJsonResponse(buildErrorPayload(error));
                    response->setStatusCode(drogon::k400BadRequest);
                    callback(response);
                    return;
                }
                context.vendor = vendorLower;

                auto attributes = req->attributes();
                if (attributes) {
                    attributes->insert("observability.company", providerKey);
                    attributes->insert("observability.endpoint", std::string("/xai/") + suffix);
                }

                auto provider = superapi::providers::getProvider(providerKey);
                if (!provider) {
                    providers::ProviderError error{
                        .type = "provider_error",
                        .message = "Provider is not configured.",
                        .provider = providerKey,
                        .code = "provider_not_configured",
                        .requestId = context.requestId,
                        .retryAfter = 0.0,
                    };
                    auto response = drogon::HttpResponse::newHttpJsonResponse(buildErrorPayload(error));
                    response->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(response);
                    return;
                }

                auto respondError = [&](const providers::ProviderError &error) {
                    auto payload = buildErrorPayload(error);
                    auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
                    response->setStatusCode(statusFromErrorType(error));
                    if (error.retryAfter > 0.0) {
                        response->addHeader("Retry-After", std::to_string(error.retryAfter));
                    }
                    callback(response);
                };

                if (operation == ProviderOperation::JobStatus) {
                    auto jobId = req->getParameter("id");
                    if (jobId.empty()) {
                        providers::ProviderError error{
                            .type = "validation_error",
                            .message = "Missing required path parameter 'id'.",
                            .provider = providerKey,
                            .code = "missing_job_id",
                            .requestId = context.requestId,
                            .retryAfter = 0.0,
                        };
                        respondError(error);
                        return;
                    }
                    providers::JobStatusRequest request{.jobId = jobId};
                    auto result = provider->jobStatus(request, context);
                    if (!result.ok()) {
                        respondError(*result.error);
                        return;
                    }
                    recordUsageMetrics(req, Usage{});
                    auto response = drogon::HttpResponse::newHttpJsonResponse(result.data->payload);
                    response->setStatusCode(drogon::k200OK);
                    if (!result.data->providerRequestId.empty()) {
                        response->addHeader("X-Provider-Request-ID", result.data->providerRequestId);
                    }
                    callback(response);
                    return;
                }

                if (operation == ProviderOperation::ListModels) {
                    providers::ListModelsRequest request{Json::Value(Json::objectValue)};
                    auto result = provider->listModels(request, context);
                    if (!result.ok()) {
                        respondError(*result.error);
                        return;
                    }
                    recordUsageMetrics(req, Usage{});
                    auto response = drogon::HttpResponse::newHttpJsonResponse(result.data->payload);
                    response->setStatusCode(drogon::k200OK);
                    if (!result.data->providerRequestId.empty()) {
                        response->addHeader("X-Provider-Request-ID", result.data->providerRequestId);
                    }
                    callback(response);
                    return;
                }

                auto jsonBody = req->getJsonObject();
                if (!jsonBody) {
                    providers::ProviderError error{
                        .type = "validation_error",
                        .message = "Request body must be valid JSON.",
                        .provider = providerKey,
                        .code = "invalid_json",
                        .requestId = context.requestId,
                        .retryAfter = 0.0,
                    };
                    respondError(error);
                    return;
                }

                Json::Value payload = *jsonBody;

                switch (operation) {
                    case ProviderOperation::Chat:
                        if (auto result = provider->chat(providers::ChatRequest{payload}, context); !result.ok()) {
                            respondError(*result.error);
                        } else {
                            recordUsageMetrics(req, result.data->usage);
                            auto response = drogon::HttpResponse::newHttpJsonResponse(result.data->payload);
                            response->setStatusCode(drogon::k200OK);
                            if (!result.data->providerRequestId.empty()) {
                                response->addHeader("X-Provider-Request-ID", result.data->providerRequestId);
                            }
                            callback(response);
                        }
                        return;
                    case ProviderOperation::Embeddings:
                        if (auto result =
                                provider->embeddings(providers::EmbeddingsRequest{payload}, context);
                            !result.ok()) {
                            respondError(*result.error);
                        } else {
                            recordUsageMetrics(req, result.data->usage);
                            auto response = drogon::HttpResponse::newHttpJsonResponse(result.data->payload);
                            response->setStatusCode(drogon::k200OK);
                            if (!result.data->providerRequestId.empty()) {
                                response->addHeader("X-Provider-Request-ID", result.data->providerRequestId);
                            }
                            callback(response);
                        }
                        return;
                    case ProviderOperation::Images:
                        if (auto result = provider->images(providers::ImageRequest{payload}, context); !result.ok()) {
                            respondError(*result.error);
                        } else {
                            recordUsageMetrics(req, Usage{});
                            auto response = drogon::HttpResponse::newHttpJsonResponse(result.data->payload);
                            response->setStatusCode(drogon::k200OK);
                            if (!result.data->providerRequestId.empty()) {
                                response->addHeader("X-Provider-Request-ID", result.data->providerRequestId);
                            }
                            callback(response);
                        }
                        return;
                    case ProviderOperation::Asr:
                        if (auto result = provider->asr(providers::AsrRequest{payload}, context); !result.ok()) {
                            respondError(*result.error);
                        } else {
                            recordUsageMetrics(req, result.data->usage);
                            auto response = drogon::HttpResponse::newHttpJsonResponse(result.data->payload);
                            response->setStatusCode(drogon::k200OK);
                            if (!result.data->providerRequestId.empty()) {
                                response->addHeader("X-Provider-Request-ID", result.data->providerRequestId);
                            }
                            callback(response);
                        }
                        return;
                    case ProviderOperation::Tts:
                        if (auto result = provider->tts(providers::TtsRequest{payload}, context); !result.ok()) {
                            respondError(*result.error);
                        } else {
                            recordUsageMetrics(req, result.data->usage);
                            auto response = drogon::HttpResponse::newHttpJsonResponse(result.data->payload);
                            response->setStatusCode(drogon::k200OK);
                            if (!result.data->providerRequestId.empty()) {
                                response->addHeader("X-Provider-Request-ID", result.data->providerRequestId);
                            }
                            callback(response);
                        }
                        return;
                    case ProviderOperation::Video:
                        if (auto result = provider->video(providers::VideoRequest{payload}, context); !result.ok()) {
                            respondError(*result.error);
                        } else {
                            recordUsageMetrics(req, result.data->usage);
                            auto response = drogon::HttpResponse::newHttpJsonResponse(result.data->payload);
                            response->setStatusCode(drogon::k200OK);
                            if (!result.data->providerRequestId.empty()) {
                                response->addHeader("X-Provider-Request-ID", result.data->providerRequestId);
                            }
                            callback(response);
                        }
                        return;
                    case ProviderOperation::Batches:
                        if (auto result = provider->batches(providers::BatchesRequest{payload}, context); !result.ok()) {
                            respondError(*result.error);
                        } else {
                            recordUsageMetrics(req, Usage{});
                            auto response = drogon::HttpResponse::newHttpJsonResponse(result.data->payload);
                            response->setStatusCode(drogon::k200OK);
                            if (!result.data->providerRequestId.empty()) {
                                response->addHeader("X-Provider-Request-ID", result.data->providerRequestId);
                            }
                            callback(response);
                        }
                        return;
                    case ProviderOperation::ListModels:
                    case ProviderOperation::JobStatus:
                        break;
                }

                providers::ProviderError error{
                    .type = "provider_error",
                    .message = "Unsupported operation.",
                    .provider = providerKey,
                    .code = "unsupported_operation",
                    .requestId = context.requestId,
                    .retryAfter = 0.0,
                };
                respondError(error);
            },
            {method});
    }
}

std::shared_ptr<IProvider> getProvider(const std::string &key) {
    auto &state = registry();
    std::lock_guard guard(state.mutex);
    auto it = state.providers.find(key);
    if (it != state.providers.end()) {
        return it->second;
    }
    return nullptr;
}

}  // namespace providers

}  // namespace superapi
