#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace sv::common::config {

struct CommonOptions {
    std::string server_host{"127.0.0.1"};
    std::uint16_t server_port{5000};
    std::chrono::milliseconds connect_timeout{std::chrono::milliseconds{5000}};
    std::chrono::milliseconds io_timeout{std::chrono::milliseconds{15000}};
    std::size_t max_retries{5};
    std::chrono::milliseconds retry_initial_delay{std::chrono::milliseconds{250}};
    std::chrono::milliseconds retry_max_delay{std::chrono::milliseconds{5000}};
    double retry_backoff_factor{2.0};
    bool tcp_no_delay{true};
    std::optional<std::string> auth_token{};

    [[nodiscard]] std::string summary() const {
        std::string result;
        result.reserve(128);
        result.append("host=").append(server_host);
        result.append(", port=").append(std::to_string(server_port));
        result.append(", connect_timeout_ms=").append(std::to_string(connect_timeout.count()));
        result.append(", io_timeout_ms=").append(std::to_string(io_timeout.count()));
        result.append(", max_retries=").append(std::to_string(max_retries));
        result.append(", tcp_no_delay=").append(tcp_no_delay ? "true" : "false");
        if (auth_token) {
            result.append(", auth_token=***");
        }
        return result;
    }
};

namespace detail {

inline std::optional<std::string> getenv(std::string_view key) {
    if (key.empty()) {
        return std::nullopt;
    }
    std::string name(key);
    if (const char* value = std::getenv(name.c_str()); value != nullptr) {
        return std::string(value);
    }
    return std::nullopt;
}

inline bool to_bool(std::string_view value) {
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        return false;
    }
    throw std::invalid_argument("Invalid boolean value: " + std::string(value));
}

inline std::unordered_map<std::string, std::string> parse_cli(int argc, const char* const argv[]) {
    std::unordered_map<std::string, std::string> result;
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg.rfind("--", 0) != 0) {
            continue;
        }
        arg.erase(0, 2);
        auto eq_pos = arg.find('=');
        if (eq_pos != std::string::npos) {
            result[arg.substr(0, eq_pos)] = arg.substr(eq_pos + 1);
            continue;
        }
        if (i + 1 < argc) {
            result[arg] = argv[++i];
        } else {
            result[arg] = "true";
        }
    }
    return result;
}

inline std::uint16_t parse_port(std::string_view value) {
    auto parsed = std::stoul(std::string(value));
    if (parsed == 0 || parsed > 65535) {
        throw std::out_of_range("Port out of range");
    }
    return static_cast<std::uint16_t>(parsed);
}

inline std::chrono::milliseconds parse_duration_ms(std::string_view value) {
    auto parsed = std::stoll(std::string(value));
    if (parsed < 0) {
        throw std::out_of_range("Duration must be non-negative");
    }
    return std::chrono::milliseconds(parsed);
}

inline double parse_double(std::string_view value) {
    return std::stod(std::string(value));
}

inline std::size_t parse_size(std::string_view value) {
    auto parsed = std::stoll(std::string(value));
    if (parsed < 0) {
        throw std::out_of_range("Size must be non-negative");
    }
    return static_cast<std::size_t>(parsed);
}

inline void apply_env(CommonOptions& options) {
    if (auto host = getenv("SV_HOST")) {
        options.server_host = *host;
    }
    if (auto port = getenv("SV_PORT")) {
        options.server_port = parse_port(*port);
    }
    if (auto connect_timeout = getenv("SV_CONNECT_TIMEOUT_MS")) {
        options.connect_timeout = parse_duration_ms(*connect_timeout);
    }
    if (auto io_timeout = getenv("SV_IO_TIMEOUT_MS")) {
        options.io_timeout = parse_duration_ms(*io_timeout);
    }
    if (auto retries = getenv("SV_MAX_RETRIES")) {
        options.max_retries = parse_size(*retries);
    }
    if (auto initial_delay = getenv("SV_RETRY_INITIAL_MS")) {
        options.retry_initial_delay = parse_duration_ms(*initial_delay);
    }
    if (auto max_delay = getenv("SV_RETRY_MAX_MS")) {
        options.retry_max_delay = parse_duration_ms(*max_delay);
    }
    if (auto factor = getenv("SV_RETRY_FACTOR")) {
        options.retry_backoff_factor = parse_double(*factor);
    }
    if (auto no_delay = getenv("SV_TCP_NO_DELAY")) {
        options.tcp_no_delay = to_bool(*no_delay);
    }
    if (auto token = getenv("SV_AUTH_TOKEN")) {
        if (!token->empty()) {
            options.auth_token = *token;
        }
    }
}

inline void apply_cli(CommonOptions& options, const std::unordered_map<std::string, std::string>& args) {
    if (auto it = args.find("host"); it != args.end()) {
        options.server_host = it->second;
    }
    if (auto it = args.find("port"); it != args.end()) {
        options.server_port = parse_port(it->second);
    }
    if (auto it = args.find("connect-timeout"); it != args.end()) {
        options.connect_timeout = parse_duration_ms(it->second);
    }
    if (auto it = args.find("io-timeout"); it != args.end()) {
        options.io_timeout = parse_duration_ms(it->second);
    }
    if (auto it = args.find("max-retries"); it != args.end()) {
        options.max_retries = parse_size(it->second);
    }
    if (auto it = args.find("retry-initial"); it != args.end()) {
        options.retry_initial_delay = parse_duration_ms(it->second);
    }
    if (auto it = args.find("retry-max"); it != args.end()) {
        options.retry_max_delay = parse_duration_ms(it->second);
    }
    if (auto it = args.find("retry-factor"); it != args.end()) {
        options.retry_backoff_factor = parse_double(it->second);
    }
    if (auto it = args.find("tcp-no-delay"); it != args.end()) {
        options.tcp_no_delay = to_bool(it->second);
    }
    if (auto it = args.find("auth-token"); it != args.end()) {
        if (!it->second.empty()) {
            options.auth_token = it->second;
        } else {
            options.auth_token.reset();
        }
    }
}

inline void validate(const CommonOptions& options) {
    if (options.retry_backoff_factor < 1.0) {
        throw std::invalid_argument("retry_backoff_factor must be >= 1.0");
    }
    if (options.retry_initial_delay.count() < 0 || options.retry_max_delay.count() < 0) {
        throw std::invalid_argument("retry delays must be >= 0");
    }
    if (options.retry_initial_delay > options.retry_max_delay) {
        throw std::invalid_argument("retry_initial_delay cannot exceed retry_max_delay");
    }
}

}  // namespace detail

inline CommonOptions load_common_options(int argc, const char* const argv[]) {
    CommonOptions options;
    detail::apply_env(options);
    auto args = detail::parse_cli(argc, argv);
    detail::apply_cli(options, args);
    detail::validate(options);
    return options;
}

}  // namespace sv::common::config

