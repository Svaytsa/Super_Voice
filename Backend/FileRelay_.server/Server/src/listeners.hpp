#pragma once

#include <asio.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace server
{

enum class Channel
{
    Health,
    Telemetry,
    Control,
    Ack,
    Data
};

class ListenerManager
{
public:
    using Handler = std::function<void(Channel, asio::ip::tcp::socket&&)>;

    ListenerManager(asio::ip::address address,
                    std::uint16_t sys_base,
                    std::uint16_t data_base,
                    std::size_t initial_data_count,
                    Handler handler)
        : address_{std::move(address)}
        , sys_base_{sys_base}
        , data_base_{data_base}
        , handler_{std::move(handler)}
        , desired_data_count_{initial_data_count}
    {
    }

    ~ListenerManager()
    {
        stop();
    }

    ListenerManager(const ListenerManager&) = delete;
    ListenerManager& operator=(const ListenerManager&) = delete;

    void start()
    {
        std::lock_guard lock{mutex_};
        if (started_)
        {
            return;
        }

        started_ = true;

        for (std::size_t i = 0; i < system_acceptors_.size(); ++i)
        {
            auto ctx = std::make_unique<AcceptorContext>();
            ctx->channel = static_cast<Channel>(i);
            ctx->port = static_cast<std::uint16_t>(sys_base_ + i);
            start_acceptor(*ctx);
            system_acceptors_[i] = std::move(ctx);
        }

        ensure_data_acceptors_locked(desired_data_count_);
    }

    void stop()
    {
        std::lock_guard lock{mutex_};
        if (!started_)
        {
            return;
        }

        for (auto& ctx : system_acceptors_)
        {
            if (ctx)
            {
                stop_acceptor_locked(*ctx);
            }
        }

        for (auto& ctx : data_acceptors_)
        {
            stop_acceptor_locked(*ctx);
        }
        data_acceptors_.clear();

        started_ = false;
    }

    void update_data_listener_count(std::size_t new_count)
    {
        std::lock_guard lock{mutex_};
        desired_data_count_ = new_count;
        if (!started_)
        {
            return;
        }
        ensure_data_acceptors_locked(new_count);
    }

private:
    struct AcceptorContext
    {
        std::unique_ptr<asio::io_context> io;
        std::unique_ptr<asio::ip::tcp::acceptor> acceptor;
        std::thread thread;
        std::atomic<bool> running{false};
        Channel channel{Channel::Data};
        std::uint16_t port{};
    };

    void ensure_data_acceptors_locked(std::size_t target)
    {
        if (target == data_acceptors_.size())
        {
            return;
        }

        if (target < data_acceptors_.size())
        {
            while (data_acceptors_.size() > target)
            {
                auto ctx = std::move(data_acceptors_.back());
                data_acceptors_.pop_back();
                stop_acceptor_locked(*ctx);
            }
            return;
        }

        for (std::size_t i = data_acceptors_.size(); i < target; ++i)
        {
            auto ctx = std::make_unique<AcceptorContext>();
            ctx->channel = Channel::Data;
            ctx->port = static_cast<std::uint16_t>(data_base_ + i);
            start_acceptor(*ctx);
            data_acceptors_.push_back(std::move(ctx));
        }
    }

    void start_acceptor(AcceptorContext& ctx)
    {
        ctx.io = std::make_unique<asio::io_context>();
        ctx.acceptor = std::make_unique<asio::ip::tcp::acceptor>(*ctx.io);

        asio::ip::tcp::endpoint endpoint{address_, ctx.port};

        std::error_code ec;
        ctx.acceptor->open(endpoint.protocol(), ec);
        if (ec)
        {
            std::clog << "[listeners] failed to open port " << ctx.port << ": " << ec.message()
                      << '\n';
            return;
        }
        ctx.acceptor->set_option(asio::ip::tcp::acceptor::reuse_address(true), ec);
        if (ec)
        {
            std::clog << "[listeners] failed to set reuse_address on port " << ctx.port << ": "
                      << ec.message() << '\n';
        }
        ctx.acceptor->bind(endpoint, ec);
        if (ec)
        {
            std::clog << "[listeners] failed to bind port " << ctx.port << ": " << ec.message()
                      << '\n';
            return;
        }
        ctx.acceptor->listen(asio::socket_base::max_listen_connections, ec);
        if (ec)
        {
            std::clog << "[listeners] failed to listen on port " << ctx.port << ": "
                      << ec.message() << '\n';
            return;
        }

        ctx.running = true;
        ctx.thread = std::thread([this, &ctx]() { accept_loop(ctx); });
        std::clog << "[listeners] listening on port " << ctx.port << '\n';
    }

    void stop_acceptor_locked(AcceptorContext& ctx)
    {
        ctx.running = false;
        if (ctx.acceptor)
        {
            std::error_code ignored;
            ctx.acceptor->cancel(ignored);
            ctx.acceptor->close(ignored);
        }
        if (ctx.io)
        {
            ctx.io->stop();
        }
        if (ctx.thread.joinable())
        {
            ctx.thread.join();
        }
    }

    void accept_loop(AcceptorContext& ctx)
    {
        auto& io = *ctx.io;
        while (ctx.running)
        {
            asio::ip::tcp::socket socket{io};
            std::error_code ec;
            ctx.acceptor->accept(socket, ec);
            if (ec)
            {
                if (ctx.running)
                {
                    std::clog << "[listeners] accept error on port " << ctx.port << ": "
                              << ec.message() << '\n';
                    std::this_thread::sleep_for(std::chrono::milliseconds{250});
                }
                continue;
            }

            if (!handler_)
            {
                std::clog << "[listeners] dropping connection on port " << ctx.port
                          << " (no handler)\n";
                continue;
            }

            try
            {
                std::thread{[handler = handler_, channel = ctx.channel,
                             sock = std::move(socket)]() mutable {
                    try
                    {
                        handler(channel, std::move(sock));
                    }
                    catch (const std::exception& ex)
                    {
                        std::clog << "[listeners] handler exception: " << ex.what() << '\n';
                    }
                    catch (...)
                    {
                        std::clog << "[listeners] handler unknown exception\n";
                    }
                }}.detach();
            }
            catch (const std::exception& ex)
            {
                std::clog << "[listeners] failed to dispatch handler: " << ex.what() << '\n';
            }
        }
    }

    asio::ip::address address_;
    std::uint16_t sys_base_;
    std::uint16_t data_base_;
    Handler handler_;

    std::array<std::unique_ptr<AcceptorContext>, 4> system_acceptors_{};
    std::vector<std::unique_ptr<AcceptorContext>> data_acceptors_;

    std::mutex mutex_;
    bool started_{false};
    std::size_t desired_data_count_{};
};

} // namespace server

