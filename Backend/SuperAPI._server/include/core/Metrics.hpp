#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace superapi::core {

class RequestObservation;

class MetricsRegistry {
  public:
    static MetricsRegistry &instance();

    std::shared_ptr<RequestObservation> startRequest(std::string company,
                                                     std::string endpoint,
                                                     std::uint64_t bytesIn,
                                                     std::uint64_t tokensIn = 0);

    void incrementError(const std::string &company,
                        const std::string &endpoint,
                        const std::string &errorType);

    std::string renderPrometheus() const;

  private:
    friend class RequestObservation;

    struct SeriesKey {
        std::string company;
        std::string endpoint;

        bool operator<(const SeriesKey &other) const {
            return std::tie(company, endpoint) < std::tie(other.company, other.endpoint);
        }
    };

    struct Histogram {
        std::vector<double> buckets;
        std::vector<std::uint64_t> counts;
        double sum{0.0};
        std::uint64_t totalCount{0};
    };

    struct SeriesMetrics {
        std::uint64_t requestsTotal{0};
        std::uint64_t bytesIn{0};
        std::uint64_t bytesOut{0};
        std::uint64_t tokensIn{0};
        std::uint64_t tokensOut{0};
        std::uint64_t streamEvents{0};
        std::map<std::string, std::uint64_t> errorCounts;
        Histogram latency;
    };

    MetricsRegistry();

    void recordRequest(const SeriesKey &key,
                       double latencyMs,
                       std::uint64_t bytesIn,
                       std::uint64_t bytesOut,
                       std::uint64_t tokensIn,
                       std::uint64_t tokensOut,
                       std::uint64_t streamEvents,
                       const std::string &errorType);

    static Histogram createHistogram();

    static std::string escapeLabel(std::string_view value);

    mutable std::shared_mutex mutex_;
    std::map<SeriesKey, SeriesMetrics> metricsBySeries_;
};

class RequestObservation : public std::enable_shared_from_this<RequestObservation> {
  public:
    RequestObservation(MetricsRegistry &registry,
                       std::string company,
                       std::string endpoint,
                       std::uint64_t bytesIn,
                       std::uint64_t tokensIn);

    ~RequestObservation();

    void complete(unsigned statusCode,
                  std::uint64_t bytesOut,
                  std::uint64_t tokensOut,
                  std::uint64_t streamEvents,
                  const std::string &errorType = "");

    double latencyMs() const;

    const std::string &company() const { return company_; }
    const std::string &endpoint() const { return endpoint_; }
    unsigned statusCode() const { return statusCode_; }

    void addStreamEvents(std::uint64_t count);
    void addTokensOut(std::uint64_t count);
    void addTokensIn(std::uint64_t count);

    std::uint64_t tokensIn() const { return tokensIn_.load(std::memory_order_relaxed); }
    std::uint64_t tokensOut() const { return tokensOut_.load(std::memory_order_relaxed); }
    std::uint64_t streamEvents() const { return streamEvents_.load(std::memory_order_relaxed); }

  private:
    MetricsRegistry &registry_;
    std::string company_;
    std::string endpoint_;
    std::uint64_t bytesIn_;
    std::atomic<std::uint64_t> tokensIn_;
    std::atomic<std::uint64_t> tokensOut_;
    std::atomic<std::uint64_t> streamEvents_;
    std::chrono::steady_clock::time_point start_;
    std::atomic<bool> completed_{false};
    double latencyMs_{0.0};
    unsigned statusCode_{0};
};

}  // namespace superapi::core
