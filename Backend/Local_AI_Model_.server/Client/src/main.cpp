#include "chunker.hpp"
#include "compressor.hpp"
#include "queue.hpp"
#include "sender.hpp"
#include "system_channels.hpp"
#include "watcher.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <stdexcept>
#include <thread>

namespace {
std::atomic<bool> g_stop_requested{false};

void signal_handler(int)
{
    g_stop_requested.store(true);
}

struct ClientConfig
{
    std::filesystem::path watch_dir = sv::client::WatcherOptions{}.root;
    std::chrono::milliseconds scan_interval = sv::client::WatcherOptions{}.poll_interval;
    std::size_t queue_capacity{32};
    std::size_t chunk_payload_size{2'500'000};
    int compression_level{ZSTD_CLEVEL_DEFAULT};
    std::size_t connections{2};
    std::string host_prefix{"data-base"};
    std::uint16_t base_port{9'000};
    std::size_t max_send_retries{3};
    std::chrono::milliseconds connect_timeout{std::chrono::milliseconds{5000}};
    std::size_t max_connect_attempts{3};
    std::chrono::milliseconds connect_retry_delay{std::chrono::milliseconds{500}};
    bool tcp_no_delay{true};
    std::chrono::milliseconds queue_update_period{std::chrono::milliseconds{3000}};
    std::string control_host{"127.0.0.1"};
    std::uint16_t control_port{7000};
};

void print_usage(std::string_view executable)
{
    std::cout << "Client Application\n"
              << "Usage: " << executable << " [options]\n\n"
              << "Options:\n"
              << "  -h, --help                 Show this help message\n"
              << "  --watch-dir PATH           Directory to monitor\n"
              << "  --scan-interval-ms N       Scan interval in milliseconds\n"
              << "  --queue-capacity N         Maximum number of chunks buffered\n"
              << "  --chunk-size N             Chunk payload size in bytes\n"
              << "  --compression-level N      Zstd compression level\n"
              << "  --connections N            Number of parallel connections\n"
              << "  --host-prefix NAME         Host prefix for data channels (e.g. data-base)\n"
              << "  --base-port PORT           Base port for data channels\n"
              << "  --max-send-retries N       Chunk send retry attempts\n"
              << "  --connect-timeout-ms N     Connection timeout in milliseconds\n"
              << "  --max-connect-attempts N   Connection retry attempts\n"
              << "  --connect-retry-delay-ms N Delay between connection retry attempts\n"
              << "  --control-host HOST        System channel host\n"
              << "  --control-port PORT        System channel port\n"
              << "  --queue-update-ms N        System channel queue update period\n"
              << "  --no-tcp-no-delay          Disable TCP_NODELAY on data channels\n";
}

bool parse_arguments(int argc, char** argv, ClientConfig& config)
{
    if (argc <= 1)
    {
        return true;
    }

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg{argv[i]};
        auto require_value = [&](std::string_view name) -> std::string {
            if (i + 1 >= argc)
            {
                throw std::runtime_error(std::string{"Missing value for option "} + std::string{name});
            }
            return argv[++i];
        };

        try
        {
            if (arg == "-h" || arg == "--help")
            {
                print_usage(argv[0]);
                return false;
            }
            else if (arg == "--watch-dir")
            {
                config.watch_dir = require_value(arg);
            }
            else if (arg == "--scan-interval-ms")
            {
                config.scan_interval = std::chrono::milliseconds{std::stoll(require_value(arg))};
            }
            else if (arg == "--queue-capacity")
            {
                config.queue_capacity = static_cast<std::size_t>(std::stoull(require_value(arg)));
            }
            else if (arg == "--chunk-size")
            {
                config.chunk_payload_size = static_cast<std::size_t>(std::stoull(require_value(arg)));
            }
            else if (arg == "--compression-level")
            {
                config.compression_level = std::stoi(require_value(arg));
            }
            else if (arg == "--connections")
            {
                config.connections = static_cast<std::size_t>(std::stoull(require_value(arg)));
            }
            else if (arg == "--host-prefix")
            {
                config.host_prefix = require_value(arg);
            }
            else if (arg == "--base-port")
            {
                config.base_port = static_cast<std::uint16_t>(std::stoul(require_value(arg)));
            }
            else if (arg == "--max-send-retries")
            {
                config.max_send_retries = static_cast<std::size_t>(std::stoull(require_value(arg)));
            }
            else if (arg == "--connect-timeout-ms")
            {
                config.connect_timeout = std::chrono::milliseconds{std::stoll(require_value(arg))};
            }
            else if (arg == "--max-connect-attempts")
            {
                config.max_connect_attempts = static_cast<std::size_t>(std::stoull(require_value(arg)));
            }
            else if (arg == "--connect-retry-delay-ms")
            {
                config.connect_retry_delay = std::chrono::milliseconds{std::stoll(require_value(arg))};
            }
            else if (arg == "--control-host")
            {
                config.control_host = require_value(arg);
            }
            else if (arg == "--control-port")
            {
                config.control_port = static_cast<std::uint16_t>(std::stoul(require_value(arg)));
            }
            else if (arg == "--queue-update-ms")
            {
                config.queue_update_period = std::chrono::milliseconds{std::stoll(require_value(arg))};
            }
            else if (arg == "--no-tcp-no-delay")
            {
                config.tcp_no_delay = false;
            }
            else
            {
                std::cerr << "Unknown option: " << arg << "\n";
                print_usage(argv[0]);
                return false;
            }
        }
        catch (const std::exception& ex)
        {
            std::cerr << "Error parsing arguments: " << ex.what() << "\n";
            return false;
        }
    }

    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    ClientConfig config{};
    if (!parse_arguments(argc, argv, config))
    {
        return EXIT_SUCCESS;
    }

