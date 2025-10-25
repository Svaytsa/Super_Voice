#pragma once

#include <string>
#include <string_view>

namespace YAML {
class Node;
}  // namespace YAML

namespace superapi {

struct LogContext {
    std::string requestId;
    std::string company;
    std::string endpoint;
    int status{0};
    double latencyMs{0.0};
    bool hasRequest{false};
};

class ScopedLogContext {
  public:
    explicit ScopedLogContext(LogContext context);
    ScopedLogContext(const ScopedLogContext &) = delete;
    ScopedLogContext &operator=(const ScopedLogContext &) = delete;
    ScopedLogContext(ScopedLogContext &&other) noexcept;
    ScopedLogContext &operator=(ScopedLogContext &&other) noexcept;
    ~ScopedLogContext();

  private:
    bool active_{false};
    LogContext previous_{};
};

void initializeLogging(const std::string &level, const YAML::Node &loggingConfig);

void setLogContext(const LogContext &context);
void updateLogContext(const LogContext &context);
LogContext currentLogContext();
void clearLogContext();

std::string redactMessage(std::string_view message);

}  // namespace superapi
