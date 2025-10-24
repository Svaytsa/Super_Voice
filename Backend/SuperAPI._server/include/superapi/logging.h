#pragma once

#include <string>

namespace YAML {
class Node;
}  // namespace YAML

namespace superapi {

void initializeLogging(const std::string &level, const YAML::Node &loggingConfig);

}  // namespace superapi
