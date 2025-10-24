#include "superapi/environment.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace superapi {
namespace {

std::string trim(std::string_view input) {
    auto begin = input.begin();
    auto end = input.end();
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

void setEnv(const std::string &key, const std::string &value) {
#ifdef _WIN32
    _putenv((key + "=" + value).c_str());
#else
    setenv(key.c_str(), value.c_str(), 1);
#endif
}

}  // namespace

bool loadDotEnv(const std::filesystem::path &path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        auto delimiterPos = line.find('=');
        if (delimiterPos == std::string::npos) {
            continue;
        }

        auto key = trim(std::string_view(line.data(), delimiterPos));
        auto value = trim(std::string_view(line.data() + delimiterPos + 1, line.size() - delimiterPos - 1));

        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                                  (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }

        if (!key.empty()) {
            setEnv(key, value);
        }
    }

    return true;
}

std::optional<std::string> getEnv(std::string_view key) {
    auto *value = std::getenv(std::string(key).c_str());
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
}

std::string getEnvOrDefault(std::string_view key, std::string_view defaultValue) {
    auto value = getEnv(key);
    if (!value.has_value() || value->empty()) {
        return std::string(defaultValue);
    }
    return *value;
}

bool getEnvFlag(std::string_view key, bool defaultValue) {
    auto value = getEnv(key);
    if (!value.has_value()) {
        return defaultValue;
    }

    std::string lowered = *value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on") {
        return true;
    }
    if (lowered == "false" || lowered == "0" || lowered == "no" || lowered == "off") {
        return false;
    }

    return defaultValue;
}

}  // namespace superapi
