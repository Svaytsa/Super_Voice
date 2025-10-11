#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace server
{

struct ChunkData
{
    std::string file_id;
    std::string original_name;
    std::size_t index{};
    std::size_t total_chunks{};
    std::chrono::system_clock::time_point timestamp;
    std::chrono::seconds ttl{0};
    std::vector<std::byte> header_bytes;
    std::vector<std::byte> payload;
    std::uint32_t header_crc{};
    std::uint32_t payload_crc{};
};

struct PayloadRecord
{
    std::string file_id;
    std::string original_name;
    std::size_t total_chunks{};
    std::filesystem::path patches_dir;
    std::filesystem::path files_dir;
    std::vector<std::filesystem::path> chunk_files;
};

class Storage
{
public:
    Storage(std::filesystem::path root, std::chrono::seconds default_ttl)
        : root_{std::move(root)}
        , patches_dir_{root_ / "patches"}
        , files_dir_{root_ / "files"}
        , default_ttl_rep_{default_ttl.count()}
    {
        std::error_code ec;
        std::filesystem::create_directories(patches_dir_, ec);
        if (ec)
        {
            std::clog << "[storage] failed to create patches directory: " << ec.message() << '\n';
        }
        std::filesystem::create_directories(files_dir_, ec);
        if (ec)
        {
            std::clog << "[storage] failed to create files directory: " << ec.message() << '\n';
        }
    }

    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;

    std::optional<PayloadRecord> store_chunk(const ChunkData& chunk)
    {
        if (!verify_crc(chunk))
        {
            std::clog << "[storage] CRC mismatch for chunk " << chunk.file_id << '#' << chunk.index
                      << '\n';
            return std::nullopt;
        }

        auto manifest_dir = patches_dir_ / chunk.file_id;
        std::error_code ec;
        std::filesystem::create_directories(manifest_dir, ec);
        if (ec)
        {
            std::clog << "[storage] failed to create directory " << manifest_dir << ": "
                      << ec.message() << '\n';
            return std::nullopt;
        }

        const auto patch_path = manifest_dir / patch_file_name(chunk.index);
        if (!write_binary_file(patch_path, chunk.payload))
        {
            std::clog << "[storage] failed to write patch file " << patch_path << '\n';
            return std::nullopt;
        }

        const auto now = std::chrono::system_clock::now();

        std::lock_guard lock{mutex_};
        auto& entry = payloads_[chunk.file_id];
        if (entry.record.chunk_files.empty())
        {
            entry.record.file_id = chunk.file_id;
            entry.record.original_name = chunk.original_name;
            entry.record.total_chunks = chunk.total_chunks;
            entry.record.patches_dir = manifest_dir;
            entry.record.files_dir = files_dir_;
        }
        entry.record.chunk_files.resize(std::max(entry.record.chunk_files.size(), chunk.total_chunks));
        entry.record.chunk_files[chunk.index] = patch_path;
        entry.received.insert(chunk.index);
        entry.last_update = now;
        entry.ttl = chunk.ttl.count() > 0 ? chunk.ttl
                                          : std::chrono::seconds{default_ttl_rep_.load()};
        entry.state = entry.received.size() == entry.record.total_chunks ? "complete" : "partial";

        const auto received_chunks = entry.received.size();
        const auto total_chunks = entry.record.total_chunks;
        const double completeness = total_chunks > 0
                                         ? (static_cast<double>(received_chunks) / static_cast<double>(total_chunks)) *
                                               100.0
                                         : 0.0;
        std::ostringstream completeness_stream;
        completeness_stream << std::fixed << std::setprecision(1) << completeness;
        std::clog << "[storage] chunk stored file=" << chunk.file_id << " index=" << chunk.index << '/' << total_chunks
                  << " size=" << chunk.payload.size() << "B completeness=" << received_chunks << '/'
                  << total_chunks << " (" << completeness_stream.str() << "%)" << '\n';

        persist_manifest(entry.record, entry);

        if (entry.received.size() == entry.record.total_chunks)
        {
            return entry.record;
        }

        return std::nullopt;
    }

    void mark_published(const std::string& file_id)
    {
        std::lock_guard lock{mutex_};
        payloads_.erase(file_id);
    }

    std::vector<PayloadRecord> ready_payloads() const
    {
        std::lock_guard lock{mutex_};
        std::vector<PayloadRecord> ready;
        ready.reserve(payloads_.size());
        for (const auto& [id, entry] : payloads_)
        {
            if (entry.received.size() == entry.record.total_chunks)
            {
                ready.push_back(entry.record);
            }
        }
        return ready;
    }

    void update_ttl(std::chrono::seconds new_ttl)
    {
        default_ttl_rep_.store(new_ttl.count());
        std::lock_guard lock{mutex_};
        for (auto& [_, entry] : payloads_)
        {
            entry.ttl = new_ttl;
            persist_manifest(entry.record, entry);
        }
    }

    void cleanup_expired(std::chrono::system_clock::time_point now)
    {
        std::lock_guard lock{mutex_};
        for (auto it = payloads_.begin(); it != payloads_.end();)
        {
            const auto age = now - it->second.last_update;
            if (age > it->second.ttl)
            {
                std::clog << "[storage] removing expired payload " << it->first << '\n';
                std::error_code ec;
                std::filesystem::remove_all(it->second.record.patches_dir, ec);
                if (ec)
                {
                    std::clog << "[storage] failed to remove "
                              << it->second.record.patches_dir << ": " << ec.message() << '\n';
                }
                it = payloads_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    const std::filesystem::path& patches_dir() const noexcept { return patches_dir_; }
    const std::filesystem::path& files_dir() const noexcept { return files_dir_; }

private:
    struct PayloadEntry
    {
        PayloadRecord record;
        std::set<std::size_t> received;
        std::chrono::system_clock::time_point last_update{};
        std::chrono::seconds ttl{0};
        std::string state{"partial"};
    };

    static std::string patch_file_name(std::size_t index)
    {
        return "patch_" + std::to_string(index) + ".bin";
    }

    static std::uint32_t crc32(std::span<const std::byte> data)
    {
        static constexpr std::uint32_t polynomial = 0xEDB88320u;
        std::uint32_t crc = 0xFFFFFFFFu;
        for (auto byte : data)
        {
            crc ^= static_cast<std::uint32_t>(byte);
            for (int i = 0; i < 8; ++i)
            {
                const bool bit = crc & 1u;
                crc >>= 1u;
                if (bit)
                {
                    crc ^= polynomial;
                }
            }
        }
        return crc ^ 0xFFFFFFFFu;
    }

    bool verify_crc(const ChunkData& chunk) const
    {
        const auto header_crc = crc32(chunk.header_bytes);
        if (header_crc != chunk.header_crc)
        {
            std::clog << "[storage] header CRC mismatch: expected " << chunk.header_crc
                      << " actual " << header_crc << '\n';
            return false;
        }
        const auto payload_crc = crc32(chunk.payload);
        if (payload_crc != chunk.payload_crc)
        {
            std::clog << "[storage] payload CRC mismatch: expected " << chunk.payload_crc
                      << " actual " << payload_crc << '\n';
            return false;
        }
        return true;
    }

    static bool write_binary_file(const std::filesystem::path& path, std::span<const std::byte> data)
    {
        const auto tmp_path = path.string() + ".tmp";
        const int fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (fd < 0)
        {
            std::clog << "[storage] open failed for " << tmp_path << ": " << std::strerror(errno)
                      << '\n';
            return false;
        }

        const auto* buffer = reinterpret_cast<const char*>(data.data());
        std::size_t remaining = data.size();
        while (remaining > 0)
        {
            const ssize_t written = ::write(fd, buffer + (data.size() - remaining), remaining);
            if (written < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                std::clog << "[storage] write failed for " << tmp_path << ": "
                          << std::strerror(errno) << '\n';
                ::close(fd);
                ::unlink(tmp_path.c_str());
                return false;
            }
            remaining -= static_cast<std::size_t>(written);
        }

        if (::fsync(fd) != 0)
        {
            std::clog << "[storage] fsync failed for " << tmp_path << ": " << std::strerror(errno)
                      << '\n';
            ::close(fd);
            ::unlink(tmp_path.c_str());
            return false;
        }
        ::close(fd);

        std::error_code ec;
        std::filesystem::rename(tmp_path, path, ec);
        if (ec)
        {
            std::clog << "[storage] rename failed from " << tmp_path << " to " << path << ": "
                      << ec.message() << '\n';
            return false;
        }
        return true;
    }

    void persist_manifest(const PayloadRecord& record, const PayloadEntry& entry)
    {
        const auto manifest_path = record.patches_dir / "ids.list";
        const auto tmp_path = manifest_path.string() + ".tmp";
        const int fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (fd < 0)
        {
            std::clog << "[storage] open failed for manifest " << tmp_path << ": "
                      << std::strerror(errno) << '\n';
            return;
        }

        const auto ts = std::chrono::duration_cast<std::chrono::seconds>(
                             entry.last_update.time_since_epoch())
                             .count();
        const auto ttl = entry.ttl.count();

        const std::string line = record.file_id + ',' + record.original_name + ',' + std::to_string(ts) +
                                 ',' + std::to_string(ttl) + ',' + entry.state + '\n';

        const auto* raw = line.data();
        std::size_t remaining = line.size();
        while (remaining > 0)
        {
            const ssize_t written = ::write(fd, raw + (line.size() - remaining), remaining);
            if (written < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                std::clog << "[storage] write failed for manifest " << tmp_path << ": "
                          << std::strerror(errno) << '\n';
                ::close(fd);
                ::unlink(tmp_path.c_str());
                return;
            }
            remaining -= static_cast<std::size_t>(written);
        }

        if (::fsync(fd) != 0)
        {
            std::clog << "[storage] fsync failed for manifest " << tmp_path << ": "
                      << std::strerror(errno) << '\n';
            ::close(fd);
            ::unlink(tmp_path.c_str());
            return;
        }
        ::close(fd);

        std::error_code ec;
        std::filesystem::rename(tmp_path, manifest_path, ec);
        if (ec)
        {
            std::clog << "[storage] rename failed for manifest " << manifest_path << ": "
                      << ec.message() << '\n';
        }
    }

    std::filesystem::path root_;
    std::filesystem::path patches_dir_;
    std::filesystem::path files_dir_;
    std::unordered_map<std::string, PayloadEntry> payloads_;
    mutable std::mutex mutex_;
    std::atomic<std::chrono::seconds::rep> default_ttl_rep_;
};

} // namespace server

