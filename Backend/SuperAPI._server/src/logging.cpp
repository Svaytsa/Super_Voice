#include "superapi/logging.h"

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>
#include <trantor/utils/Logger.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace superapi {
namespace {

std::mutex logMutex;
std::unique_ptr<std::ofstream> fileSink;
std::vector<std::regex> redactionRules;

thread_local LogContext threadLogContext{};

std::string isoTimestampUtc() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto seconds = time_point_cast<std::chrono::seconds>(now);
    const auto micro = duration_cast<microseconds>(now - seconds).count();
    std::time_t tt = system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%FT%T");
    oss << '.' << std::setw(6) << std::setfill('0') << micro << 'Z';
    return oss.str();
}

trantor::Logger::LogLevel toTrantorLevel(const std::string &level) {
    std::string lowered = level;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (lowered == "trace") {
        return trantor::Logger::kTrace;
    }
    if (lowered == "debug") {
        return trantor::Logger::kDebug;
    }
    if (lowered == "warn" || lowered == "warning") {
        return trantor::Logger::kWarn;
    }
    if (lowered == "error") {
        return trantor::Logger::kError;
    }
    if (lowered == "fatal" || lowered == "critical") {
        return trantor::Logger::kFatal;
    }
    return trantor::Logger::kInfo;
}

std::string extractLevel(std::string_view message) {
    auto start = message.find('[');
    if (start == std::string_view::npos) {
        return "INFO";
    }
    auto end = message.find(']', start);
    if (end == std::string_view::npos || end <= start + 1) {
        return "INFO";
    }
    std::string level(message.substr(start + 1, end - start - 1));
    for (auto &ch : level) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return level;
}

void emitLogPayload(const nlohmann::json &payload) {
    const std::string serialized = payload.dump();
    std::lock_guard<std::mutex> lock(logMutex);
    std::cout << serialized << std::endl;
    if (fileSink && fileSink->is_open()) {
        (*fileSink) << serialized << std::endl;
    }
}

}  // namespace

void initializeLogging(const std::string &level, const YAML::Node &loggingConfig) {
    using trantor::Logger;

    Logger::setLogLevel(toTrantorLevel(level));

    bool enableStdout = true;
    bool enableFile = false;
    std::filesystem::path logPath;

    redactionRules.clear();
    redactionRules.emplace_back(R"(([A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}))", std::regex::icase);
    redactionRules.emplace_back(R"((?:(?:\+?\d{1,3})?[-.\s]?)?(?:\d{3}[-.\s]?){2}\d{4})");
    redactionRules.emplace_back("(?:\\b\\d{4}[- ]?){3}\\d{4}\\b");
    redactionRules.emplace_back(R"((?:(?:api[_-]?key|token|secret|authorization))[:=][^\s]+)", std::regex::icase);

    if (loggingConfig) {
        if (auto logging = loggingConfig["logging"]; logging) {
            if (auto stdoutNode = logging["stdout"]; stdoutNode) {
                enableStdout = stdoutNode.as<bool>(enableStdout);
            }
            if (auto fileNode = logging["file"]; fileNode) {
                enableFile = fileNode["enabled"].as<bool>(enableFile);
                if (enableFile && fileNode["path"]) {
                    logPath = fileNode["path"].as<std::string>();
                    if (!logPath.empty()) {
                        auto parent = logPath.parent_path();
                        if (!parent.empty()) {
                            std::error_code ec;
                            std::filesystem::create_directories(parent, ec);
                        }
                        fileSink = std::make_unique<std::ofstream>(logPath, std::ios::app);
                    }
                }
            }
            if (auto redactNode = logging["redact"]; redactNode) {
                if (auto patterns = redactNode["patterns"]; patterns && patterns.IsSequence()) {
                    for (const auto &pattern : patterns) {
                        try {
                            redactionRules.emplace_back(pattern.as<std::string>());
                        } catch (const std::regex_error &) {
                            LOG_WARN << "Invalid redaction regex ignored: " << pattern.as<std::string>("");
                        }
                    }
                }
            }
        }
    }

    if (!enableStdout) {
        // Redirect std::cout to /dev/null by disabling sync
        std::cout.setstate(std::ios::failbit);
    }

    Logger::setOutputFunction(
        [](const char *msg, const uint64_t len) {
            std::string_view view(msg, len);
            if (!view.empty() && view.back() == '\n') {
                view.remove_suffix(1);
            }

            auto sanitized = redactMessage(view);
            auto level = extractLevel(view);
            auto context = currentLogContext();

            nlohmann::json payload;
            payload["ts"] = isoTimestampUtc();
            payload["level"] = level;
            payload["msg"] = sanitized;
            if (context.requestId.empty()) {
                payload["request_id"] = nullptr;
            } else {
                payload["request_id"] = context.requestId;
            }
            if (context.company.empty()) {
                payload["company"] = nullptr;
            } else {
                payload["company"] = context.company;
            }
            if (context.endpoint.empty()) {
                payload["endpoint"] = nullptr;
            } else {
                payload["endpoint"] = context.endpoint;
            }
            if (context.status != 0) {
                payload["status"] = context.status;
            } else {
                payload["status"] = nullptr;
            }
            if (context.latencyMs > 0.0) {
                payload["latency_ms"] = context.latencyMs;
            } else {
                payload["latency_ms"] = nullptr;
            }

            emitLogPayload(payload);
        },
        []() {
            std::lock_guard<std::mutex> lock(logMutex);
            std::cout << std::flush;
            if (fileSink && fileSink->is_open()) {
                fileSink->flush();
            }
        });

    LOG_INFO << "Logging initialized";
}

void setLogContext(const LogContext &context) {
    threadLogContext = context;
    threadLogContext.hasRequest = true;
}

void updateLogContext(const LogContext &context) {
    if (!context.requestId.empty()) {
        threadLogContext.requestId = context.requestId;
    }
    if (!context.company.empty()) {
        threadLogContext.company = context.company;
    }
    if (!context.endpoint.empty()) {
        threadLogContext.endpoint = context.endpoint;
    }
    if (context.status != 0) {
        threadLogContext.status = context.status;
    }
    if (context.latencyMs > 0.0) {
        threadLogContext.latencyMs = context.latencyMs;
    }
    threadLogContext.hasRequest = threadLogContext.hasRequest || context.hasRequest || !threadLogContext.requestId.empty();
}

LogContext currentLogContext() {
    return threadLogContext;
}

void clearLogContext() {
    threadLogContext = LogContext{};
}

std::string redactMessage(std::string_view message) {
    std::string sanitized(message);
    for (const auto &rule : redactionRules) {
        sanitized = std::regex_replace(sanitized, rule, "[REDACTED]");
    }
    return sanitized;
}

}  // namespace superapi
