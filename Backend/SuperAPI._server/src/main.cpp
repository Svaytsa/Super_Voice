#include <drogon/drogon.h>
#include <cpr/cpr.h>
#include <json/json.h>
#include <yaml-cpp/yaml.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <signal.h>
#endif

#include "core/Metrics.hpp"
#include "core/Tracing.hpp"
#include "http/HttpServer.hpp"
#include "superapi/app_config.h"
#include "superapi/environment.h"
#include "superapi/logging.h"
#include "superapi/middleware/idempotency.h"
#include "superapi/middleware/request_id.h"
#include "superapi/providers.h"

namespace {
std::atomic<bool> shutdownRequested{false};
#ifndef _WIN32
void installSignalHandlers() {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGTERM);

    pthread_sigmask(SIG_BLOCK, &sigset, nullptr);

    std::thread([sigset]() mutable {
        int signo = 0;
        while (sigwait(&sigset, &signo) == 0) {
            if (!shutdownRequested.exchange(true)) {
                drogon::app().getLoop()->queueInLoop([]() {
                    LOG_INFO << "Shutdown signal received. Stopping server.";
                    drogon::app().quit();
                });
            }
        }
    }).detach();
}
#else
BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT) {
        if (!shutdownRequested.exchange(true)) {
            drogon::app().getLoop()->queueInLoop([]() {
                LOG_INFO << "Shutdown signal received. Stopping server.";
                drogon::app().quit();
            });
        }
        return TRUE;
    }
    return FALSE;
}

void installSignalHandlers() {
    SetConsoleCtrlHandler(consoleHandler, TRUE);
}
#endif

std::string resolveRequestId(const drogon::HttpRequestPtr &req) {
    std::string requestId;
    try {
        requestId = req->attributes()->get<std::string>("request_id");
    } catch (const std::exception &) {
        requestId = req->getHeader("X-Request-ID");
    }
    return requestId;
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

std::uint64_t getAttributeCount(const drogon::AttributesPtr &attributes, const std::string &key) {
    if (!attributes) {
        return 0;
    }
    try {
        return attributes->get<std::uint64_t>(key);
    } catch (const std::exception &) {
    }
    try {
        return static_cast<std::uint64_t>(attributes->get<std::size_t>(key));
    } catch (const std::exception &) {
    }
    try {
        return static_cast<std::uint64_t>(attributes->get<int>(key));
    } catch (const std::exception &) {
    }
    try {
        return static_cast<std::uint64_t>(attributes->get<long long>(key));
    } catch (const std::exception &) {
    }
    return 0;
}

template <typename T>
std::shared_ptr<T> getSharedAttribute(const drogon::AttributesPtr &attributes, const std::string &key) {
    if (!attributes) {
        return nullptr;
    }
    try {
        return attributes->get<std::shared_ptr<T>>(key);
    } catch (const std::exception &) {
        return nullptr;
    }
}

std::string getStringAttribute(const drogon::AttributesPtr &attributes, const std::string &key) {
    if (!attributes) {
        return {};
    }
    try {
        return attributes->get<std::string>(key);
    } catch (const std::exception &) {
        return {};
    }
}

}  // namespace

