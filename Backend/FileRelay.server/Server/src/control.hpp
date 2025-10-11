#pragma once

#include "listeners.hpp"
#include "storage.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <asio.hpp>

namespace server
{

class ControlPlane
{
public:
    using MetricsHook = std::function<void()>;

    ControlPlane(ListenerManager& listeners,
                 Storage& storage,
                 std::atomic<std::size_t>& data_listener_count,
                 std::atomic<std::chrono::seconds::rep>& ttl,
                 MetricsHook hook)
        : listeners_{listeners}
        , storage_{storage}
        , data_listener_count_{data_listener_count}
        , ttl_{ttl}
        , metrics_hook_{std::move(hook)}
    {
    }

    void handle_socket(asio::ip::tcp::socket&& socket)
    {
        try
        {
            asio::streambuf buffer;
            std::istream input(&buffer);
            std::ostream output(&buffer);
            bool running = true;
            while (running)
            {
                asio::read_until(socket, buffer, '\n');
                std::string line;
                std::getline(input, line);
                if (line.empty())
                {
                    continue;
                }

                const auto response = apply_command(line);
                const std::string payload = response + "\n";
                asio::write(socket, asio::buffer(payload));

                if (line == "QUIT" || line == "EXIT")
                {
                    running = false;
                }
            }
        }
        catch (const std::exception& ex)
        {
            std::clog << "[control] socket error: " << ex.what() << '\n';
        }
    }

    std::string apply_command(std::string_view command)
    {
        std::istringstream ss(std::string{command});
        std::string verb;
        ss >> verb;
        if (verb == "SCALE_DATA")
        {
            std::size_t value{};
            ss >> value;
            if (value == 0)
            {
                return "ERR data listener count must be > 0";
            }
            data_listener_count_.store(value);
            listeners_.update_data_listener_count(value);
            return "OK data listeners=" + std::to_string(value);
        }
        if (verb == "SET_TTL")
        {
            long long seconds{};
            ss >> seconds;
            if (seconds <= 0)
            {
                return "ERR ttl must be positive";
            }
            std::chrono::seconds new_ttl{seconds};
            ttl_.store(new_ttl.count());
            storage_.update_ttl(new_ttl);
            return "OK ttl=" + std::to_string(seconds);
        }
        if (verb == "PING")
        {
            if (metrics_hook_)
            {
                metrics_hook_();
            }
            return "PONG";
        }
        if (verb == "STATUS")
        {
            return "OK listeners=" + std::to_string(data_listener_count_.load()) +
                   " ttl=" + std::to_string(ttl_.load());
        }
        return "ERR unknown command";
    }

private:
    ListenerManager& listeners_;
    Storage& storage_;
    std::atomic<std::size_t>& data_listener_count_;
    std::atomic<std::chrono::seconds::rep>& ttl_;
    MetricsHook metrics_hook_;
};

} // namespace server

