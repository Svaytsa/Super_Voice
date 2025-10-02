#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sv::client {

struct FileDescriptor
{
    std::filesystem::path path{};
    std::uintmax_t size{0};
    std::filesystem::file_time_type last_write_time{};
};

struct WatcherOptions
{
    std::filesystem::path root{std::filesystem::path{"C:\\Super_Voise\\Lokal AI Model\\client\\files"}};
    std::chrono::milliseconds poll_interval{std::chrono::milliseconds{2000}};
    bool recursive{true};
};

class DirectoryWatcher
{
public:
    explicit DirectoryWatcher(WatcherOptions options = {}) : options_(std::move(options)) {}

    [[nodiscard]] const WatcherOptions& options() const noexcept { return options_; }

    std::vector<FileDescriptor> scan()
    {
        std::vector<FileDescriptor> updated;
        std::scoped_lock lock(mutex_);

        try
        {
            auto process_entry = [&](const auto& entry) {
                if (!entry.is_regular_file())
                {
                    return;
                }

                FileDescriptor descriptor{};
                descriptor.path = entry.path();
                descriptor.size = entry.file_size();
                descriptor.last_write_time = entry.last_write_time();

                const auto key = make_key(descriptor.path);
                auto known = known_files_.find(key);
                if (known == known_files_.end() || known->second.size != descriptor.size ||
                    known->second.last_write_time != descriptor.last_write_time)
                {
                    known_files_[key] = descriptor;
                    updated.push_back(std::move(descriptor));
                }
            };

            if (options_.recursive)
            {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(options_.root))
                {
                    process_entry(entry);
                }
            }
            else
            {
                for (const auto& entry : std::filesystem::directory_iterator(options_.root))
                {
                    process_entry(entry);
                }
            }
        }
        catch (const std::filesystem::filesystem_error&)
        {
            // Ignore transient errors such as the directory not existing yet.
        }

        return updated;
    }

private:
    using SnapshotKey = std::string;

    static SnapshotKey make_key(const std::filesystem::path& path)
    {
        return path.generic_string();
    }

    WatcherOptions options_{};
    std::mutex mutex_;
    std::unordered_map<SnapshotKey, FileDescriptor> known_files_{};
};

}  // namespace sv::client
