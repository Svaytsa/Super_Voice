#pragma once

#include "watcher.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <zstd.h>

namespace sv::client {

class Sha256
{
public:
    Sha256()
    {
        state_ = {
            0x6a09e667u,
            0xbb67ae85u,
            0x3c6ef372u,
            0xa54ff53au,
            0x510e527fu,
            0x9b05688cu,
            0x1f83d9abu,
            0x5be0cd19u,
        };
    }

    void update(std::span<const std::uint8_t> data)
    {
        update(data.data(), data.size());
    }

    void update(const void* data, std::size_t size)
    {
        if (finalized_)
        {
            throw std::logic_error("Sha256::update called after finalize");
        }

        const auto* bytes = static_cast<const std::uint8_t*>(data);
        bit_count_ += static_cast<std::uint64_t>(size) * 8;

        std::size_t offset = 0;
        while (offset < size)
        {
            const auto to_copy = std::min<std::size_t>(size - offset, buffer_.size() - buffer_size_);
            std::memcpy(buffer_.data() + buffer_size_, bytes + offset, to_copy);
            buffer_size_ += to_copy;
            offset += to_copy;

            if (buffer_size_ == buffer_.size())
            {
                transform(buffer_.data());
                buffer_size_ = 0;
            }
        }
    }

    std::array<std::uint8_t, 32> finalize()
    {
        if (!finalized_)
        {
            finalized_ = true;

            buffer_[buffer_size_++] = 0x80;
            if (buffer_size_ > 56)
            {
                while (buffer_size_ < 64)
                {
                    buffer_[buffer_size_++] = 0;
                }
                transform(buffer_.data());
                buffer_size_ = 0;
            }

            while (buffer_size_ < 56)
            {
                buffer_[buffer_size_++] = 0;
            }

            for (int i = 7; i >= 0; --i)
            {
                buffer_[buffer_size_++] = static_cast<std::uint8_t>((bit_count_ >> (i * 8)) & 0xffu);
            }

            transform(buffer_.data());
        }

        std::array<std::uint8_t, 32> digest{};
        for (std::size_t i = 0; i < state_.size(); ++i)
        {
            digest[i * 4 + 0] = static_cast<std::uint8_t>((state_[i] >> 24) & 0xffu);
            digest[i * 4 + 1] = static_cast<std::uint8_t>((state_[i] >> 16) & 0xffu);
            digest[i * 4 + 2] = static_cast<std::uint8_t>((state_[i] >> 8) & 0xffu);
            digest[i * 4 + 3] = static_cast<std::uint8_t>(state_[i] & 0xffu);
        }

        return digest;
    }

private:
    void transform(const std::uint8_t block[64])
    {
        static constexpr std::uint32_t k[] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
        };

        auto right_rotate = [](std::uint32_t value, std::uint32_t bits) {
            return (value >> bits) | (value << (32u - bits));
        };

        std::uint32_t w[64];
        for (std::size_t i = 0; i < 16; ++i)
        {
            w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                   (static_cast<std::uint32_t>(block[i * 4 + 3]));
        }
        for (std::size_t i = 16; i < 64; ++i)
        {
            const std::uint32_t s0 = right_rotate(w[i - 15], 7) ^ right_rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = right_rotate(w[i - 2], 17) ^ right_rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];

        for (std::size_t i = 0; i < 64; ++i)
        {
            const std::uint32_t S1 = right_rotate(e, 6) ^ right_rotate(e, 11) ^ right_rotate(e, 25);
            const std::uint32_t ch = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 = h + S1 + ch + k[i] + w[i];
            const std::uint32_t S0 = right_rotate(a, 2) ^ right_rotate(a, 13) ^ right_rotate(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = S0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t bit_count_{0};
    std::size_t buffer_size_{0};
    bool finalized_{false};
};

struct CompressedFile
{
    FileDescriptor descriptor;
    std::string sha256_hex;
    std::vector<std::uint8_t> compressed_data;
};

class Compressor
{
public:
    explicit Compressor(int compression_level = ZSTD_CLEVEL_DEFAULT) : compression_level_(compression_level) {}

    CompressedFile operator()(const FileDescriptor& descriptor) const
    {
        std::ifstream file(descriptor.path, std::ios::binary);
        if (!file)
        {
            throw std::runtime_error("Failed to open file for compression: " + descriptor.path.string());
        }

        Sha256 sha;
        std::vector<std::uint8_t> input;
        input.reserve(static_cast<std::size_t>(descriptor.size));

        std::array<char, 8192> buffer{};
        while (file)
        {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto read = static_cast<std::size_t>(file.gcount());
            if (read == 0)
            {
                break;
            }
            sha.update(buffer.data(), read);
            input.insert(input.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(read));
        }

        const auto digest = sha.finalize();

        ZSTD_CCtx* context = ZSTD_createCCtx();
        if (context == nullptr)
        {
            throw std::runtime_error("Failed to create ZSTD_CCtx");
        }

        ZSTD_CCtx_setParameter(context, ZSTD_c_compressionLevel, compression_level_);

        const auto bound = ZSTD_compressBound(input.size());
        std::vector<std::uint8_t> compressed(bound);

        const auto result = ZSTD_compress2(context,
                                           compressed.data(),
                                           compressed.size(),
                                           input.data(),
                                           input.size());

        ZSTD_freeCCtx(context);

        if (ZSTD_isError(result))
        {
            throw std::runtime_error(std::string{"ZSTD_compress2 failed: "} + ZSTD_getErrorName(result));
        }

        compressed.resize(result);

        CompressedFile output{};
        output.descriptor = descriptor;
        output.sha256_hex = to_hex(digest);
        output.compressed_data = std::move(compressed);
        return output;
    }

private:
    static std::string to_hex(const std::array<std::uint8_t, 32>& digest)
    {
        static constexpr char hex_chars[] = "0123456789abcdef";
        std::string result(64, '\0');
        for (std::size_t i = 0; i < digest.size(); ++i)
        {
            result[i * 2] = hex_chars[(digest[i] >> 4) & 0x0fu];
            result[i * 2 + 1] = hex_chars[digest[i] & 0x0fu];
        }
        return result;
    }

    int compression_level_;
};

}  // namespace sv::client
