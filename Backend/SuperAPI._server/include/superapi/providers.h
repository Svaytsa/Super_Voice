#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace drogon {
class HttpAppFramework;
class HttpRequest;
using HttpRequestPtr = std::shared_ptr<HttpRequest>;
}  // namespace drogon

namespace superapi::providers {
class IProvider;
}

namespace superapi {

void validateProviderConfig(const std::filesystem::path &path);

namespace providers {

void initialize(const std::filesystem::path &path, bool dryRun);
void registerRoutes(drogon::HttpAppFramework &app);
std::shared_ptr<IProvider> getProvider(const std::string &key);

}  // namespace providers

}  // namespace superapi
