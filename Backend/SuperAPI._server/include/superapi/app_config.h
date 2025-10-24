#pragma once

#include <cstdint>
#include <string>

namespace YAML {
class Node;
}  // namespace YAML

namespace superapi {

struct AppConfig {
    std::string host{"0.0.0.0"};
    std::uint16_t port{8080};
    bool dryRun{false};
    std::string logLevel{"info"};
};

AppConfig loadAppConfig(const YAML::Node &serverConfig, const YAML::Node &loggingConfig);
void applyAppConfig(const AppConfig &config);

}  // namespace superapi
