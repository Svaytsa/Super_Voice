#include "core/Tracing.hpp"

#include "superapi/logging.h"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>
#include <trantor/utils/Logger.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace superapi::core {

namespace {
thread_local std::vector<std::weak_ptr<Span>> spanStack;

std::string generateId(std::size_t bytes) {
    std::array<unsigned char, 16> buffer{};
    std::random_device rd;
    std::uniform_int_distribution<int> dist(0, 255);
    for (std::size_t i = 0; i < bytes; ++i) {
        buffer[i] = static_cast<unsigned char>(dist(rd));
    }
    bool allZero = std::all_of(buffer.begin(), buffer.begin() + bytes, [](unsigned char value) { return value == 0; });
    if (allZero) {
        buffer[bytes - 1] = 1;
    }

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < bytes; ++i) {
        oss << std::setw(2) << static_cast<int>(buffer[i]);
    }
    return oss.str();
}

std::string resolveEnv(std::string value) {
    std::size_t pos = 0;
    while ((pos = value.find("${", pos)) != std::string::npos) {
        auto end = value.find('}', pos + 2);
        if (end == std::string::npos) {
            break;
        }
        auto expr = value.substr(pos + 2, end - pos - 2);
        std::string key;
        std::string fallback;
        auto delim = expr.find(":-");
        if (delim != std::string::npos) {
            key = expr.substr(0, delim);
            fallback = expr.substr(delim + 2);
        } else {
            key = expr;
        }
        const char *envValue = std::getenv(key.c_str());
        std::string replacement = (envValue != nullptr && *envValue != '\0') ? std::string(envValue) : fallback;
        value.replace(pos, end - pos + 1, replacement);
        pos += replacement.size();
    }
    return value;
}

std::string trim(std::string_view value) {
    auto begin = value.begin();
    auto end = value.end();
    while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    while (end != begin) {
        auto prev = end;
        --prev;
        if (!std::isspace(static_cast<unsigned char>(*prev))) {
            break;
        }
        end = prev;
    }
    return std::string(begin, end);
}

nlohmann::json attributeToJson(const AttributeValue &value) {
    nlohmann::json result;
    if (std::holds_alternative<std::string>(value)) {
        result["stringValue"] = std::get<std::string>(value);
    } else if (std::holds_alternative<std::int64_t>(value)) {
        result["intValue"] = std::get<std::int64_t>(value);
    } else if (std::holds_alternative<double>(value)) {
        result["doubleValue"] = std::get<double>(value);
    } else if (std::holds_alternative<bool>(value)) {
        result["boolValue"] = std::get<bool>(value);
    }
    return result;
}

std::uint64_t toUnixNanos(const std::chrono::system_clock::time_point &tp) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count());
}

}  // namespace

Tracer &Tracer::instance() {
    static Tracer tracer;
    return tracer;
}

Tracer::Tracer() = default;

void Tracer::configure(const YAML::Node &config) {
    enabled_ = false;
    endpoint_.clear();
    headers_.clear();

    if (!config || !config["otel"]) {
        return;
    }

    const auto otel = config["otel"];

    if (auto service = otel["service"]; service) {
        serviceName_ = service["name"].as<std::string>("superapi");
        serviceNamespace_ = service["namespace"].as<std::string>("");
        serviceVersion_ = service["version"].as<std::string>("");
    }

    if (auto resources = otel["resources"]; resources) {
        environment_ = resources["environment"].as<std::string>("");
    }

    std::string endpoint;
    if (auto exporter = otel["exporter"]; exporter) {
        if (auto otlp = exporter["otlp"]; otlp) {
            if (auto endpointNode = otlp["endpoint"]; endpointNode) {
                endpoint = resolveEnv(endpointNode.as<std::string>(""));
            }
            if (auto headersNode = otlp["headers"]; headersNode) {
                auto headerString = resolveEnv(headersNode.as<std::string>(""));
                if (!headerString.empty()) {
                    std::stringstream stream(headerString);
                    std::string item;
                    while (std::getline(stream, item, ',')) {
                        auto eq = item.find('=');
                        if (eq == std::string::npos) {
                            continue;
                        }
                        auto key = trim(std::string_view(item.data(), eq));
                        auto value = trim(std::string_view(item.data() + eq + 1, item.size() - eq - 1));
                        if (!key.empty() && !value.empty()) {
                            headers_[key] = value;
                        }
                    }
                }
            }
        }
    }

    if (endpoint.empty()) {
        return;
    }

    endpoint = trim(endpoint);
    if (endpoint.ends_with('/')) {
        endpoint.pop_back();
    }
    if (!endpoint.ends_with("/v1/traces")) {
        endpoint += "/v1/traces";
    }

    endpoint_ = endpoint;
    enabled_ = true;
}

