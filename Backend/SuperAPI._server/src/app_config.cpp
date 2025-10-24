#include "superapi/app_config.h"

#include "superapi/environment.h"

#include <drogon/drogon.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace superapi {
namespace {

std::uint16_t parsePort(const std::string &value, std::uint16_t fallback) {
    try {
        auto portValue = std::stoul(value);
        if (portValue == 0 || portValue > 65535U) {
            return fallback;
        }
        return static_cast<std::uint16_t>(portValue);
    } catch (const std::exception &) {
        return fallback;
    }
}

}  // namespace

AppConfig loadAppConfig(const YAML::Node &serverConfig, const YAML::Node &loggingConfig) {
    AppConfig config{};

    std::string defaultHost{"0.0.0.0"};
    std::uint16_t defaultPort{8080};
    bool defaultDryRun{false};
    std::string defaultLogLevel{"info"};

    if (serverConfig) {
        if (auto listeners = serverConfig["listeners"]; listeners && listeners.IsSequence() && !listeners.IsNull() && listeners.size() > 0) {
            const auto listener = listeners[0];
            if (auto address = listener["address"]; address) {
                defaultHost = address.as<std::string>(defaultHost);
            }
            if (auto port = listener["port"]; port) {
                defaultPort = static_cast<std::uint16_t>(port.as<std::uint32_t>(defaultPort));
            }
        }
        if (auto appNode = serverConfig["app"]; appNode) {
            defaultDryRun = appNode["dry_run"].as<bool>(defaultDryRun);
        }
    }

    if (loggingConfig) {
        if (auto logging = loggingConfig["logging"]; logging) {
            defaultLogLevel = logging["level"].as<std::string>(defaultLogLevel);
        }
    }

    config.host = getEnvOrDefault("HOST", defaultHost);
    config.port = parsePort(getEnvOrDefault("PORT", std::to_string(defaultPort)), defaultPort);
    config.dryRun = getEnvFlag("DRY_RUN", defaultDryRun);
    config.logLevel = getEnvOrDefault("LOG_LEVEL", defaultLogLevel);

    return config;
}

void applyAppConfig(const AppConfig &config) {
    auto &application = drogon::app();
    application.addListener(config.host, config.port);

    if (config.dryRun) {
        LOG_WARN << "DRY_RUN mode is enabled; side effects will be skipped.";
    }
}

}  // namespace superapi
