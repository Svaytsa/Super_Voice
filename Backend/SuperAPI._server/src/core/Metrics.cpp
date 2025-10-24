#include "core/Metrics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <shared_mutex>
#include <sstream>
#include <utility>

namespace superapi::core {

namespace {
constexpr std::array<double, 10> kDefaultBuckets{0.5, 1.0, 2.5, 5.0, 10.0, 25.0, 50.0, 100.0, 250.0, 500.0};

std::string formatDouble(double value) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(6);
    oss << value;
    return oss.str();
}

}  // namespace

MetricsRegistry &MetricsRegistry::instance() {
    static MetricsRegistry instance;
    return instance;
}

MetricsRegistry::MetricsRegistry() = default;

std::shared_ptr<RequestObservation> MetricsRegistry::startRequest(std::string company,
                                                                  std::string endpoint,
                                                                  std::uint64_t bytesIn,
                                                                  std::uint64_t tokensIn) {
    return std::make_shared<RequestObservation>(*this, std::move(company), std::move(endpoint), bytesIn, tokensIn);
}

void MetricsRegistry::incrementError(const std::string &company,
                                      const std::string &endpoint,
                                      const std::string &errorType) {
    if (errorType.empty()) {
        return;
    }
    const SeriesKey key{company, endpoint};
    std::unique_lock lock(mutex_);
    auto &series = metricsBySeries_[key];
    if (series.latency.buckets.empty()) {
        series.latency = createHistogram();
    }
    ++series.errorCounts[errorType];
}

void MetricsRegistry::recordRequest(const SeriesKey &key,
                                    double latencyMs,
                                    std::uint64_t bytesIn,
                                    std::uint64_t bytesOut,
                                    std::uint64_t tokensIn,
                                    std::uint64_t tokensOut,
                                    std::uint64_t streamEvents,
                                    const std::string &errorType) {
    std::unique_lock lock(mutex_);
    auto &series = metricsBySeries_[key];
    if (series.latency.buckets.empty()) {
        series.latency = createHistogram();
    }

    ++series.requestsTotal;
    series.bytesIn += bytesIn;
    series.bytesOut += bytesOut;
    series.tokensIn += tokensIn;
    series.tokensOut += tokensOut;
    series.streamEvents += streamEvents;

    auto &histogram = series.latency;
    auto &counts = histogram.counts;
    bool bucketed = false;
    for (std::size_t i = 0; i < histogram.buckets.size(); ++i) {
        if (latencyMs <= histogram.buckets[i]) {
            ++counts[i];
            bucketed = true;
            break;
        }
    }
    if (!bucketed) {
        ++counts.back();
    }
    histogram.sum += latencyMs;
    ++histogram.totalCount;

    if (!errorType.empty()) {
        ++series.errorCounts[errorType];
    }
}

MetricsRegistry::Histogram MetricsRegistry::createHistogram() {
    Histogram histogram;
    histogram.buckets.assign(kDefaultBuckets.begin(), kDefaultBuckets.end());
    histogram.counts.assign(histogram.buckets.size() + 1, 0);
    return histogram;
}