int main() {
    using namespace drogon;

    superapi::loadDotEnv(".env");

    YAML::Node serverConfig;
    YAML::Node loggingConfig;
    try {
        serverConfig = YAML::LoadFile("config/server.yaml");
    } catch (const std::exception &ex) {
        std::cerr << "Failed to load config/server.yaml: " << ex.what() << std::endl;
    }

    try {
        loggingConfig = YAML::LoadFile("config/logging.yaml");
    } catch (const std::exception &ex) {
        std::cerr << "Failed to load config/logging.yaml: " << ex.what() << std::endl;
    }

    YAML::Node otelConfig;
    try {
        otelConfig = YAML::LoadFile("config/otel.yaml");
    } catch (const std::exception &ex) {
        std::cerr << "Failed to load config/otel.yaml: " << ex.what() << std::endl;
    }

    auto config = superapi::loadAppConfig(serverConfig, loggingConfig);

    superapi::initializeLogging(config.logLevel, loggingConfig);
    superapi::core::Tracer::instance().configure(otelConfig);

    auto &application = drogon::app();
    application.enableServerHeader(false);
    application.enableDateHeader(true);

    if (serverConfig && serverConfig["app"]) {
        if (auto threads = serverConfig["app"]["threads"]; threads) {
            application.setThreadNum(threads.as<size_t>(0));
        }
    }

    superapi::applyAppConfig(config);

    application.registerFilter(std::make_shared<superapi::middleware::IdempotencyMiddleware>());
    application.registerFilter(std::make_shared<superapi::middleware::RequestIdMiddleware>());

    application.registerPostHandlingAdvice([](const HttpRequestPtr &req, const HttpResponsePtr &resp) {
        auto requestId = resolveRequestId(req);
        if (resp && !requestId.empty()) {
            resp->addHeader("X-Request-ID", requestId);
        }

        auto attributes = req->attributes();
        auto metricsContext = getSharedAttribute<superapi::core::RequestObservation>(attributes, "observability.metrics");
        auto span = getSharedAttribute<superapi::core::Span>(attributes, "observability.span");

        const std::string company = getStringAttribute(attributes, "observability.company");
        const std::string endpoint = getStringAttribute(attributes, "observability.endpoint");

        const auto statusCode = resp ? static_cast<unsigned>(resp->getStatusCode()) : 0U;
        const auto bytesOut = resp ? static_cast<std::uint64_t>(resp->body().size()) : 0ULL;

        std::uint64_t tokensOut = getAttributeCount(attributes, "observability.tokens_out");
        if (tokensOut == 0 && resp) {
            tokensOut = parseUnsigned(resp->getHeader("X-Tokens-Out"));
        }
        const std::uint64_t streamEvents = getAttributeCount(attributes, "observability.stream_events");

        if (metricsContext) {
            metricsContext->complete(statusCode, bytesOut, tokensOut, streamEvents);
        }

        if (span) {
            if (!company.empty()) {
                span->setAttribute("company", company);
            }
            if (!endpoint.empty()) {
                span->setAttribute("http.route", endpoint);
            }
            if (metricsContext) {
                span->setAttribute("request.latency_ms", metricsContext->latencyMs());
            }
            span->setAttribute("http.response_content_length", static_cast<std::int64_t>(bytesOut));
            span->end(static_cast<int>(statusCode), statusCode >= 400 ? "http_error" : "");
            if (resp) {
                resp->addHeader("traceparent", superapi::core::Tracer::instance().buildTraceparent(span->context()));
            }
        }

        superapi::LogContext context = superapi::currentLogContext();
        context.requestId = requestId;
        if (!company.empty()) {
            context.company = company;
        }
        if (!endpoint.empty()) {
            context.endpoint = endpoint;
        }
        context.status = static_cast<int>(statusCode);
        context.latencyMs = metricsContext ? metricsContext->latencyMs() : 0.0;
        context.hasRequest = true;
        superapi::updateLogContext(context);
        LOG_INFO << "request complete";

        const auto idempotencyKey = getStringAttribute(attributes, "idempotency.key");
        const auto idempotencyFingerprint = getStringAttribute(attributes, "idempotency.fingerprint");
        if (!idempotencyKey.empty() && !idempotencyFingerprint.empty() && resp) {
            superapi::middleware::IdempotencyStore::instance().put(idempotencyKey, idempotencyFingerprint, resp);
        }

        superapi::clearLogContext();
    });

    superapi::validateProviderConfig("config/providers.yaml");

    const std::string version = "0.1.0";
    const auto startTime = std::chrono::system_clock::now();

    application.registerHandler(
        "/health",
        [version, startTime](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
            Json::Value payload(Json::objectValue);
            payload["status"] = "ok";
            payload["service"] = "superapi_server";
            payload["version"] = version;
            payload["uptime_seconds"] = static_cast<Json::Int64>(
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - startTime).count());

            auto response = HttpResponse::newHttpJsonResponse(payload);
            response->setStatusCode(k200OK);
            auto requestId = resolveRequestId(req);
            if (!requestId.empty()) {
                response->addHeader("X-Request-ID", requestId);
            }
            callback(response);
        },
        {Get});

    application.registerHandler(
        "/version",
        [version](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
            Json::Value payload(Json::objectValue);
            payload["service"] = "superapi_server";
            payload["version"] = version;

            auto response = HttpResponse::newHttpJsonResponse(payload);
            response->setStatusCode(k200OK);
            auto requestId = resolveRequestId(req);
            if (!requestId.empty()) {
                response->addHeader("X-Request-ID", requestId);
            }
            callback(response);
        },
        {Get});

    application.registerHandler(
        "/metrics",
        [](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
            auto body = superapi::core::MetricsRegistry::instance().renderPrometheus();
            auto response = HttpResponse::newHttpResponse();
            response->setStatusCode(k200OK);
            response->setContentTypeString("text/plain; version=0.0.4");
            response->setBody(std::move(body));
            auto requestId = resolveRequestId(req);
            if (!requestId.empty()) {
                response->addHeader("X-Request-ID", requestId);
            }
            callback(response);
        },
        {Get});

    superapi::http::HttpServer::registerRoutes(config);

    installSignalHandlers();

    LOG_INFO << "Starting superapi_server on " << config.host << ':' << config.port;

    application.run();
    LOG_INFO << "Server stopped.";
    superapi::core::Tracer::instance().shutdown();

    return 0;
}