SpanPtr Tracer::startSpan(const std::string &name,
                          const SpanContext &parent,
                          const std::unordered_map<std::string, AttributeValue> &attributes,
                          std::string kind) {
    SpanContext resolvedParent = parent;
    if (!resolvedParent.valid()) {
        if (auto current = currentSpan()) {
            resolvedParent = current->context();
        }
    }

    SpanContext context;
    if (resolvedParent.valid()) {
        context.traceId = resolvedParent.traceId;
        context.spanId = generateId(8);
        context.traceFlags = resolvedParent.traceFlags;
    } else {
        context = makeContext();
    }

    auto span = std::make_shared<Span>(*this, name, context, resolvedParent, std::move(kind), attributes);
    setCurrentSpan(span);
    return span;
}

SpanContext Tracer::extractTraceparent(std::string_view traceparent) const {
    SpanContext context;
    if (traceparent.empty()) {
        return context;
    }

    std::array<std::string, 4> parts{};
    std::size_t index = 0;
    std::size_t start = 0;
    while (index < parts.size()) {
        auto pos = traceparent.find('-', start);
        if (pos == std::string_view::npos) {
            parts[index++] = std::string(traceparent.substr(start));
            break;
        }
        parts[index++] = std::string(traceparent.substr(start, pos - start));
        start = pos + 1;
    }
    if (index < 4) {
        return SpanContext{};
    }

    if (parts[0] != "00") {
        return SpanContext{};
    }

    if (parts[1].size() == 32 && parts[2].size() == 16) {
        context.traceId = parts[1];
        context.spanId = parts[2];
        context.traceFlags = parts[3];
    }

    return context;
}

std::string Tracer::buildTraceparent(const SpanContext &context) const {
    if (!context.valid()) {
        return {};
    }
    return "00-" + context.traceId + '-' + context.spanId + '-' + context.traceFlags;
}

void Tracer::setCurrentSpan(const SpanPtr &span) {
    if (!span) {
        return;
    }
    spanStack.emplace_back(span);
}

SpanPtr Tracer::currentSpan() const {
    while (!spanStack.empty()) {
        auto &weak = spanStack.back();
        auto locked = weak.lock();
        if (locked) {
            return locked;
        }
        spanStack.pop_back();
    }
    return nullptr;
}

void Tracer::clearCurrentSpan() {
    if (spanStack.empty()) {
        return;
    }
    spanStack.pop_back();
}

void Tracer::releaseSpan(const Span *span) {
    if (span == nullptr) {
        clearCurrentSpan();
        return;
    }
    for (auto it = spanStack.begin(); it != spanStack.end();) {
        auto locked = it->lock();
        if (!locked || locked.get() == span) {
            it = spanStack.erase(it);
            if (locked && locked.get() == span) {
                break;
            }
            continue;
        }
        ++it;
    }
}

void Tracer::shutdown() {
    enabled_ = false;
    endpoint_.clear();
    headers_.clear();
    spanStack.clear();
}

SpanContext Tracer::makeContext() const {
    SpanContext context;
    context.traceId = generateId(16);
    context.spanId = generateId(8);
    context.traceFlags = "01";
    return context;
}

