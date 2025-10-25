#include "http/CompanyNamespace.hpp"

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace superapi::http {
namespace {

using drogon::HttpRequestPtr;
using drogon::HttpResponsePtr;

std::string getRequestId(const HttpRequestPtr &req) {
    if (!req) {
        return {};
    }
    auto attributes = req->attributes();
    if (attributes) {
        try {
            return attributes->get<std::string>("request_id");
        } catch (const std::exception &) {
        }
    }
    return req->getHeader("X-Request-ID");
}

HttpResponsePtr makeErrorResponse(drogon::HttpStatusCode status,
                                  const std::string &company,
                                  const std::string &message,
                                  const HttpRequestPtr &req,
                                  std::string_view type = "invalid_request",
                                  std::optional<double> retryAfter = std::nullopt) {
    Json::Value root(Json::objectValue);
    Json::Value error(Json::objectValue);
    error["type"] = std::string(type);
    error["message"] = message;
    error["provider"] = company;
    error["code"] = static_cast<Json::Value::Int>(status);
    error["request_id"] = getRequestId(req);
    error["retry_after"] = retryAfter.value_or(0.0);
    root["error"] = std::move(error);

    auto response = drogon::HttpResponse::newHttpJsonResponse(root);
    response->setStatusCode(status);
    return response;
}

std::string toJsonString(const Json::Value &value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

bool parseJsonBody(const HttpRequestPtr &req, Json::Value &json, std::string &error) {
    if (!req) {
        error = "empty request";
        return false;
    }
    auto body = req->getBody();
    if (body.empty()) {
        error = "request body must not be empty";
        return false;
    }
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(body.data(), body.data() + body.size(), &json, &errs)) {
        error = errs.empty() ? "invalid JSON payload" : errs;
        return false;
    }
    if (!json.isObject()) {
        error = "request body must be a JSON object";
        return false;
    }
    return true;
}

bool validateMessages(const Json::Value &messages, std::string &error) {
    if (!messages.isArray() || messages.empty()) {
        error = "messages must be a non-empty array";
        return false;
    }
    for (const auto &message : messages) {
        if (!message.isObject()) {
            error = "each message must be an object";
            return false;
        }
        if (!message.isMember("role") || !message["role"].isString()) {
            error = "message.role must be a string";
            return false;
        }
        static const std::vector<std::string> roles{"system", "user", "assistant", "tool", "function"};
        const auto &roleValue = message["role"].asString();
        if (std::find(roles.begin(), roles.end(), roleValue) == roles.end()) {
            error = "message.role is not supported";
            return false;
        }
        if (message.isMember("content")) {
            const auto &content = message["content"];
            if (!(content.isString() || (content.isArray() && content.size() > 0))) {
                error = "message.content must be a string or array";
                return false;
            }
        }
    }
    return true;
}

bool validateChatRequest(const Json::Value &json, std::string &error) {
    if (!json.isMember("model") || !json["model"].isString()) {
        error = "model is required and must be a string";
        return false;
    }
    if (!json.isMember("messages")) {
        error = "messages field is required";
        return false;
    }
    if (!validateMessages(json["messages"], error)) {
        return false;
    }
    if (json.isMember("temperature") && !json["temperature"].isNumeric()) {
        error = "temperature must be a number";
        return false;
    }
    if (json.isMember("top_p") && !json["top_p"].isNumeric()) {
        error = "top_p must be a number";
        return false;
    }
    if (json.isMember("max_tokens") && !json["max_tokens"].isInt()) {
        error = "max_tokens must be an integer";
        return false;
    }
    if (json.isMember("stream") && !json["stream"].isBool()) {
        error = "stream must be a boolean";
        return false;
    }
    if (json.isMember("tools") && !json["tools"].isArray()) {
        error = "tools must be an array";
        return false;
    }
    if (json.isMember("response_format") && !(json["response_format"].isString() || json["response_format"].isObject())) {
        error = "response_format must be a string or object";
        return false;
    }
    return true;
}

bool validateEmbeddingsRequest(const Json::Value &json, std::string &error) {
    if (!json.isMember("model") || !json["model"].isString()) {
        error = "model is required and must be a string";
        return false;
    }
    if (!json.isMember("input")) {
        error = "input is required";
        return false;
    }
    const auto &input = json["input"];
    if (!(input.isString() || (input.isArray() && input.size() > 0))) {
        error = "input must be a string or a non-empty array of strings";
        return false;
    }
    if (input.isArray()) {
        for (const auto &item : input) {
            if (!item.isString()) {
                error = "input array must contain strings";
                return false;
            }
        }
    }
    if (json.isMember("encoding_format") && !json["encoding_format"].isString()) {
        error = "encoding_format must be a string";
        return false;
    }
    return true;
}

bool validateImageRequest(const Json::Value &json, std::string &error) {
    if (!json.isMember("model") || !json["model"].isString()) {
        error = "model is required and must be a string";
        return false;
    }
    if (!json.isMember("prompt") || !json["prompt"].isString()) {
        error = "prompt is required and must be a string";
        return false;
    }
    if (json.isMember("n") && !json["n"].isInt()) {
        error = "n must be an integer";
        return false;
    }
    if (json.isMember("size") && !json["size"].isString()) {
        error = "size must be a string";
        return false;
    }
    return true;
}

bool validateTranscriptionRequest(const Json::Value &json, std::string &error) {
    if (!json.isMember("model") || !json["model"].isString()) {
        error = "model is required and must be a string";
        return false;
    }
    if (!json.isMember("file") || !json["file"].isString()) {
        error = "file is required and must be a string";
        return false;
    }
    if (json.isMember("temperature") && !json["temperature"].isNumeric()) {
        error = "temperature must be a number";
        return false;
    }
    return true;
}

bool validateSpeechRequest(const Json::Value &json, std::string &error) {
    if (!json.isMember("model") || !json["model"].isString()) {
        error = "model is required and must be a string";
        return false;
    }
    if (!json.isMember("input") || !json["input"].isString()) {
        error = "input is required and must be a string";
        return false;
    }
    if (json.isMember("format") && !json["format"].isString()) {
        error = "format must be a string";
        return false;
    }
    if (json.isMember("speed") && !json["speed"].isNumeric()) {
        error = "speed must be numeric";
        return false;
    }
    return true;
}

bool validateVideoRequest(const Json::Value &json, std::string &error) {
    if (!json.isMember("model") || !json["model"].isString()) {
        error = "model is required and must be a string";
        return false;
    }
    if (!json.isMember("prompt") || !json["prompt"].isString()) {
        error = "prompt is required and must be a string";
        return false;
    }
    if (json.isMember("duration_seconds") && !json["duration_seconds"].isInt()) {
        error = "duration_seconds must be an integer";
        return false;
    }
    return true;
}

bool validateBatchRequest(const Json::Value &json, std::string &error) {
    if (!json.isMember("input_file_id") || !json["input_file_id"].isString()) {
        error = "input_file_id is required and must be a string";
        return false;
    }
    if (!json.isMember("endpoint") || !json["endpoint"].isString()) {
        error = "endpoint is required and must be a string";
        return false;
    }
    return true;
}

bool wantsEventStream(const HttpRequestPtr &req, const Json::Value &body) {
    if (body.isMember("stream") && body["stream"].isBool() && body["stream"].asBool()) {
        return true;
    }
    const auto accept = req->getHeader("Accept");
    return accept.find("text/event-stream") != std::string::npos;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool ensureVendorSelected(const HttpRequestPtr &req,
                          bool requireVendor,
                          std::string &vendor,
                          const std::string &company,
                          HttpResponsePtr &error) {
    if (!requireVendor) {
        return true;
    }
    vendor = req->getParameter("vendor");
    if (vendor.empty()) {
        vendor = req->getHeader("X-Vendor");
    }
    vendor = lower(vendor);
    if (vendor == "grok" || vendor == "zhipu") {
        if (auto attributes = req->attributes()) {
            attributes->insert("observability.vendor", vendor);
        }
        return true;
    }
    error = makeErrorResponse(drogon::k400BadRequest, company, "vendor must be either 'grok' or 'zhipu'", req, "missing_vendor");
    return false;
}

void setTokensOut(const HttpRequestPtr &req, std::uint64_t tokens) {
    if (auto attributes = req->attributes()) {
        attributes->insert("observability.tokens_out", tokens);
    }
}

void setStreamEvents(const HttpRequestPtr &req, std::uint64_t events) {
    if (auto attributes = req->attributes()) {
        attributes->insert("observability.stream_events", events);
    }
}

std::string makeIdentifier(const std::string &company, std::string_view kind) {
    const auto now = std::chrono::system_clock::now();
    const auto ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::ostringstream oss;
    oss << "dryrun-" << company << '-' << kind << '-' << ts;
    return oss.str();
}

Json::Value buildChatCompletion(const Json::Value &request, const std::string &company) {
    Json::Value response(Json::objectValue);
    response["id"] = makeIdentifier(company, "chat");
    response["object"] = "chat.completion";
    response["created"] = static_cast<Json::Value::Int64>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    response["model"] = request["model"].asString();

    Json::Value choice(Json::objectValue);
    choice["index"] = 0;
    Json::Value message(Json::objectValue);
    message["role"] = "assistant";
    message["content"] = "This is a dry-run response for " + company + " chat completions.";
    choice["message"] = std::move(message);
    choice["finish_reason"] = "stop";

    Json::Value choices(Json::arrayValue);
    choices.append(std::move(choice));
    response["choices"] = std::move(choices);

    Json::Value usage(Json::objectValue);
    usage["prompt_tokens"] = 32;
    usage["completion_tokens"] = 64;
    usage["total_tokens"] = 96;
    response["usage"] = std::move(usage);

    response["system_fingerprint"] = "dry-run";
    return response;
}

HttpResponsePtr makeNotImplemented(const std::string &company, const HttpRequestPtr &req) {
    return makeErrorResponse(drogon::k501NotImplemented,
                             company,
                             "Provider integration has not been implemented yet.",
                             req,
                             "not_implemented");
}

HttpResponsePtr buildChatSseResponse(const Json::Value &request, const std::string &company, const HttpRequestPtr &req) {
    Json::Value chunk = buildChatCompletion(request, company);
    chunk["object"] = "chat.completion.chunk";

    Json::Value firstChoice = chunk["choices"][0];
    firstChoice.removeMember("message");
    Json::Value delta(Json::objectValue);
    delta["role"] = "assistant";
    delta["content"] = "Streaming response from " + company + " (dry run)";
    firstChoice["delta"] = std::move(delta);
    chunk["choices"][0] = std::move(firstChoice);

    Json::Value done(Json::objectValue);
    done["type"] = "done";
    done["done"] = true;

    const std::string deltaFrame = "event: delta\ndata: " + toJsonString(chunk) + "\n\n";
    const std::string doneFrame = "event: done\ndata: {}\n\n";

    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(drogon::k200OK);
    response->setContentTypeString("text/event-stream");
    response->addHeader("Cache-Control", "no-cache");
    response->addHeader("Connection", "keep-alive");
    response->setBody(deltaFrame + doneFrame);

    setTokensOut(req, 64);
    setStreamEvents(req, 2);
    response->addHeader("X-Tokens-Out", "64");

    return response;
}

HttpResponsePtr buildChatResponse(const Json::Value &request, const std::string &company, const HttpRequestPtr &req) {
    auto payload = buildChatCompletion(request, company);
    auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
    response->setStatusCode(drogon::k200OK);
    setTokensOut(req, 64);
    response->addHeader("X-Tokens-Out", "64");
    return response;
}

HttpResponsePtr buildEmbeddingsResponse(const Json::Value &request, const std::string &company, const HttpRequestPtr &req) {
    Json::Value payload(Json::objectValue);
    payload["object"] = "list";
    payload["model"] = request["model"].asString();

    Json::Value data(Json::arrayValue);
    const auto appendEmbedding = [&](const std::string &input, std::uint32_t index) {
        Json::Value item(Json::objectValue);
        item["object"] = "embedding";
        Json::Value embedding(Json::arrayValue);
        embedding.append(0.01 * static_cast<double>(index + 1));
        embedding.append(0.02 * static_cast<double>(index + 1));
        embedding.append(0.03 * static_cast<double>(index + 1));
        item["embedding"] = std::move(embedding);
        item["index"] = index;
        data.append(std::move(item));
    };

    if (request["input"].isString()) {
        appendEmbedding(request["input"].asString(), 0);
    } else {
        std::uint32_t idx = 0;
        for (const auto &item : request["input"]) {
            appendEmbedding(item.asString(), idx++);
        }
    }
    payload["data"] = std::move(data);

    Json::Value usage(Json::objectValue);
    usage["prompt_tokens"] = 8;
    usage["total_tokens"] = 8;
    payload["usage"] = std::move(usage);

    auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
    response->setStatusCode(drogon::k200OK);
    setTokensOut(req, 0);
    response->addHeader("X-Tokens-Out", "0");
    return response;
}

HttpResponsePtr buildImageResponse(const Json::Value &request, const std::string &company, const HttpRequestPtr &req) {
    Json::Value payload(Json::objectValue);
    payload["created"] = static_cast<Json::Value::Int64>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    Json::Value images(Json::arrayValue);
    Json::Value data(Json::objectValue);
    data["url"] = "https://example.com/dry-run/" + company + "/image.png";
    data["revised_prompt"] = request["prompt"].asString();
    images.append(std::move(data));
    payload["data"] = std::move(images);

    auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
    response->setStatusCode(drogon::k200OK);
    setTokensOut(req, 32);
    response->addHeader("X-Tokens-Out", "32");
    return response;
}

HttpResponsePtr buildTranscriptionResponse(const Json::Value &request, const std::string &company, const HttpRequestPtr &req) {
    Json::Value payload(Json::objectValue);
    payload["text"] = "Transcription (dry-run) for " + company;
    Json::Value segments(Json::arrayValue);
    Json::Value segment(Json::objectValue);
    segment["id"] = 0;
    segment["start"] = 0.0;
    segment["end"] = 1.5;
    segment["text"] = "Hello world";
    segments.append(std::move(segment));
    payload["segments"] = std::move(segments);
    payload["language"] = "en";
    Json::Value usage(Json::objectValue);
    usage["prompt_tokens"] = 12;
    usage["total_tokens"] = 12;
    payload["usage"] = std::move(usage);

    auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
    response->setStatusCode(drogon::k200OK);
    setTokensOut(req, 12);
    response->addHeader("X-Tokens-Out", "12");
    return response;
}

HttpResponsePtr buildSpeechResponse(const Json::Value &request, const std::string &company, const HttpRequestPtr &req) {
    Json::Value payload(Json::objectValue);
    payload["audio"] = "U1RBVElDX0RSWV9SVU4=";  // "STATIC_DRY_RUN" base64
    payload["format"] = request.get("format", "mp3").asString();
    payload["duration_seconds"] = 1.2;
    Json::Value usage(Json::objectValue);
    usage["prompt_tokens"] = 16;
    usage["total_tokens"] = 16;
    payload["usage"] = std::move(usage);

    auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
    response->setStatusCode(drogon::k200OK);
    setTokensOut(req, 16);
    response->addHeader("X-Tokens-Out", "16");
    return response;
}

HttpResponsePtr buildVideoResponse(const Json::Value &request, const std::string &company, const HttpRequestPtr &req) {
    Json::Value payload(Json::objectValue);
    payload["id"] = makeIdentifier(company, "video");
    payload["status"] = "processing";
    payload["created"] = static_cast<Json::Value::Int64>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    payload["url"] = "https://example.com/dry-run/" + company + "/video.mp4";
    payload["preview_image_url"] = "https://example.com/dry-run/" + company + "/poster.png";
    Json::Value usage(Json::objectValue);
    usage["prompt_tokens"] = 40;
    usage["total_tokens"] = 40;
    payload["usage"] = std::move(usage);

    auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
    response->setStatusCode(drogon::k202Accepted);
    setTokensOut(req, 40);
    response->addHeader("X-Tokens-Out", "40");
    return response;
}

HttpResponsePtr buildModelsResponse(const std::string &company, const HttpRequestPtr &req) {
    Json::Value payload(Json::objectValue);
    payload["object"] = "list";
    Json::Value data(Json::arrayValue);

    auto appendModel = [&](const std::string &id, const std::vector<std::string> &modalities) {
        Json::Value model(Json::objectValue);
        model["id"] = id;
        model["object"] = "model";
        model["created"] = static_cast<Json::Value::Int64>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
        model["owned_by"] = company;
        Json::Value capabilities(Json::objectValue);
        Json::Value modArray(Json::arrayValue);
        for (const auto &mode : modalities) {
            modArray.append(mode);
        }
        capabilities["modalities"] = std::move(modArray);
        capabilities["supports_streaming"] = true;
        capabilities["supports_tool_calls"] = true;
        model["capabilities"] = std::move(capabilities);
        data.append(std::move(model));
    };

    appendModel(company + "-chat-large", {"text"});
    appendModel(company + "-multimodal", {"text", "image"});

    payload["data"] = std::move(data);

    auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
    response->setStatusCode(drogon::k200OK);
    response->addHeader("X-Tokens-Out", "0");
    setTokensOut(req, 0);
    return response;
}

HttpResponsePtr buildBatchResponse(const Json::Value &request, const std::string &company, const HttpRequestPtr &req) {
    Json::Value payload(Json::objectValue);
    payload["id"] = makeIdentifier(company, "batch");
    payload["object"] = "batch";
    payload["status"] = "in_progress";
    payload["created_at"] = static_cast<Json::Value::Int64>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    payload["request_counts"] = Json::Value(Json::objectValue);
    payload["request_counts"]["total"] = 1;
    payload["request_counts"]["succeeded"] = 0;
    payload["request_counts"]["failed"] = 0;

    auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
    response->setStatusCode(drogon::k201Created);
    response->addHeader("X-Tokens-Out", "0");
    setTokensOut(req, 0);
    return response;
}

HttpResponsePtr buildJobResponse(const std::string &jobId, const std::string &company, const HttpRequestPtr &req) {
    Json::Value payload(Json::objectValue);
    payload["id"] = jobId;
    payload["object"] = "job";
    payload["status"] = "completed";
    payload["type"] = "batch";
    payload["created_at"] = static_cast<Json::Value::Int64>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) - 60);
    payload["finished_at"] = static_cast<Json::Value::Int64>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    payload["result"] = Json::Value(Json::objectValue);

    auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
    response->setStatusCode(drogon::k200OK);
    response->addHeader("X-Tokens-Out", "0");
    setTokensOut(req, 0);
    return response;
}

}  // namespace

