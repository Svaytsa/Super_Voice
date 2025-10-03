#include "assembler.hpp"
#include "control.hpp"
#include "listeners.hpp"
#include "storage.hpp"

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace
{

struct Config
{
    asio::ip::address listen_address = asio::ip::make_address("0.0.0.0");
    std::uint16_t sys_base = 7000;
    std::uint16_t data_base = 7100;
    std::size_t data_listeners = 4;
    std::chrono::seconds ttl{3600};
    std::filesystem::path root_dir{"server_data"};
};

struct Metrics
{
    std::atomic<std::uint64_t> accepted{0};
    std::atomic<std::uint64_t> health{0};
    std::atomic<std::uint64_t> telemetry{0};
    std::atomic<std::uint64_t> control{0};
    std::atomic<std::uint64_t> acks{0};
    std::atomic<std::uint64_t> data_connections{0};
    std::atomic<std::uint64_t> chunks{0};
    std::atomic<std::uint64_t> chunk_errors{0};
    std::atomic<std::uint64_t> assemblies{0};
    std::atomic<std::uint64_t> assembly_errors{0};
};

std::atomic<bool> g_running{true};

void signal_handler(int)
{
    g_running.store(false);
}

Config parse_arguments(int argc, char** argv)
{
    Config config;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg{argv[i]};
        if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: " << argv[0]
                      << " [--address 0.0.0.0] [--sys-base 7000] [--data-base 7100] [--x 4]"
                         " [--ttl 3600] [--root server_data]\n";
            std::exit(EXIT_SUCCESS);
        }
        if (arg == "--address" && i + 1 < argc)
        {
            config.listen_address = asio::ip::make_address(argv[++i]);
            continue;
        }
        if (arg == "--sys-base" && i + 1 < argc)
        {
            config.sys_base = static_cast<std::uint16_t>(std::stoi(argv[++i]));
            continue;
        }
        if (arg == "--data-base" && i + 1 < argc)
        {
            config.data_base = static_cast<std::uint16_t>(std::stoi(argv[++i]));
            continue;
        }
        if (arg == "--x" && i + 1 < argc)
        {
            config.data_listeners = static_cast<std::size_t>(std::stoul(argv[++i]));
            continue;
        }
        if (arg == "--ttl" && i + 1 < argc)
        {
            config.ttl = std::chrono::seconds{std::stoll(argv[++i])};
            continue;
        }
        if (arg == "--root" && i + 1 < argc)
        {
            config.root_dir = argv[++i];
            continue;
        }
        std::cerr << "Unknown argument: " << arg << '\n';
    }
    return config;
}

std::string metrics_snapshot(const Metrics& metrics)
{
    std::ostringstream oss;
    oss << "accepted=" << metrics.accepted.load()
        << " health=" << metrics.health.load()
        << " telemetry=" << metrics.telemetry.load()
        << " control=" << metrics.control.load()
        << " ack=" << metrics.acks.load()
        << " data=" << metrics.data_connections.load()
        << " chunks=" << metrics.chunks.load()
        << " chunk_errors=" << metrics.chunk_errors.load()
        << " assemblies=" << metrics.assemblies.load()
        << " assembly_errors=" << metrics.assembly_errors.load();
    return oss.str();
}

std::vector<std::byte> to_bytes(const std::string& value)
{
    std::vector<std::byte> bytes;
    bytes.reserve(value.size());
    for (unsigned char ch : value)
    {
        bytes.push_back(static_cast<std::byte>(ch));
    }
    return bytes;
}

bool read_line(asio::ip::tcp::socket& socket, asio::streambuf& buffer, std::string& line)
{
    std::error_code ec;
    asio::read_until(socket, buffer, '\n', ec);
    if (ec)
    {
        return false;
    }
    std::istream is(&buffer);
    std::getline(is, line);
    return true;
}

