#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

namespace sv::common::bytes {

namespace detail {

template <typename T>
using EnableIfUInt = std::enable_if_t<std::is_unsigned_v<T> && std::is_integral_v<T>, int>;

constexpr std::uint32_t crc32_table_entry(std::uint32_t index) {
    std::uint32_t crc = index;
    for (int i = 0; i < 8; ++i) {
        if (crc & 1U) {
            crc = (crc >> 1) ^ 0xEDB88320U;
        } else {
            crc >>= 1;
        }
    }
    return crc;
}

constexpr auto build_crc32_table() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < table.size(); ++i) {
        table[i] = crc32_table_entry(i);
    }
    return table;
}

inline const std::array<std::uint32_t, 256>& crc32_table() {
    static const auto table = build_crc32_table();
    return table;
}

inline constexpr std::uint32_t rotr(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

inline constexpr std::uint32_t ch(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return (x & y) ^ (~x & z);
}

inline constexpr std::uint32_t maj(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

inline constexpr std::uint32_t big_sigma0(std::uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

inline constexpr std::uint32_t big_sigma1(std::uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

inline constexpr std::uint32_t small_sigma0(std::uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

inline constexpr std::uint32_t small_sigma1(std::uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

}  // namespace detail

// --- Little endian helpers -------------------------------------------------

template <typename T, detail::EnableIfUInt<T> = 0>
constexpr std::array<std::uint8_t, sizeof(T)> to_le_array(T value) noexcept {
    std::array<std::uint8_t, sizeof(T)> out{};
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        out[i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU);
    }
    return out;
}

template <typename T, detail::EnableIfUInt<T> = 0>
constexpr T from_le_array(const std::array<std::uint8_t, sizeof(T)>& in) noexcept {
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value |= static_cast<T>(static_cast<T>(in[i]) << (8 * i));
    }
    return value;
}

template <typename T, detail::EnableIfUInt<T> = 0>
inline void write_le(T value, std::span<std::uint8_t> buffer, std::size_t offset = 0) {
    if (buffer.size() < offset + sizeof(T)) {
        throw std::out_of_range("write_le: buffer too small");
    }
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        buffer[offset + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU);
    }
}

template <typename T, detail::EnableIfUInt<T> = 0>
inline T read_le(std::span<const std::uint8_t> buffer, std::size_t offset = 0) {
    if (buffer.size() < offset + sizeof(T)) {
        throw std::out_of_range("read_le: buffer too small");
    }
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value |= static_cast<T>(static_cast<T>(buffer[offset + i]) << (8 * i));
    }
    return value;
}

template <typename T, detail::EnableIfUInt<T> = 0>
inline void append_le(std::vector<std::uint8_t>& buffer, T value) {
    auto arr = to_le_array(value);
    buffer.insert(buffer.end(), arr.begin(), arr.end());
}

inline std::uint32_t read_u32_le(const std::uint8_t* data) noexcept {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

inline std::uint64_t read_u64_le(const std::uint8_t* data) noexcept {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(data[i]) << (8 * i);
    }
    return value;
}

inline void write_u32_le(std::uint32_t value, std::uint8_t* out) noexcept {
    out[0] = static_cast<std::uint8_t>(value & 0xFFU);
    out[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFU);
    out[2] = static_cast<std::uint8_t>((value >> 16) & 0xFFU);
    out[3] = static_cast<std::uint8_t>((value >> 24) & 0xFFU);
}

inline void write_u64_le(std::uint64_t value, std::uint8_t* out) noexcept {
    for (int i = 0; i < 8; ++i) {
        out[i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU);
    }
}

class ByteReader {
  public:
    explicit ByteReader(std::span<const std::uint8_t> data) noexcept : data_(data), offset_(0) {}

    template <typename T, detail::EnableIfUInt<T> = 0>
    T read() {
        T value = read_le<T>(data_, offset_);
        offset_ += sizeof(T);
        return value;
    }

    std::span<const std::uint8_t> read_bytes(std::size_t length) {
        if (remaining() < length) {
            throw std::out_of_range("ByteReader::read_bytes: not enough data");
        }
        auto result = data_.subspan(offset_, length);
        offset_ += length;
        return result;
    }

    std::size_t remaining() const noexcept { return data_.size() - offset_; }

    std::size_t offset() const noexcept { return offset_; }

  private:
    std::span<const std::uint8_t> data_;
    std::size_t offset_;
};

class ByteWriter {
  public:
    ByteWriter() = default;

    template <typename T, detail::EnableIfUInt<T> = 0>
    void write(T value) {
        append_le(buffer_, value);
    }

    void write_bytes(std::span<const std::uint8_t> bytes) {
        buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    }

    [[nodiscard]] const std::vector<std::uint8_t>& buffer() const noexcept { return buffer_; }

    std::vector<std::uint8_t>&& move_buffer() noexcept { return std::move(buffer_); }

    void clear() noexcept { buffer_.clear(); }

  private:
    std::vector<std::uint8_t> buffer_{};
};

// --- CRC32 -----------------------------------------------------------------

class Crc32 {
  public:
    Crc32() { reset(); }

    void reset() noexcept { crc_ = 0xFFFFFFFFU; }

    void update(std::span<const std::uint8_t> data) noexcept {
        const auto& table = detail::crc32_table();
        for (auto byte : data) {
            std::uint32_t idx = (crc_ ^ static_cast<std::uint32_t>(byte)) & 0xFFU;
            crc_ = (crc_ >> 8) ^ table[idx];
        }
    }

    [[nodiscard]] std::uint32_t value() const noexcept { return crc_ ^ 0xFFFFFFFFU; }

  private:
    std::uint32_t crc_{};
};

inline std::uint32_t crc32(std::span<const std::uint8_t> data) noexcept {
    Crc32 crc;
    crc.update(data);
    return crc.value();
}

inline std::uint32_t crc32(const void* data, std::size_t size) noexcept {
    const auto* ptr = static_cast<const std::uint8_t*>(data);
    return crc32(std::span<const std::uint8_t>(ptr, size));
}

inline std::uint32_t crc32(const std::string_view& sv) noexcept {
    return crc32(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(sv.data()), sv.size()));
}

