#pragma once

#include "storage.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include <zstd.h>

namespace server
{

class Assembler
{
public:
    explicit Assembler(std::filesystem::path files_root)
        : files_root_{std::move(files_root)}
    {
        std::error_code ec;
        std::filesystem::create_directories(files_root_, ec);
        if (ec)
        {
            std::clog << "[assembler] failed to create files directory: " << ec.message() << '\n';
        }
    }

    Assembler(const Assembler&) = delete;
    Assembler& operator=(const Assembler&) = delete;

    std::optional<std::filesystem::path> assemble(const PayloadRecord& record)
    {
        if (record.chunk_files.size() != record.total_chunks)
        {
            std::clog << "[assembler] incomplete record for " << record.file_id << '\n';
            return std::nullopt;
        }

        const auto part_path = files_root_ / (record.original_name + ".part");
        const int out_fd = ::open(part_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (out_fd < 0)
        {
            std::clog << "[assembler] open failed for " << part_path << ": " << std::strerror(errno)
                      << '\n';
            return std::nullopt;
        }

        ZSTD_DStream* stream = ZSTD_createDStream();
        if (!stream)
        {
            std::clog << "[assembler] failed to allocate ZSTD stream\n";
            ::close(out_fd);
            ::unlink(part_path.c_str());
            return std::nullopt;
        }
        ZSTD_initDStream(stream);

        bool success = true;
        size_t pending = 0;
        std::vector<char> input_buffer;
        std::vector<char> output_buffer(ZSTD_DStreamOutSize());

        for (std::size_t idx = 0; idx < record.total_chunks && success; ++idx)
        {
            const auto& chunk_path = record.chunk_files[idx];
            if (chunk_path.empty())
            {
                std::clog << "[assembler] missing chunk " << idx << " for " << record.file_id
                          << '\n';
                success = false;
                break;
            }

            std::ifstream in(chunk_path, std::ios::binary);
            if (!in)
            {
                std::clog << "[assembler] failed to open chunk " << chunk_path << '\n';
                success = false;
                break;
            }
            input_buffer.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());

            ZSTD_inBuffer zin{input_buffer.data(), input_buffer.size(), 0};
            while (zin.pos < zin.size && success)
            {
                ZSTD_outBuffer zout{output_buffer.data(), output_buffer.size(), 0};
                const auto ret = ZSTD_decompressStream(stream, &zout, &zin);
                if (ZSTD_isError(ret))
                {
                    std::clog << "[assembler] ZSTD error: " << ZSTD_getErrorName(ret) << '\n';
                    success = false;
                    break;
                }
                pending = ret;

                if (!flush_buffer(out_fd, output_buffer.data(), zout.pos))
                {
                    success = false;
                    break;
                }
            }
        }

        if (pending != 0)
        {
            std::clog << "[assembler] stream not complete, expected more data" << '\n';
            success = false;
        }

        if (::fsync(out_fd) != 0)
        {
            std::clog << "[assembler] fsync failed for " << part_path << ": " << std::strerror(errno)
                      << '\n';
            success = false;
        }
        ::close(out_fd);
        ZSTD_freeDStream(stream);

        if (!success)
        {
            ::unlink(part_path.c_str());
            return std::nullopt;
        }

        const auto final_path = files_root_ / record.original_name;
        std::error_code ec;
        std::filesystem::rename(part_path, final_path, ec);
        if (ec)
        {
            std::clog << "[assembler] rename failed: " << ec.message() << '\n';
            return std::nullopt;
        }

        std::filesystem::remove_all(record.patches_dir, ec);
        if (ec)
        {
            std::clog << "[assembler] failed to remove patches for " << record.file_id << ": "
                      << ec.message() << '\n';
        }

        return final_path;
    }

private:
    static bool flush_buffer(int fd, const char* data, std::size_t size)
    {
        std::size_t written_total = 0;
        while (written_total < size)
        {
            const ssize_t written = ::write(fd, data + written_total, size - written_total);
            if (written < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                std::clog << "[assembler] write failed: " << std::strerror(errno) << '\n';
                return false;
            }
            written_total += static_cast<std::size_t>(written);
        }
        return true;
    }

    std::filesystem::path files_root_;
};

} // namespace server