void cleanup_completed_files(const std::filesystem::path& files_dir, std::chrono::seconds ttl)
{
    if (ttl <= std::chrono::seconds::zero())
    {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    std::error_code ec;
    std::filesystem::directory_iterator it{files_dir, ec};
    if (ec)
    {
        std::clog << "[cleanup] directory iteration error: " << ec.message() << '\n';
        return;
    }
    for (const auto& entry : it)
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        if (entry.path().extension() == ".part")
        {
            continue;
        }

        std::error_code time_ec;
        const auto file_time = entry.last_write_time(time_ec);
        if (time_ec)
        {
            std::clog << "[cleanup] last_write_time error: " << time_ec.message() << '\n';
            continue;
        }
        const auto system_time = std::chrono::clock_cast<std::chrono::system_clock>(file_time);
        if (now - system_time > ttl)
        {
            std::error_code remove_ec;
            std::filesystem::remove(entry.path(), remove_ec);
            if (remove_ec)
            {
                std::clog << "[cleanup] failed to remove file " << entry.path() << ": "
                          << remove_ec.message() << '\n';
            }
            else
            {
                std::clog << "[cleanup] removed expired file " << entry.path() << '\n';
            }
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    const auto config = parse_arguments(argc, argv);
    Metrics metrics;

    server::Storage storage(config.root_dir, config.ttl);
    server::Assembler assembler(storage.files_dir());
    std::atomic<std::size_t> data_listener_count{config.data_listeners};
    std::atomic<std::chrono::seconds::rep> ttl_seconds{config.ttl.count()};

    auto metrics_hook = [&]() {
        std::clog << "[metrics] " << metrics_snapshot(metrics) << '\n';
    };

    server::ListenerManager listeners(config.listen_address,
                                      config.sys_base,
                                      config.data_base,
                                      config.data_listeners,
                                      [&](server::Channel channel, asio::ip::tcp::socket&& socket) {
                                          metrics.accepted.fetch_add(1);
                                          switch (channel)
                                          {
                                          case server::Channel::Health:
                                              metrics.health.fetch_add(1);
                                              try
                                              {
                                                  const std::string response = "OK\n";
                                                  asio::write(socket, asio::buffer(response));
                                              }
                                              catch (const std::exception& ex)
                                              {
                                                  std::clog << "[health] error: " << ex.what() << '\n';
                                              }
                                              break;
                                          case server::Channel::Telemetry:
                                              metrics.telemetry.fetch_add(1);
                                              try
                                              {
                                                  const std::string response = metrics_snapshot(metrics) + "\n";
                                                  asio::write(socket, asio::buffer(response));
                                              }
                                              catch (const std::exception& ex)
                                              {
                                                  std::clog << "[telemetry] error: " << ex.what() << '\n';
                                              }
                                              break;
                                          case server::Channel::Control:
                                          {
                                              metrics.control.fetch_add(1);
                                              server::ControlPlane control(listeners,
                                                                           storage,
                                                                           data_listener_count,
                                                                           ttl_seconds,
                                                                           metrics_hook);
                                              control.handle_socket(std::move(socket));
                                              break;
                                          }
                                          case server::Channel::Ack:
                                              metrics.acks.fetch_add(1);
                                              try
                                              {
                                                  const std::string response = "ACK\n";
                                                  asio::write(socket, asio::buffer(response));
                                              }
                                              catch (const std::exception& ex)
                                              {
                                                  std::clog << "[ack] error: " << ex.what() << '\n';
                                              }
                                              break;
                                          case server::Channel::Data:
                                              metrics.data_connections.fetch_add(1);
                                              std::thread([socket = std::move(socket), &storage, &assembler, &metrics]() mutable {
                                                  try
                                                  {
                                                      asio::streambuf buffer;
                                                      std::string line;
                                                      server::ChunkData chunk;
                                                      chunk.timestamp = std::chrono::system_clock::now();

                                                      auto fail = [&]() {
                                                          metrics.chunk_errors.fetch_add(1);
                                                      };

                                                      if (!read_line(socket, buffer, line))
                                                      {
                                                          fail();
                                                          return;
                                                      }
                                                      chunk.file_id = line;

                                                      if (!read_line(socket, buffer, line))
                                                      {
                                                          fail();
                                                          return;
                                                      }
                                                      chunk.original_name = line;

                                                      if (!read_line(socket, buffer, line))
                                                      {
                                                          fail();
                                                          return;
                                                      }
                                                      chunk.index = static_cast<std::size_t>(std::stoul(line));

                                                      if (!read_line(socket, buffer, line))
                                                      {
                                                          fail();
                                                          return;
                                                      }
                                                      chunk.total_chunks = static_cast<std::size_t>(std::stoul(line));

                                                      if (chunk.total_chunks == 0 || chunk.index >= chunk.total_chunks)
                                                      {
                                                          fail();
                                                          return;
                                                      }

                                                      if (!read_line(socket, buffer, line))
                                                      {
                                                          fail();
                                                          return;
                                                      }
                                                      chunk.ttl = std::chrono::seconds{std::stoll(line)};

                                                      if (!read_line(socket, buffer, line))
                                                      {
                                                          fail();
                                                          return;
                                                      }
                                                      const auto payload_size = static_cast<std::size_t>(std::stoull(line));

                                                      if (!read_line(socket, buffer, line))
                                                      {
                                                          fail();
                                                          return;
                                                      }
                                                      chunk.header_crc = static_cast<std::uint32_t>(std::stoul(line));

                                                      if (!read_line(socket, buffer, line))
                                                      {
                                                          fail();
                                                          return;
                                                      }
                                                      chunk.payload_crc = static_cast<std::uint32_t>(std::stoul(line));

                                                      const std::string header_blob = chunk.file_id + '\n' + chunk.original_name + '\n' +
                                                                                       std::to_string(chunk.index) + '\n' +
                                                                                       std::to_string(chunk.total_chunks) + '\n' +
                                                                                       std::to_string(chunk.ttl.count()) + '\n' +
                                                                                       std::to_string(payload_size) + '\n';
                                                      chunk.header_bytes = to_bytes(header_blob);

                                                      chunk.payload.resize(payload_size);
                                                      std::size_t received = 0;
                                                      while (received < payload_size)
                                                      {
                                                          std::error_code ec;
                                                          const auto bytes = socket.read_some(
                                                              asio::buffer(chunk.payload.data() + received, payload_size - received), ec);
                                                          if (ec)
                                                          {
                                                              fail();
                                                              return;
                                                          }
                                                          received += bytes;
                                                      }

                                                      std::clog << "[data] patch received file=" << chunk.file_id << " index="
                                                                << chunk.index << '/' << chunk.total_chunks
                                                                << " size=" << payload_size << "B" << '\n';

                                                      metrics.chunks.fetch_add(1);
                                                      if (auto record = storage.store_chunk(chunk))
                                                      {
                                                          if (auto final_path = assembler.assemble(*record))
                                                          {
                                                              metrics.assemblies.fetch_add(1);
                                                              storage.mark_published(record->file_id);
                                                              std::clog << "[assembler] published " << final_path->string() << '\n';
                                                          }
                                                          else
                                                          {
                                                              metrics.assembly_errors.fetch_add(1);
                                                          }
                                                      }

                                                      try
                                                      {
                                                          const std::string response = "STORED\n";
                                                          asio::write(socket, asio::buffer(response));
                                                      }
                                                      catch (const std::exception& ex)
                                                      {
                                                          std::clog << "[data] response error: " << ex.what() << '\n';
                                                      }
                                                  }
                                                  catch (const std::exception& ex)
                                                  {
                                                      metrics.chunk_errors.fetch_add(1);
                                                      std::clog << "[data] exception: " << ex.what() << '\n';
                                                  }
                                              }).detach();
                                              break;
                                          }
                                      });

    listeners.start();

    std::thread cleanup_thread([&]() {
        while (g_running.load())
        {
            std::this_thread::sleep_for(std::chrono::seconds{30});
            const auto ttl_value = std::chrono::seconds{ttl_seconds.load()};
            std::clog << "[cleanup] sweep ttl=" << ttl_value.count() << "s" << '\n';
            storage.cleanup_expired(std::chrono::system_clock::now());
            cleanup_completed_files(storage.files_dir(), ttl_value);
        }
    });

    std::thread metrics_thread([&]() {
        while (g_running.load())
        {
            std::this_thread::sleep_for(std::chrono::seconds{15});
            metrics_hook();
        }
    });

    std::clog << "[main] server running. press Ctrl+C to stop." << '\n';
    while (g_running.load())
    {
        std::this_thread::sleep_for(std::chrono::seconds{1});
    }

    std::clog << "[main] shutting down..." << '\n';
    listeners.stop();

    if (cleanup_thread.joinable())
    {
        cleanup_thread.join();
    }
    if (metrics_thread.joinable())
    {
        metrics_thread.join();
    }

    std::clog << "[main] shutdown complete" << '\n';
    return 0;
}