// --- SHA-256 ----------------------------------------------------------------

class Sha256 {
  public:
    Sha256() { reset(); }

    void reset() noexcept {
        bit_length_ = 0;
        buffer_length_ = 0;
        state_ = {
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    }

    void update(std::span<const std::uint8_t> data) noexcept {
        bit_length_ += static_cast<std::uint64_t>(data.size()) << 3U;
        for (std::size_t i = 0; i < data.size(); ++i) {
            buffer_[buffer_length_++] = data[i];
            if (buffer_length_ == 64) {
                process_block(buffer_.data());
                buffer_length_ = 0;
            }
        }
    }

    std::array<std::uint8_t, 32> finish() noexcept {
        const auto total_bits = bit_length_;
        buffer_[buffer_length_++] = 0x80U;
        if (buffer_length_ > 56) {
            while (buffer_length_ < 64) {
                buffer_[buffer_length_++] = 0;
            }
            process_block(buffer_.data());
            buffer_length_ = 0;
        }

        while (buffer_length_ < 56) {
            buffer_[buffer_length_++] = 0;
        }

        for (int shift = 56; shift >= 0; shift -= 8) {
            buffer_[buffer_length_++] = static_cast<std::uint8_t>((total_bits >> shift) & 0xFFU);
        }

        process_block(buffer_.data());

        std::array<std::uint8_t, 32> digest{};
        for (std::size_t i = 0; i < state_.size(); ++i) {
            digest[4 * i + 0] = static_cast<std::uint8_t>((state_[i] >> 24) & 0xFFU);
            digest[4 * i + 1] = static_cast<std::uint8_t>((state_[i] >> 16) & 0xFFU);
            digest[4 * i + 2] = static_cast<std::uint8_t>((state_[i] >> 8) & 0xFFU);
            digest[4 * i + 3] = static_cast<std::uint8_t>(state_[i] & 0xFFU);
        }

        reset();
        return digest;
    }

    [[nodiscard]] std::array<std::uint8_t, 32> digest(std::span<const std::uint8_t> data) {
        reset();
        update(data);
        return finish();
    }

  private:
    void process_block(const std::uint8_t* block) noexcept {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                   (static_cast<std::uint32_t>(block[i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            w[i] = detail::small_sigma1(w[i - 2]) + w[i - 7] + detail::small_sigma0(w[i - 15]) + w[i - 16];
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];

        static constexpr std::uint32_t k[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

        for (int i = 0; i < 64; ++i) {
            std::uint32_t temp1 = h + detail::big_sigma1(e) + detail::ch(e, f, g) + k[i] + w[i];
            std::uint32_t temp2 = detail::big_sigma0(a) + detail::maj(a, b, c);
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
    std::uint64_t bit_length_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffer_length_{};
};

inline std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data) {
    Sha256 hasher;
    return hasher.digest(data);
}

inline std::array<std::uint8_t, 32> sha256(const std::string_view& sv) {
    return sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(sv.data()), sv.size()));
}

}  // namespace sv::common::bytes

