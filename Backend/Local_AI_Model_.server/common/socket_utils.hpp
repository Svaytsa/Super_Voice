#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

namespace sv::common::net {

struct ConnectRetryOptions {
    std::size_t max_attempts{3};
    std::chrono::milliseconds initial_delay{std::chrono::milliseconds{200}};
    std::chrono::milliseconds max_delay{std::chrono::milliseconds{2000}};
    double backoff_multiplier{2.0};
    bool tcp_no_delay{true};
};

struct ConnectOptions {
    std::chrono::milliseconds connect_timeout{std::chrono::milliseconds{5000}};
    ConnectRetryOptions retry{};
};

inline void set_tcp_no_delay(boost::asio::ip::tcp::socket& socket, bool enable) {
    boost::asio::ip::tcp::no_delay option(enable);
    socket.set_option(option);
}

namespace detail {

inline void sleep_with_bounds(std::chrono::milliseconds delay) {
    if (delay.count() > 0) {
        std::this_thread::sleep_for(delay);
    }
}

inline boost::asio::ip::tcp::socket make_socket(boost::asio::io_context& io_context) {
    return boost::asio::ip::tcp::socket(io_context);
}

inline void connect_with_timeout(boost::asio::ip::tcp::socket& socket,
                                 const boost::asio::ip::tcp::resolver::results_type& endpoints,
                                 std::chrono::milliseconds timeout) {
    if (timeout.count() <= 0) {
        boost::asio::connect(socket, endpoints);
        return;
    }

    boost::system::error_code ec;
    bool completed = false;

    boost::asio::steady_timer timer(socket.get_executor());
    timer.expires_after(timeout);
    timer.async_wait([&](const boost::system::error_code& wait_ec) {
        if (!wait_ec && !completed) {
            ec = boost::asio::error::timed_out;
            socket.cancel();
        }
    });

    boost::asio::async_connect(socket, endpoints, [&](const boost::system::error_code& connect_ec, const auto&) {
        completed = true;
        ec = connect_ec;
        timer.cancel();
    });

    auto& context = static_cast<boost::asio::io_context&>(socket.get_executor().context());
    context.restart();
    context.run();

    if (ec) {
        throw boost::system::system_error(ec);
    }
}

inline void apply_tcp_no_delay(boost::asio::ip::tcp::socket& socket, bool enable) {
    if (enable) {
        set_tcp_no_delay(socket, true);
    }
}

}  // namespace detail

inline boost::asio::ip::tcp::socket connect_with_retry(boost::asio::io_context& io_context,
                                                       const std::string& host,
                                                       std::uint16_t port,
                                                       const ConnectOptions& options = {}) {
    if (options.retry.max_attempts == 0) {
        throw std::invalid_argument("retry.max_attempts must be at least 1");
    }
    if (options.retry.backoff_multiplier < 1.0) {
        throw std::invalid_argument("retry.backoff_multiplier must be >= 1.0");
    }

    boost::asio::ip::tcp::resolver resolver(io_context);
    auto port_string = std::to_string(port);
    const auto endpoints = resolver.resolve(host, port_string);

    std::chrono::milliseconds delay = options.retry.initial_delay;
    for (std::size_t attempt = 0; attempt < options.retry.max_attempts; ++attempt) {
        boost::asio::ip::tcp::socket socket = detail::make_socket(io_context);
        try {
            detail::connect_with_timeout(socket, endpoints, options.connect_timeout);
            detail::apply_tcp_no_delay(socket, options.retry.tcp_no_delay);
            return socket;
        } catch (const boost::system::system_error&) {
            if (attempt + 1 >= options.retry.max_attempts) {
                throw;
            }
            detail::sleep_with_bounds(delay);
            auto next_delay = static_cast<double>(delay.count()) * options.retry.backoff_multiplier;
            delay = std::chrono::milliseconds(static_cast<std::int64_t>(std::min<double>(next_delay, options.retry.max_delay.count())));
        }
    }

    throw std::runtime_error("Failed to connect after retries");
}

inline void read_exact(boost::asio::ip::tcp::socket& socket, std::span<std::uint8_t> buffer) {
    boost::asio::read(socket, boost::asio::buffer(buffer.data(), buffer.size()), boost::asio::transfer_exactly(buffer.size()));
}

inline void write_exact(boost::asio::ip::tcp::socket& socket, std::span<const std::uint8_t> buffer) {
    boost::asio::write(socket, boost::asio::buffer(buffer.data(), buffer.size()));
}

inline std::vector<std::uint8_t> read_exact(boost::asio::ip::tcp::socket& socket, std::size_t size) {
    std::vector<std::uint8_t> buffer(size);
    read_exact(socket, std::span<std::uint8_t>(buffer));
    return buffer;
}

inline void write_all(boost::asio::ip::tcp::socket& socket, const std::vector<std::uint8_t>& buffer) {
    write_exact(socket, std::span<const std::uint8_t>(buffer));
}

}  // namespace sv::common::net

