#pragma once

#include "chunker.hpp"
#include "queue.hpp"

#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <unordered_set>

#include <asio.hpp>

namespace sv::client {

struct SystemChannelOptions
{
    std::string host{"127.0.0.1"};
    std::uint16_t port{7000};
    std::chrono::milliseconds queue_update_period{std::chrono::milliseconds{500}};
};

class SystemChannels
{
public:
    explicit SystemChannels(SystemChannelOptions options = {}) : options_(std::move(options))
    {
        try
        {
            asio::ip::udp::resolver resolver(io_context_);
            auto endpoints = resolver.resolve(options_.host, std::to_string(options_.port));
            if (endpoints.empty())
            {
                std::cerr << "[system-channel] Failed to resolve endpoint" << std::endl;
                return;
            }
            endpoint_ = *endpoints.begin();
            socket_.emplace(io_context_);
            socket_->open(endpoint_.protocol());
        }
        catch (const std::exception& ex)
        {
            std::cerr << "[system-channel] Initialization error: " << ex.what() << std::endl;
        }
    }

    ~SystemChannels()
    {
        stop();
    }

    void start()
    {
        if (queue_thread_.joinable())
        {
            return;
        }
        queue_thread_ = std::jthread([this](std::stop_token stop_token) { queue_size_loop(stop_token); });
    }

    void stop()
    {
        if (queue_thread_.joinable())
        {
            queue_thread_.request_stop();
            queue_thread_.join();
        }
    }

    void set_queue_size_provider(std::function<std::size_t()> provider)
    {
        queue_size_provider_ = std::move(provider);
    }

    void set_queue_capacity_provider(std::function<std::size_t()> provider)
    {
        queue_capacity_provider_ = std::move(provider);
    }

    void notify_file_chunk_enqueued(const FileChunk& chunk, std::size_t queue_size)
    {
        (void)queue_size;
        send_file_meta(chunk);
        send_file_patch_map(chunk);
    }

    void notify_control(std::size_t total_connections, std::size_t active_connections)
    {
        std::ostringstream oss;
        oss << R"({"type":"CONTROL","total_connections":)" << total_connections
            << R"(,"active_connections":)" << active_connections << '}';
        send_message(oss.str());
    }

private:
    static std::string escape_json(std::string_view value)
    {
        std::string escaped;
        escaped.reserve(value.size());
        for (char ch : value)
        {
            switch (ch)
            {
            case '\\':
                escaped += "\\\\";
                break;
            case '\"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += ch;
                break;
            }
        }
        return escaped;
    }

    void send_message(const std::string& message)
    {
        std::cout << "[system-channel] " << message << std::endl;
        std::scoped_lock lock(socket_mutex_);
        if (!socket_)
        {
            return;
        }
        asio::error_code ec;
        socket_->send_to(asio::buffer(message), endpoint_, 0, ec);
        if (ec)
        {
            std::cerr << "[system-channel] send failed: " << ec.message() << std::endl;
        }
    }

    void send_file_meta(const FileChunk& chunk)
    {
        const auto key = chunk.descriptor.path.generic_string() + ':' + chunk.sha256_hex;
        {
            std::scoped_lock lock(meta_mutex_);
            if (!published_meta_.insert(key).second)
            {
                return;
            }
        }

        std::ostringstream oss;
        oss << R"({"type":"FILE_META","path":")" << escape_json(chunk.descriptor.path.generic_string())
            << R"(","sha256":")" << chunk.sha256_hex << R"(","size":)" << chunk.descriptor.size
            << R"(,"chunks":)" << chunk.total_chunks << '}';
        send_message(oss.str());
    }

    void send_file_patch_map(const FileChunk& chunk)
    {
        std::ostringstream oss;
        oss << R"({"type":"FILE_PATCH_MAP","path":")" << escape_json(chunk.descriptor.path.generic_string())
            << R"(","sha256":")" << chunk.sha256_hex << R"(","chunk_index":)" << chunk.index
            << R"(,"total_chunks":)" << chunk.total_chunks << R"(,"payload_size":)" << chunk.payload.size() << '}';
        send_message(oss.str());
    }

    void queue_size_loop(std::stop_token stop_token)
    {
        while (!stop_token.stop_requested())
        {
            if (queue_size_provider_)
            {
                const auto size = queue_size_provider_();
                std::ostringstream oss;
                oss << R"({"type":"QUEUE_SIZE_UPDATE","size":)" << size;
                if (queue_capacity_provider_)
                {
                    oss << R"(,"capacity":)" << queue_capacity_provider_();
                }
                oss << '}';
                send_message(oss.str());
            }
            if (options_.queue_update_period.count() <= 0)
            {
                break;
            }
            std::this_thread::sleep_for(options_.queue_update_period);
        }
    }

    SystemChannelOptions options_{};
    asio::io_context io_context_{};
    std::optional<asio::ip::udp::socket> socket_{};
    asio::ip::udp::endpoint endpoint_{};
    std::mutex socket_mutex_{};

    std::function<std::size_t()> queue_size_provider_{};
    std::function<std::size_t()> queue_capacity_provider_{};
    std::jthread queue_thread_{};
    std::mutex meta_mutex_{};
    std::unordered_set<std::string> published_meta_{};
};

}  // namespace sv::client
