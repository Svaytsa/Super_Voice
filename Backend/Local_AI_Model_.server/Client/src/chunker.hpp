#pragma once

#include "compressor.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sv::client {

struct FileChunk
{
    FileDescriptor descriptor;
    std::string sha256_hex;
    std::size_t index{0};
    std::size_t total_chunks{0};
    std::vector<std::uint8_t> payload;
};

class Chunker
{
public:
    explicit Chunker(std::size_t payload_size = 2'500'000) : payload_size_(payload_size) {}

    [[nodiscard]] std::size_t payload_size() const noexcept { return payload_size_; }

    std::vector<FileChunk> operator()(const CompressedFile& file) const
    {
        std::vector<FileChunk> chunks;
        if (file.compressed_data.empty())
        {
            return chunks;
        }

        const auto total_chunks = (file.compressed_data.size() + payload_size_ - 1) / payload_size_;
        chunks.reserve(total_chunks);
        for (std::size_t index = 0; index < total_chunks; ++index)
        {
            const auto offset = index * payload_size_;
            const auto size = std::min<std::size_t>(payload_size_, file.compressed_data.size() - offset);

            FileChunk chunk{};
            chunk.descriptor = file.descriptor;
            chunk.sha256_hex = file.sha256_hex;
            chunk.index = index;
            chunk.total_chunks = total_chunks;
            chunk.payload.insert(chunk.payload.end(),
                                 file.compressed_data.begin() + static_cast<std::ptrdiff_t>(offset),
                                 file.compressed_data.begin() + static_cast<std::ptrdiff_t>(offset + size));

            chunks.push_back(std::move(chunk));
        }

        return chunks;
    }

private:
    std::size_t payload_size_;
};

}  // namespace sv::client
