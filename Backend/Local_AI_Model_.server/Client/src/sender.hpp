#pragma once

#include "chunker.hpp"
#include "queue.hpp"
#include "system_channels.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <asio.hpp>

namespace sv::client {

struct SenderOptions
{
    std::string host_prefix{"data-base"};
    std::uint16_t base_port{9'000};
    std::size_t connections{2};
    std::size_t max_send_retries{3};
    std::size_t max_connect_attempts{3};
    std::chrono::milliseconds connect_timeout{std::chrono::milliseconds{5000}};
    std::chrono::milliseconds reconnect_delay{std::chrono::milliseconds{200}};
    bool tcp_no_delay{true};
};

class Sender
{
public:
    Sender(SenderOptions options, BoundedBlockingQueue<FileChunk>& queue, SystemChannels& channels)
        : options_(std::move(options)), queue_(queue), channels_(channels)
    {
        if (options_.connections == 0)
        {
            options_.connections = 1;
        }
        connections_.reserve(options_.connections);
        for (std::size_t index = 0; index < options_.connections; ++index)
        {
            auto connection = std::make_unique<Connection>();
            connection->index = index;
            connection->host = options_.host_prefix + std::to_string(index);
            connection->port = static_cast<std::uint16_t>(options_.base_port + index);
            connection->max_send_retries = options_.max_send_retries;
            connection->max_connect_attempts = options_.max_connect_attempts;
            connection->connect_timeout = options_.connect_timeout;
            connection->reconnect_delay = options_.reconnect_delay;
            connection->tcp_no_delay = options_.tcp_no_delay;
            connections_.push_back(std::move(connection));
        }
    }

    ~Sender()
    {
        stop();
    }

    void start()
    {
        if (worker_.joinable())
        {
            return;
        }
        worker_ = std::jthread([this](std::stop_token stop_token) { run(stop_token); });
    }

    void stop()
    {
        queue_.close();
        if (worker_.joinable())
        {
            worker_.request_stop();
            worker_.join();
        }
    }

private:
    struct Connection
    {
        std::size_t index{0};
        std::string host;
        std::uint16_t port{0};
        std::size_t max_send_retries{3};
        std::size_t max_connect_attempts{3};
        std::chrono::milliseconds connect_timeout{std::chrono::milliseconds{5000}};
        std::chrono::milliseconds reconnect_delay{std::chrono::milliseconds{200}};
        bool tcp_no_delay{true};
        asio::io_context io_context{};

        void close()
        {
            if (socket_ && socket_->is_open())
            {
                asio::error_code ec;
                socket_->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
                socket_->close(ec);
            }
            socket_.reset();
        }

        asio::ip::tcp::socket& ensure_connected()
        {
            if (socket_ && socket_->is_open())
            {
                return *socket_;
            }
            socket_.reset();

            asio::ip::tcp::resolver resolver(io_context);
            auto endpoints = resolver.resolve(host, std::to_string(port));
            asio::error_code last_error = asio::error::host_not_found;

            for (std::size_t attempt = 0; attempt < std::max<std::size_t>(1, max_connect_attempts); ++attempt)
            {
                socket_.emplace(io_context);
                asio::error_code connect_error{};
                bool timed_out = false;

                asio::steady_timer timer(io_context);
                asio::async_connect(*socket_, endpoints, [&](const asio::error_code& ec, const auto&) {
                    connect_error = ec;
                    timer.cancel();
                });

                if (connect_timeout.count() > 0)
                {
                    timer.expires_after(connect_timeout);
                    timer.async_wait([&](const asio::error_code& ec) {
                        if (!ec)
                        {
                            timed_out = true;
                            connect_error = asio::error::timed_out;
                            socket_->close();
                        }
                    });
                }

                io_context.restart();
                io_context.run();
                last_error = connect_error;

                if (!connect_error && !timed_out)
                {
                    if (tcp_no_delay)
                    {
                        socket_->set_option(asio::ip::tcp::no_delay{true});
                    }
                    return *socket_;
                }

                close();
                std::this_thread::sleep_for(reconnect_delay * (attempt + 1));
            }

            throw std::system_error(last_error ? last_error : asio::error::operation_aborted,
                                    "Failed to connect to " + host + ":" + std::to_string(port));
        }

        static std::vector<std::uint8_t> serialize(const FileChunk& chunk)
        {
            std::ostringstream oss;
            oss << "FILE " << chunk.descriptor.path.generic_string() << '\n';
            oss << "SHA256 " << chunk.sha256_hex << '\n';
            oss << "ORIGINAL_SIZE " << chunk.descriptor.size << '\n';
            oss << "CHUNK " << chunk.index << '/' << chunk.total_chunks << '\n';
            oss << "PAYLOAD_SIZE " << chunk.payload.size() << "\n\n";

            const auto header = oss.str();
            std::vector<std::uint8_t> buffer(header.begin(), header.end());
            buffer.insert(buffer.end(), chunk.payload.begin(), chunk.payload.end());
            return buffer;
        }

        void send_chunk(const FileChunk& chunk)
        {
            const auto payload = serialize(chunk);
            std::size_t attempt = 0;
            while (attempt < max_send_retries)
            {
                try
                {
                    auto& socket = ensure_connected();
                    asio::write(socket, asio::buffer(payload));
                    return;
                }
                catch (const std::exception&)
                {
                    close();
                    ++attempt;
                    if (attempt >= max_send_retries)
                    {
                        throw;
                    }
                }
            }
        }

        bool is_open() const
        {
            return socket_ && socket_->is_open();
        }

    private:
        std::optional<asio::ip::tcp::socket> socket_{};
    };

    Connection& next_connection()
    {
        std::scoped_lock lock(connection_mutex_);
        if (connections_.empty())
        {
            throw std::runtime_error("No connections available");
        }
        auto& connection = *connections_[next_connection_index_];
        next_connection_index_ = (next_connection_index_ + 1) % connections_.size();
        return connection;
    }

    std::size_t active_connections()
    {
        std::scoped_lock lock(connection_mutex_);
        std::size_t count = 0;
        for (const auto& connection : connections_)
        {
            if (connection->is_open())
            {
                ++count;
            }
        }
        return count;
    }

    void run(std::stop_token stop_token)
    {
        channels_.notify_control(options_.connections, active_connections());
        while (!stop_token.stop_requested())
        {
            auto chunk = queue_.pop();
            if (!chunk)
            {
                if (queue_.closed())
                {
                    break;
                }
                continue;
            }

            bool sent = false;
            try
            {
                Connection& connection = next_connection();
                connection.send_chunk(*chunk);
                sent = true;
            }
            catch (const std::exception& ex)
            {
                std::cerr << "[sender] failed to send chunk: " << ex.what() << std::endl;
            }

            if (sent)
            {
                channels_.notify_control(options_.connections, active_connections());
                std::cout << "[sender] chunk sent: " << chunk->descriptor.path << " (#" << chunk->index << "/"
                          << chunk->total_chunks << ")" << std::endl;
            }
            else
            {
                std::cerr << "[sender] dropping chunk for " << chunk->descriptor.path << " after retries" << std::endl;
            }
        }
    }

    SenderOptions options_;
    BoundedBlockingQueue<FileChunk>& queue_;
    SystemChannels& channels_;
    std::vector<std::unique_ptr<Connection>> connections_;
    std::mutex connection_mutex_;
    std::size_t next_connection_index_{0};
    std::jthread worker_{};
};

}  // namespace sv::client
