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

#include "superapi/app_config.h"
#include "superapi/environment.h"
#include "superapi/logging.h"
#include "superapi/middleware/request_id.h"
#include "superapi/providers.h"

namespace {
std::atomic<bool> shutdownRequested{false};
std::atomic<std::uint64_t> requestCount{0};

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

    auto config = superapi::loadAppConfig(serverConfig, loggingConfig);

    superapi::initializeLogging(config.logLevel, loggingConfig);

    auto &application = drogon::app();
    application.enableServerHeader(false);
    application.enableDateHeader(true);

    if (serverConfig && serverConfig["app"]) {
        if (auto threads = serverConfig["app"]["threads"]; threads) {
            application.setThreadNum(threads.as<size_t>(0));
        }
    }

    superapi::applyAppConfig(config);

    application.registerFilter(std::make_shared<superapi::middleware::RequestIdMiddleware>());

    application.registerPreRoutingAdvice([](const HttpRequestPtr &) {
        requestCount.fetch_add(1U, std::memory_order_relaxed);
    });

    application.registerPostHandlingAdvice([](const HttpRequestPtr &req, const HttpResponsePtr &resp) {
        if (!resp) {
            return;
        }
        auto requestId = resolveRequestId(req);
        if (!requestId.empty()) {
            resp->addHeader("X-Request-ID", requestId);
        }
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
            const auto currentCount = requestCount.load(std::memory_order_relaxed);
            std::ostringstream stream;
            stream << "# HELP superapi_requests_total Total number of HTTP requests handled.\n";
            stream << "# TYPE superapi_requests_total counter\n";
            stream << "superapi_requests_total " << currentCount << "\n";
            stream << "# HELP superapi_build_info Build information.\n";
            stream << "# TYPE superapi_build_info gauge\n";
            stream << "superapi_build_info{version=\"0.1.0\"} 1\n";

            auto response = HttpResponse::newHttpResponse();
            response->setStatusCode(k200OK);
            response->setContentTypeString("text/plain; version=0.0.4");
            response->setBody(stream.str());
            auto requestId = resolveRequestId(req);
            if (!requestId.empty()) {
                response->addHeader("X-Request-ID", requestId);
            }
            callback(response);
        },
        {Get});

    installSignalHandlers();

    LOG_INFO << "Starting superapi_server on " << config.host << ':' << config.port;

    application.run();
    LOG_INFO << "Server stopped.";

    return 0;
}
