#pragma once

#include "chunker.hpp"
#include "queue.hpp"
#include "system_channels.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
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

        wait_for_inflight_zero();

        {
            std::scoped_lock metrics_lock(metrics_mutex_);
            maybe_report_metrics_locked(std::chrono::steady_clock::now(), true);
            reset_metrics_window_locked(std::chrono::steady_clock::now());
        }

        for (auto& connection : connections_)
        {
            if (connection)
            {
                connection->stop();
            }
        }
    }

private:
    struct PendingChunk
    {
        std::shared_ptr<FileChunk> chunk;
        std::size_t attempt{1};
    };

    struct MetricsWindow
    {
        std::chrono::steady_clock::time_point start{};
        std::size_t chunks{0};
        std::size_t bytes{0};
        std::size_t retries{0};
    };

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
        asio::strand<asio::io_context::executor_type> strand{asio::make_strand(io_context)};
        std::optional<asio::executor_work_guard<asio::io_context::executor_type>> work_guard{};
        std::jthread runner_{};
        std::atomic<bool> runner_cleanup_pending_{false};

        void close()
        {
            if (socket_ && socket_->is_open())
            {
                asio::error_code ec;
                socket_->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
                socket_->close(ec);
            }
            socket_.reset();
            stop_runner(false);
        }

        void stop()
        {
            close();
            stop_runner(true);
        }

        template <typename SuccessHandler, typename FailureHandler>
        void async_send_chunk(const std::shared_ptr<FileChunk>& chunk,
                              std::size_t attempt,
                              SuccessHandler&& on_success,
                              FailureHandler&& on_failure)
        {
            auto payload = std::make_shared<std::vector<std::uint8_t>>(serialize(*chunk));

            auto send_op = [this,
                            chunk,
                            payload,
                            attempt,
                            success = std::forward<SuccessHandler>(on_success),
                            failure = std::forward<FailureHandler>(on_failure)]() mutable {
                if (!socket_ || !socket_->is_open())
                {
                    failure(*chunk, attempt, "socket closed");
                    return;
                }

                asio::async_write(*socket_, asio::buffer(*payload),
                                  asio::bind_executor(
                                      strand,
                                      [this, chunk, payload, attempt, success = std::move(success),
                                       failure = std::move(failure)](const asio::error_code& ec, std::size_t) mutable {
                                          if (!ec)
                                          {
                                              payload->clear();
                                              payload->shrink_to_fit();
                                              success(*chunk, attempt);
                                          }
                                          else
                                          {
                                              const std::string message = ec.message();
                                              close();
                                              payload->clear();
                                              payload->shrink_to_fit();
                                              failure(*chunk, attempt, message);
                                          }
                                      }));
            };

            asio::dispatch(strand, std::move(send_op));
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
                    ensure_runner();
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

        bool is_open() const
        {
            return socket_ && socket_->is_open();
        }

    private:
        void ensure_runner()
        {
            if (runner_cleanup_pending_ && runner_.joinable() && std::this_thread::get_id() != runner_.get_id())
            {
                runner_.join();
                runner_ = std::jthread{};
                runner_cleanup_pending_ = false;
                io_context.restart();
            }

            if (!work_guard_)
            {
                work_guard_.emplace(asio::make_work_guard(io_context.get_executor()));
            }

            if (!runner_.joinable())
            {
                io_context.restart();
                runner_ = std::jthread([this](std::stop_token stop_token) {
                    while (!stop_token.stop_requested())
                    {
                        io_context.run();
                        if (stop_token.stop_requested())
                        {
                            break;
                        }
                        if (work_guard_)
                        {
                            io_context.restart();
                        }
                        else
                        {
                            break;
                        }
                    }
                });
            }
        }

        void stop_runner(bool wait)
        {
            if (work_guard_)
            {
                work_guard_->reset();
                work_guard_.reset();
            }

            io_context.stop();

            if (runner_.joinable())
            {
                runner_.request_stop();
                if (wait && std::this_thread::get_id() != runner_.get_id())
                {
                    runner_.join();
                    runner_ = std::jthread{};
                    runner_cleanup_pending_ = false;
                }
                else if (!wait)
                {
                    runner_cleanup_pending_ = true;
                }
            }

            if (wait || !runner_.joinable())
            {
                io_context.restart();
                if (!runner_.joinable())
                {
                    runner_cleanup_pending_ = false;
                }
            }
        }

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
        {
            std::scoped_lock metrics_lock(metrics_mutex_);
            reset_metrics_window_locked(std::chrono::steady_clock::now());
        }

        finishing_.store(false);

        while (!stop_token.stop_requested())
        {
            std::shared_ptr<FileChunk> chunk{};
            std::size_t attempt = 1;

            {
                std::lock_guard retry_lock(retry_mutex_);
                if (!retry_queue_.empty())
                {
                    auto pending = std::move(retry_queue_.front());
                    retry_queue_.pop();
                    chunk = std::move(pending.chunk);
                    attempt = pending.attempt;
                }
            }

            if (!chunk)
            {
                auto chunk_opt = queue_.pop();
                if (!chunk_opt)
                {
                    if (queue_.closed())
                    {
                        finishing_.store(true);

                        std::unique_lock retry_lock(retry_mutex_);
                        retry_cv_.wait(retry_lock, [&] {
                            if (stop_token.stop_requested())
                            {
                                return true;
                            }
                            if (!retry_queue_.empty())
                            {
                                return true;
                            }
                            std::unique_lock inflight_lock(inflight_mutex_);
                            return inflight_ == 0;
                        });

                        if (stop_token.stop_requested())
                        {
                            break;
                        }

                        if (!retry_queue_.empty())
                        {
                            finishing_.store(false);
                            continue;
                        }

                        retry_lock.unlock();
                        std::unique_lock inflight_lock(inflight_mutex_);
                        if (inflight_ == 0)
                        {
                            break;
                        }

                        finishing_.store(false);
                        continue;
                    }

                    continue;
                }

                chunk = std::make_shared<FileChunk>(std::move(*chunk_opt));
                attempt = 1;
            }

            if (!acquire_slot(stop_token))
            {
                break;
            }

            Connection& connection = next_connection();
            try
            {
                auto& socket = connection.ensure_connected();
                (void)socket;
            }
            catch (const std::exception& ex)
            {
                on_chunk_failure(chunk, attempt, ex.what());
                continue;
            }

            connection.async_send_chunk(
                chunk,
                attempt,
                [this, chunk](const FileChunk&, std::size_t used_attempts) {
                    on_chunk_success(chunk, used_attempts);
                },
                [this, chunk](const FileChunk&, std::size_t used_attempts, const std::string& error) {
                    on_chunk_failure(chunk, used_attempts, error);
                });
        }
    }

    SenderOptions options_;
    BoundedBlockingQueue<FileChunk>& queue_;
    SystemChannels& channels_;
    std::vector<std::unique_ptr<Connection>> connections_;
    std::mutex connection_mutex_;
    std::size_t next_connection_index_{0};
    std::jthread worker_{};

    std::mutex retry_mutex_;
    std::queue<PendingChunk> retry_queue_;
    std::condition_variable retry_cv_;

    std::mutex inflight_mutex_;
    std::condition_variable inflight_cv_;
    std::size_t inflight_{0};
    std::atomic<bool> finishing_{false};

    std::mutex metrics_mutex_;
    MetricsWindow metrics_window_{};
    const std::chrono::seconds metrics_interval_{5};

    bool acquire_slot(const std::stop_token& stop_token)
    {
        std::unique_lock lock(inflight_mutex_);
        inflight_cv_.wait(lock, [&] {
            return inflight_ < options_.connections || stop_token.stop_requested();
        });

        if (stop_token.stop_requested())
        {
            return false;
        }

        ++inflight_;
        return true;
    }

    void release_slot()
    {
        bool notify_retry = false;
        {
            std::scoped_lock lock(inflight_mutex_);
            if (inflight_ > 0)
            {
                --inflight_;
            }
            if (inflight_ == 0 && finishing_.load())
            {
                notify_retry = true;
            }
        }

        inflight_cv_.notify_one();
        if (notify_retry)
        {
            retry_cv_.notify_all();
        }
    }

    void wait_for_inflight_zero()
    {
        std::unique_lock lock(inflight_mutex_);
        inflight_cv_.wait(lock, [&] { return inflight_ == 0; });
    }

    std::size_t max_attempts() const
    {
        return std::max<std::size_t>(std::size_t{1}, options_.max_send_retries);
    }

    void on_chunk_success(const std::shared_ptr<FileChunk>& chunk, std::size_t attempt)
    {
        const auto retries = attempt > 0 ? attempt - 1 : 0;
        const auto payload_size = chunk->payload.size();

        {
            std::scoped_lock metrics_lock(metrics_mutex_);
            metrics_window_.chunks += 1;
            metrics_window_.bytes += payload_size;
            metrics_window_.retries += retries;
            maybe_report_metrics_locked(std::chrono::steady_clock::now(), false);
        }

        channels_.notify_control(options_.connections, active_connections());

        std::cout << "[sender] chunk sent: " << chunk->descriptor.path << " (#" << chunk->index << "/"
                  << chunk->total_chunks << ") attempts=" << attempt << std::endl;

        chunk->payload.clear();
        chunk->payload.shrink_to_fit();

        release_slot();
    }

    void on_chunk_failure(const std::shared_ptr<FileChunk>& chunk,
                          std::size_t attempt,
                          const std::string& error)
    {
        const auto attempts_limit = max_attempts();
        if (attempt >= attempts_limit)
        {
            {
                std::scoped_lock metrics_lock(metrics_mutex_);
                metrics_window_.retries += attempt > 0 ? attempt - 1 : 0;
                maybe_report_metrics_locked(std::chrono::steady_clock::now(), false);
            }

            std::cerr << "[sender] dropping chunk for " << chunk->descriptor.path << " after retries";
            if (!error.empty())
            {
                std::cerr << " reason=" << error;
            }
            std::cerr << std::endl;

            chunk->payload.clear();
            chunk->payload.shrink_to_fit();

            release_slot();
            return;
        }

        {
            std::lock_guard retry_lock(retry_mutex_);
            retry_queue_.push(PendingChunk{chunk, attempt + 1});
        }

        retry_cv_.notify_one();
        release_slot();
    }

    void reset_metrics_window_locked(std::chrono::steady_clock::time_point now)
    {
        metrics_window_.start = now;
        metrics_window_.chunks = 0;
        metrics_window_.bytes = 0;
        metrics_window_.retries = 0;
    }

    void maybe_report_metrics_locked(std::chrono::steady_clock::time_point now, bool force)
    {
        const auto elapsed = now - metrics_window_.start;
        if (!force && elapsed < metrics_interval_)
        {
            return;
        }

        const double seconds = std::chrono::duration<double>(elapsed).count();
        const double chunk_rate = seconds > 0 ? static_cast<double>(metrics_window_.chunks) / seconds : 0.0;
        const double mb_rate = seconds > 0
                                    ? (static_cast<double>(metrics_window_.bytes) / (1024.0 * 1024.0)) / seconds
                                    : 0.0;

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "[metrics] queue=" << queue_.size() << '/' << queue_.capacity() << " chunk_rate=" << chunk_rate
            << "/s mb_rate=" << mb_rate << " retries=" << metrics_window_.retries;
        if (force)
        {
            oss << " (final)";
        }
        std::cout << oss.str() << std::endl;

        reset_metrics_window_locked(now);
    }
};

}  // namespace sv::client
