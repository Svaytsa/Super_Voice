#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace superapi {

bool loadDotEnv(const std::filesystem::path &path);
std::optional<std::string> getEnv(std::string_view key);
std::string getEnvOrDefault(std::string_view key, std::string_view defaultValue);
bool getEnvFlag(std::string_view key, bool defaultValue);

}  // namespace superapi