    sv::client::WatcherOptions watcher_options{};
    watcher_options.root = config.watch_dir;
    watcher_options.poll_interval = config.scan_interval;

    sv::client::DirectoryWatcher watcher{watcher_options};
    sv::client::Compressor compressor{config.compression_level};
    sv::client::Chunker chunker{config.chunk_payload_size};
    BoundedBlockingQueue<sv::client::FileChunk> queue{config.queue_capacity};

    sv::client::SystemChannelOptions system_options{};
    system_options.host = config.control_host;
    system_options.port = config.control_port;
    system_options.queue_update_period = config.queue_update_period;

    sv::client::SystemChannels system_channels{system_options};
    system_channels.set_queue_size_provider([&queue] { return queue.size(); });
    system_channels.start();

    sv::client::SenderOptions sender_options{};
    sender_options.host_prefix = config.host_prefix;
    sender_options.base_port = config.base_port;
    sender_options.connections = config.connections;
    sender_options.max_send_retries = config.max_send_retries;
    sender_options.max_connect_attempts = config.max_connect_attempts;
    sender_options.connect_timeout = config.connect_timeout;
    sender_options.reconnect_delay = config.connect_retry_delay;
    sender_options.tcp_no_delay = config.tcp_no_delay;

    sv::client::Sender sender{sender_options, queue, system_channels};
    sender.start();

    auto last_metrics = std::chrono::steady_clock::now();
    std::size_t files_processed = 0;
    std::uintmax_t bytes_processed = 0;

    while (!g_stop_requested.load())
    {
        const auto updated_files = watcher.scan();
        for (const auto& file : updated_files)
        {
            try
            {
                const auto compressed = compressor(file);
                auto chunks = chunker(compressed);

                for (auto& chunk : chunks)
                {
                    system_channels.notify_file_chunk_enqueued(chunk, queue.size());
                    if (!queue.push(std::move(chunk)))
                    {
                        std::cerr << "Queue closed. Stopping producer." << std::endl;
                        g_stop_requested.store(true);
                        break;
                    }
                }

                ++files_processed;
                bytes_processed += file.size;

                const auto now = std::chrono::steady_clock::now();
                const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_metrics);
                if (elapsed >= std::chrono::seconds{5})
                {
                    std::cout << "[metrics] files=" << files_processed << ", bytes=" << bytes_processed
                              << ", queue_size=" << queue.size() << std::endl;
                    last_metrics = now;
                }
            }
            catch (const std::exception& ex)
            {
                std::cerr << "Failed to process file '" << file.path << "': " << ex.what() << std::endl;
            }

            if (g_stop_requested.load())
            {
                break;
            }
        }

        if (g_stop_requested.load())
        {
            break;
        }

        std::this_thread::sleep_for(config.scan_interval);
    }

    queue.close();
    sender.stop();
    system_channels.stop();

    std::cout << "[metrics] total_files=" << files_processed << ", total_bytes=" << bytes_processed << std::endl;

    return EXIT_SUCCESS;
}
