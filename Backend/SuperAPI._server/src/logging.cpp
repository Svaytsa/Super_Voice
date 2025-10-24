#include "superapi/logging.h"

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>
#include <trantor/utils/Logger.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <cctype>

namespace superapi {
namespace {

std::mutex logMutex;
std::unique_ptr<std::ofstream> fileSink;

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

            nlohmann::json payload = {
                {"timestamp", isoTimestampUtc()},
                {"level", extractLevel(view)},
                {"message", std::string(view)}
            };

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

}  // namespace superapi
