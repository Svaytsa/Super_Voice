#include "superapi/providers.h"

#include <drogon/drogon.h>
#include <yaml-cpp/yaml.h>

#include <string>
#include <vector>

namespace superapi {

void validateProviderConfig(const std::filesystem::path &path) {
    try {
        const auto config = YAML::LoadFile(path.string());
        if (!config || !config["providers"]) {
            LOG_WARN << "Provider configuration is empty or missing; external integrations are disabled.";
            return;
        }

        const auto providers = config["providers"];
        if (!providers.IsMap()) {
            LOG_WARN << "Provider configuration is malformed; expected a map of providers.";
            return;
        }

        for (const auto &entry : providers) {
            const auto name = entry.first.as<std::string>("unknown");
            const auto provider = entry.second;
            if (!provider.IsMap()) {
                LOG_WARN << "Provider " << name << " must be an object in providers.yaml.";
                continue;
            }

            std::vector<std::string> missing;
            for (const auto &field : provider) {
                const auto key = field.first.as<std::string>();
                const auto value = field.second;
                if (value.IsScalar()) {
                    auto text = value.as<std::string>("");
                    if (text.empty()) {
                        missing.push_back(key);
                    }
                } else if (value.IsSequence()) {
                    if (value.size() == 0) {
                        missing.push_back(key);
                    }
                }
            }

            if (!missing.empty()) {
                std::string joined;
                for (std::size_t i = 0; i < missing.size(); ++i) {
                    joined.append(missing[i]);
                    if (i + 1 < missing.size()) {
                        joined.append(", ");
                    }
                }
                LOG_WARN << "Provider " << name << " missing configuration values: " << joined;
            }
        }
    } catch (const std::exception &ex) {
        LOG_WARN << "Unable to load provider configuration from " << path << ": " << ex.what();
    }
}

}  // namespace superapi