std::string MetricsRegistry::escapeLabel(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '\\':
            case '\"':
                escaped.push_back('\\');
                escaped.push_back(ch);
                break;
            case '\n':
                escaped.append("\\n");
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

std::string MetricsRegistry::renderPrometheus() const {
    std::ostringstream oss;
    oss << "# HELP requests_total Total number of HTTP requests handled.\n";
    oss << "# TYPE requests_total counter\n";
    oss << "# HELP errors_total Total number of error responses by type.\n";
    oss << "# TYPE errors_total counter\n";
    oss << "# HELP latency_ms Request latency in milliseconds.\n";
    oss << "# TYPE latency_ms histogram\n";
    oss << "# HELP bytes_in Total bytes received.\n";
    oss << "# TYPE bytes_in counter\n";
    oss << "# HELP bytes_out Total bytes sent.\n";
    oss << "# TYPE bytes_out counter\n";
    oss << "# HELP tokens_in Total tokens received.\n";
    oss << "# TYPE tokens_in counter\n";
    oss << "# HELP tokens_out Total tokens returned.\n";
    oss << "# TYPE tokens_out counter\n";
    oss << "# HELP stream_events_total Total number of streamed events emitted.\n";
    oss << "# TYPE stream_events_total counter\n";

    std::shared_lock lock(mutex_);
    for (const auto &[key, series] : metricsBySeries_) {
        const auto labels = "company=\"" + escapeLabel(key.company) + "\",endpoint=\"" + escapeLabel(key.endpoint) + "\"";
        oss << "requests_total{" << labels << "} " << series.requestsTotal << "\n";
        oss << "bytes_in{" << labels << "} " << series.bytesIn << "\n";
        oss << "bytes_out{" << labels << "} " << series.bytesOut << "\n";
        oss << "tokens_in{" << labels << "} " << series.tokensIn << "\n";
        oss << "tokens_out{" << labels << "} " << series.tokensOut << "\n";
        oss << "stream_events_total{" << labels << "} " << series.streamEvents << "\n";

        for (const auto &[errorType, count] : series.errorCounts) {
            oss << "errors_total{" << labels << ",type=\"" << escapeLabel(errorType) << "\"} " << count << "\n";
        }

        const auto &histogram = series.latency;
        if (!histogram.buckets.empty()) {
            std::uint64_t cumulative = 0;
            for (std::size_t i = 0; i < histogram.buckets.size(); ++i) {
                cumulative += histogram.counts[i];
                oss << "latency_ms_bucket{" << labels << ",le=\"" << formatDouble(histogram.buckets[i]) << "\"} "
                    << cumulative << "\n";
            }
            cumulative += histogram.counts.back();
            oss << "latency_ms_bucket{" << labels << ",le=\"+Inf\"} " << cumulative << "\n";
            oss << "latency_ms_sum{" << labels << "} " << formatDouble(histogram.sum) << "\n";
            oss << "latency_ms_count{" << labels << "} " << histogram.totalCount << "\n";
        }
    }

    return oss.str();
}

RequestObservation::RequestObservation(MetricsRegistry &registry,
                                       std::string company,
                                       std::string endpoint,
                                       std::uint64_t bytesIn,
                                       std::uint64_t tokensIn)
    : registry_(registry),
      company_(std::move(company)),
      endpoint_(std::move(endpoint)),
      bytesIn_(bytesIn),
      tokensIn_(tokensIn),
      tokensOut_(0),
      streamEvents_(0),
      start_(std::chrono::steady_clock::now()) {}

RequestObservation::~RequestObservation() {
    if (!completed_.load(std::memory_order_acquire)) {
        complete(0, 0, tokensOut(), streamEvents(), "abandoned");
    }
}

void RequestObservation::complete(unsigned statusCode,
                                  std::uint64_t bytesOut,
                                  std::uint64_t tokensOut,
                                  std::uint64_t streamEvents,
                                  const std::string &errorType) {
    bool expected = false;
    if (!completed_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    latencyMs_ = std::chrono::duration<double, std::milli>(now - start_).count();
    statusCode_ = statusCode;

    const bool isErrorStatus = statusCode >= 400;
    std::string resolvedErrorType = errorType;
    if (resolvedErrorType.empty() && isErrorStatus) {
        if (statusCode >= 500) {
            resolvedErrorType = "http_5xx";
        } else {
            resolvedErrorType = "http_4xx";
        }
    }

    tokensOut_.store(tokensOut, std::memory_order_release);
    streamEvents_.store(streamEvents, std::memory_order_release);

    MetricsRegistry::SeriesKey key{company_, endpoint_};
    registry_.recordRequest(key,
                            latencyMs_,
                            bytesIn_,
                            bytesOut,
                            tokensIn_.load(std::memory_order_acquire),
                            tokensOut_.load(std::memory_order_acquire),
                            streamEvents_.load(std::memory_order_acquire),
                            resolvedErrorType);
}

double RequestObservation::latencyMs() const {
    return latencyMs_;
}

void RequestObservation::addStreamEvents(std::uint64_t count) {
    streamEvents_.fetch_add(count, std::memory_order_relaxed);
}

void RequestObservation::addTokensOut(std::uint64_t count) {
    tokensOut_.fetch_add(count, std::memory_order_relaxed);
}

void RequestObservation::addTokensIn(std::uint64_t count) {
    tokensIn_.fetch_add(count, std::memory_order_relaxed);
}

}  // namespace superapi::core