void Tracer::exportSpan(const Span &span) {
    if (!enabled_) {
        return;
    }

    std::unique_lock<std::mutex> exporterLock(exportMutex_);

    nlohmann::json spanJson;
    {
        std::scoped_lock spanLock(span.mutex_);
        spanJson["traceId"] = span.context_.traceId;
        spanJson["spanId"] = span.context_.spanId;
        if (span.parent_.valid()) {
            spanJson["parentSpanId"] = span.parent_.spanId;
        }
        spanJson["name"] = span.name_;
        spanJson["kind"] = span.kind_;
        spanJson["startTimeUnixNano"] = toUnixNanos(span.startTime_);
        spanJson["endTimeUnixNano"] = toUnixNanos(span.endTime_);

        if (!span.statusMessage_.empty() || span.otelStatus_ != 0) {
            nlohmann::json status;
            status["code"] = span.otelStatus_;
            if (!span.statusMessage_.empty()) {
                status["message"] = span.statusMessage_;
            }
            spanJson["status"] = status;
        }

        if (!span.attributes_.empty()) {
            nlohmann::json attributes = nlohmann::json::array();
            for (const auto &[key, value] : span.attributes_) {
                nlohmann::json attr;
                attr["key"] = key;
                attr["value"] = attributeToJson(value);
                attributes.push_back(attr);
            }
            spanJson["attributes"] = attributes;
        }

        if (!span.events_.empty()) {
            nlohmann::json events = nlohmann::json::array();
            for (const auto &event : span.events_) {
                nlohmann::json eventJson;
                eventJson["name"] = event.name;
                eventJson["timeUnixNano"] = toUnixNanos(event.time);
                if (!event.attributes.empty()) {
                    nlohmann::json eventAttrs = nlohmann::json::array();
                    for (const auto &[key, value] : event.attributes) {
                        nlohmann::json attr;
                        attr["key"] = key;
                        attr["value"] = attributeToJson(value);
                        eventAttrs.push_back(attr);
                    }
                    eventJson["attributes"] = eventAttrs;
                }
                events.push_back(eventJson);
            }
            spanJson["events"] = events;
        }
    }

    auto makeResourceAttribute = [](const std::string &key, const AttributeValue &value) {
        return nlohmann::json{{"key", key}, {"value", attributeToJson(value)}};
    };

    nlohmann::json resourceAttrs = nlohmann::json::array();
    if (!serviceName_.empty()) {
        resourceAttrs.push_back(makeResourceAttribute("service.name", AttributeValue{serviceName_}));
    }
    if (!serviceNamespace_.empty()) {
        resourceAttrs.push_back(makeResourceAttribute("service.namespace", AttributeValue{serviceNamespace_}));
    }
    if (!serviceVersion_.empty()) {
        resourceAttrs.push_back(makeResourceAttribute("service.version", AttributeValue{serviceVersion_}));
    }
    if (!environment_.empty()) {
        resourceAttrs.push_back(makeResourceAttribute("deployment.environment", AttributeValue{environment_}));
    }

    nlohmann::json scopeSpans = nlohmann::json::array();
    scopeSpans.push_back({{"scope", {{"name", "superapi.manual"}}}, {"spans", nlohmann::json::array({spanJson})}});

    nlohmann::json resourceSpan;
    resourceSpan["resource"] = {{"attributes", resourceAttrs}};
    resourceSpan["scopeSpans"] = scopeSpans;

    nlohmann::json payload;
    payload["resourceSpans"] = nlohmann::json::array({resourceSpan});

    cpr::Header headers{{"Content-Type", "application/json"}};
    for (const auto &[key, value] : headers_) {
        headers[key] = value;
    }

    auto body = payload.dump();
    auto response = cpr::Post(cpr::Url{endpoint_}, headers, cpr::Body{body});
    if (response.error || response.status_code >= 400) {
        LOG_WARN << "Failed to export span to OTel collector: " << response.status_code << " - " << response.error.message;
    }
}

Span::Span(Tracer &tracer,
           std::string name,
           SpanContext context,
           SpanContext parent,
           std::string kind,
           std::unordered_map<std::string, AttributeValue> attributes)
    : tracer_(tracer),
      name_(std::move(name)),
      context_(std::move(context)),
      parent_(std::move(parent)),
      kind_(std::move(kind)),
      attributes_(std::move(attributes)),
      startTime_(std::chrono::system_clock::now()) {}

Span::~Span() {
    if (!ended()) {
        end();
    }
}

void Span::setAttribute(const std::string &key, const AttributeValue &value) {
    std::scoped_lock lock(mutex_);
    if (ended_) {
        return;
    }
    attributes_[key] = value;
}

void Span::addEvent(const std::string &name,
                    const std::unordered_map<std::string, AttributeValue> &attributes) {
    std::scoped_lock lock(mutex_);
    if (ended_) {
        return;
    }
    events_.push_back(Event{.name = name, .time = std::chrono::system_clock::now(), .attributes = attributes});
}

void Span::end(int statusCode, const std::string &message) {
    std::unique_lock lock(mutex_);
    if (ended_) {
        return;
    }
    ended_ = true;
    httpStatus_ = statusCode;
    if (statusCode >= 400) {
        otelStatus_ = 2;
    } else if (statusCode > 0) {
        otelStatus_ = 1;
    } else {
        otelStatus_ = 0;
    }
    statusMessage_ = message;
    if (statusCode > 0) {
        attributes_["http.status_code"] = static_cast<std::int64_t>(statusCode);
    }
    endTime_ = std::chrono::system_clock::now();
    lock.unlock();
    tracer_.exportSpan(*this);
    tracer_.releaseSpan(this);
}

}  // namespace superapi::core