void registerCompanyNamespace(const std::string &company, bool dryRun, bool requireVendor) {
    auto &app = drogon::app();

    const auto chatPath = "/" + company + "/chat/completions";
    app.registerHandler(
        chatPath,
        [company, dryRun, requireVendor](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
            HttpResponsePtr error;
            std::string vendor;
            if (!ensureVendorSelected(req, requireVendor, vendor, company, error)) {
                callback(error);
                return;
            }
            if (!dryRun) {
                callback(makeNotImplemented(company, req));
                return;
            }
            Json::Value body;
            std::string parseError;
            if (!parseJsonBody(req, body, parseError)) {
                callback(makeErrorResponse(drogon::k400BadRequest, company, parseError, req));
                return;
            }
            std::string validationError;
            if (!validateChatRequest(body, validationError)) {
                callback(makeErrorResponse(drogon::k422UnprocessableEntity, company, validationError, req));
                return;
            }
            if (wantsEventStream(req, body)) {
                callback(buildChatSseResponse(body, company, req));
                return;
            }
            callback(buildChatResponse(body, company, req));
        },
        {drogon::Post});

    const auto embeddingsPath = "/" + company + "/embeddings";
    app.registerHandler(
        embeddingsPath,
        [company, dryRun, requireVendor](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
            HttpResponsePtr error;
            std::string vendor;
            if (!ensureVendorSelected(req, requireVendor, vendor, company, error)) {
                callback(error);
                return;
            }
            if (!dryRun) {
                callback(makeNotImplemented(company, req));
                return;
            }
            Json::Value body;
            std::string parseError;
            if (!parseJsonBody(req, body, parseError)) {
                callback(makeErrorResponse(drogon::k400BadRequest, company, parseError, req));
                return;
            }
            std::string validationError;
            if (!validateEmbeddingsRequest(body, validationError)) {
                callback(makeErrorResponse(drogon::k422UnprocessableEntity, company, validationError, req));
                return;
            }
            callback(buildEmbeddingsResponse(body, company, req));
        },
        {drogon::Post});

    const auto imagesPath = "/" + company + "/images/generations";
    app.registerHandler(
        imagesPath,
        [company, dryRun, requireVendor](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
            HttpResponsePtr error;
            std::string vendor;
            if (!ensureVendorSelected(req, requireVendor, vendor, company, error)) {
                callback(error);
                return;
            }
            if (!dryRun) {
                callback(makeNotImplemented(company, req));
                return;
            }
            Json::Value body;
            std::string parseError;
            if (!parseJsonBody(req, body, parseError)) {
                callback(makeErrorResponse(drogon::k400BadRequest, company, parseError, req));
                return;
            }
            std::string validationError;
            if (!validateImageRequest(body, validationError)) {
                callback(makeErrorResponse(drogon::k422UnprocessableEntity, company, validationError, req));
                return;
            }
            callback(buildImageResponse(body, company, req));
        },
        {drogon::Post});

    const auto transcriptionPath = "/" + company + "/audio/transcriptions";
    app.registerHandler(
        transcriptionPath,
        [company, dryRun, requireVendor](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
            HttpResponsePtr error;
            std::string vendor;
            if (!ensureVendorSelected(req, requireVendor, vendor, company, error)) {
                callback(error);
                return;
            }
            if (!dryRun) {
                callback(makeNotImplemented(company, req));
                return;
            }
            Json::Value body;
            std::string parseError;
            if (!parseJsonBody(req, body, parseError)) {
                callback(makeErrorResponse(drogon::k400BadRequest, company, parseError, req));
                return;
            }
            std::string validationError;
            if (!validateTranscriptionRequest(body, validationError)) {
                callback(makeErrorResponse(drogon::k422UnprocessableEntity, company, validationError, req));
                return;
            }
            callback(buildTranscriptionResponse(body, company, req));
        },
        {drogon::Post});

    const auto speechPath = "/" + company + "/audio/speech";
    app.registerHandler(
        speechPath,
        [company, dryRun, requireVendor](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
            HttpResponsePtr error;
            std::string vendor;
            if (!ensureVendorSelected(req, requireVendor, vendor, company, error)) {
                callback(error);
                return;
            }
            if (!dryRun) {
                callback(makeNotImplemented(company, req));
                return;
            }
            Json::Value body;
            std::string parseError;
            if (!parseJsonBody(req, body, parseError)) {
                callback(makeErrorResponse(drogon::k400BadRequest, company, parseError, req));
                return;
            }
            std::string validationError;
            if (!validateSpeechRequest(body, validationError)) {
                callback(makeErrorResponse(drogon::k422UnprocessableEntity, company, validationError, req));
                return;
            }
            callback(buildSpeechResponse(body, company, req));
        },
        {drogon::Post});

    const auto videoPath = "/" + company + "/video/generations";
    app.registerHandler(
        videoPath,
        [company, dryRun, requireVendor](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
            HttpResponsePtr error;
            std::string vendor;
            if (!ensureVendorSelected(req, requireVendor, vendor, company, error)) {
                callback(error);
                return;
            }
            if (!dryRun) {
                callback(makeNotImplemented(company, req));
                return;
            }
            Json::Value body;
            std::string parseError;
            if (!parseJsonBody(req, body, parseError)) {
                callback(makeErrorResponse(drogon::k400BadRequest, company, parseError, req));
                return;
            }
            std::string validationError;
            if (!validateVideoRequest(body, validationError)) {
                callback(makeErrorResponse(drogon::k422UnprocessableEntity, company, validationError, req));
                return;
            }
            callback(buildVideoResponse(body, company, req));
        },
        {drogon::Post});

    const auto modelsPath = "/" + company + "/models";
    app.registerHandler(
        modelsPath,
        [company, dryRun, requireVendor](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
            HttpResponsePtr error;
            std::string vendor;
            if (!ensureVendorSelected(req, requireVendor, vendor, company, error)) {
                callback(error);
                return;
            }
            if (!dryRun) {
                callback(makeNotImplemented(company, req));
                return;
            }
            callback(buildModelsResponse(company, req));
        },
        {drogon::Get});

    const auto batchesPath = "/" + company + "/batches";
    app.registerHandler(
        batchesPath,
        [company, dryRun, requireVendor](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
            HttpResponsePtr error;
            std::string vendor;
            if (!ensureVendorSelected(req, requireVendor, vendor, company, error)) {
                callback(error);
                return;
            }
            if (!dryRun) {
                callback(makeNotImplemented(company, req));
                return;
            }
            Json::Value body;
            std::string parseError;
            if (!parseJsonBody(req, body, parseError)) {
                callback(makeErrorResponse(drogon::k400BadRequest, company, parseError, req));
                return;
            }
            std::string validationError;
            if (!validateBatchRequest(body, validationError)) {
                callback(makeErrorResponse(drogon::k422UnprocessableEntity, company, validationError, req));
                return;
            }
            callback(buildBatchResponse(body, company, req));
        },
        {drogon::Post});

    const auto jobsPath = "/" + company + "/jobs/{id}";
    app.registerHandler(
        jobsPath,
        [company, dryRun, requireVendor](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
            HttpResponsePtr error;
            std::string vendor;
            if (!ensureVendorSelected(req, requireVendor, vendor, company, error)) {
                callback(error);
                return;
            }
            if (!dryRun) {
                callback(makeNotImplemented(company, req));
                return;
            }
            const auto id = req->getParameter("id");
            if (id.empty()) {
                callback(makeErrorResponse(drogon::k400BadRequest, company, "job id is required", req));
                return;
            }
            callback(buildJobResponse(id, company, req));
        },
        {drogon::Get});
}

}  // namespace superapi::http
