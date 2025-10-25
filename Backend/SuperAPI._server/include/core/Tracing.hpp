#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace YAML {
class Node;
}  // namespace YAML

namespace superapi::core {

class Span;

struct SpanContext {
    std::string traceId;
    std::string spanId;
    std::string traceFlags{"01"};

    [[nodiscard]] bool valid() const { return !traceId.empty() && !spanId.empty(); }
};

using SpanPtr = std::shared_ptr<Span>;
using AttributeValue = std::variant<std::string, std::int64_t, double, bool>;

class Tracer {
  public:
    static Tracer &instance();

    void configure(const YAML::Node &config);

    SpanPtr startSpan(const std::string &name,
                      const SpanContext &parent,
                      const std::unordered_map<std::string, AttributeValue> &attributes,
                      std::string kind = "INTERNAL");

    SpanContext extractTraceparent(std::string_view traceparent) const;
    std::string buildTraceparent(const SpanContext &context) const;

    void setCurrentSpan(const SpanPtr &span);
    SpanPtr currentSpan() const;
    void clearCurrentSpan();
    void releaseSpan(const Span *span);

    void shutdown();

  private:
    friend class Span;

    Tracer();

    void exportSpan(const Span &span);

    SpanContext makeContext() const;

    bool enabled_{false};
    std::string endpoint_;
    std::map<std::string, std::string> headers_;
    std::string serviceName_;
    std::string serviceNamespace_;
    std::string serviceVersion_;
    std::string environment_;
    mutable std::mutex exportMutex_;
};

class Span : public std::enable_shared_from_this<Span> {
  public:
    Span(Tracer &tracer,
         std::string name,
         SpanContext context,
         SpanContext parent,
         std::string kind,
         std::unordered_map<std::string, AttributeValue> attributes);

    ~Span();

    void setAttribute(const std::string &key, const AttributeValue &value);
    void addEvent(const std::string &name,
                  const std::unordered_map<std::string, AttributeValue> &attributes = {});
    void setStatus(int statusCode, std::string message = "");
    void recordException(const std::string &type, const std::string &message);
    void end(int statusCode = 0, const std::string &message = "");

    [[nodiscard]] bool ended() const { return ended_; }
    [[nodiscard]] const SpanContext &context() const { return context_; }
    [[nodiscard]] const SpanContext &parent() const { return parent_; }
    [[nodiscard]] std::string_view name() const { return name_; }

  private:
    friend class Tracer;

    struct Event {
        std::string name;
        std::chrono::system_clock::time_point time;
        std::unordered_map<std::string, AttributeValue> attributes;
    };

    Tracer &tracer_;
    std::string name_;
    SpanContext context_;
    SpanContext parent_;
    std::string kind_;
    std::unordered_map<std::string, AttributeValue> attributes_;
    std::vector<Event> events_;
    std::chrono::system_clock::time_point startTime_;
    std::chrono::system_clock::time_point endTime_;
    int otelStatus_{0};
    int httpStatus_{0};
    std::string statusMessage_;
    bool ended_{false};
    mutable std::mutex mutex_;
};

}  // namespace superapi::core
