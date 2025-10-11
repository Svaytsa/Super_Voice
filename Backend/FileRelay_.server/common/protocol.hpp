#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "bytes.hpp"

namespace sv::common::protocol {

using bytes::ByteReader;
using bytes::ByteWriter;

struct PatchHeader {
    static constexpr std::array<char, 4> Magic = {'S', 'V', 'P', '1'};
    static constexpr std::uint32_t Version = 1;
    static constexpr std::size_t EncodedSize = 40;

    std::uint32_t version{Version};
    std::uint32_t header_size{static_cast<std::uint32_t>(EncodedSize)};
    std::uint64_t file_id{};
    std::uint32_t total_patches{};
    std::uint32_t patch_index{};
    std::uint32_t payload_size{};
    std::uint32_t header_crc32{};
    std::uint32_t payload_crc32{};

    [[nodiscard]] std::array<std::uint8_t, EncodedSize> serialize(bool include_header_crc = true) const {
        std::array<std::uint8_t, EncodedSize> out{};
        std::size_t offset = 0;
        std::memcpy(out.data(), Magic.data(), Magic.size());
        offset += Magic.size();
        bytes::write_u32_le(version, out.data() + offset);
        offset += 4;
        bytes::write_u32_le(header_size, out.data() + offset);
        offset += 4;
        bytes::write_u64_le(file_id, out.data() + offset);
        offset += 8;
        bytes::write_u32_le(total_patches, out.data() + offset);
        offset += 4;
        bytes::write_u32_le(patch_index, out.data() + offset);
        offset += 4;
        bytes::write_u32_le(payload_size, out.data() + offset);
        offset += 4;
        auto header_crc = include_header_crc ? header_crc32 : 0U;
        bytes::write_u32_le(header_crc, out.data() + offset);
        offset += 4;
        bytes::write_u32_le(payload_crc32, out.data() + offset);
        return out;
    }

    [[nodiscard]] std::uint32_t compute_header_crc32() const {
        auto buffer = serialize(false);
        return bytes::crc32(std::span<const std::uint8_t>(buffer));
    }

    void finalize_header_crc() {
        header_crc32 = compute_header_crc32();
    }

    void validate() const {
        if (version != Version) {
            throw std::runtime_error("Unsupported patch header version");
        }
        if (header_size != EncodedSize) {
            throw std::runtime_error("Unexpected patch header size");
        }
    }

    static PatchHeader deserialize(std::span<const std::uint8_t> data) {
        if (data.size() < EncodedSize) {
            throw std::runtime_error("PatchHeader::deserialize: insufficient data");
        }
        PatchHeader header;
        if (!std::equal(Magic.begin(), Magic.end(), data.begin())) {
            throw std::runtime_error("Invalid patch header magic");
        }
        std::size_t offset = Magic.size();
        header.version = bytes::read_le<std::uint32_t>(data, offset);
        offset += 4;
        header.header_size = bytes::read_le<std::uint32_t>(data, offset);
        offset += 4;
        header.file_id = bytes::read_le<std::uint64_t>(data, offset);
        offset += 8;
        header.total_patches = bytes::read_le<std::uint32_t>(data, offset);
        offset += 4;
        header.patch_index = bytes::read_le<std::uint32_t>(data, offset);
        offset += 4;
        header.payload_size = bytes::read_le<std::uint32_t>(data, offset);
        offset += 4;
        header.header_crc32 = bytes::read_le<std::uint32_t>(data, offset);
        offset += 4;
        header.payload_crc32 = bytes::read_le<std::uint32_t>(data, offset);

        header.validate();
        auto expected_crc = header.compute_header_crc32();
        if (expected_crc != header.header_crc32) {
            throw std::runtime_error("Patch header CRC mismatch");
        }
        return header;
    }
};

inline std::array<std::uint8_t, PatchHeader::EncodedSize> encode_patch_header(const PatchHeader& header) {
    auto out = header.serialize(true);
    auto expected_crc = header.compute_header_crc32();
    if (expected_crc != header.header_crc32) {
        auto corrected = header;
        corrected.header_crc32 = expected_crc;
        return corrected.serialize(true);
    }
    return out;
}

enum class SystemMessageType : std::uint16_t {
    QueueSizeUpdate = 1,
    FileMeta = 2,
    FilePatchMap = 3,
    Control = 4,
};

struct QueueSizeUpdateMessage {
    std::uint32_t queue_size{};
};

struct FileMetaMessage {
    std::uint64_t file_id{};
    std::string utf8_name;
    std::uint64_t original_size_bytes{};
    std::uint32_t total_patches{};
    std::array<std::uint8_t, 32> sha256{};
};

struct FilePatchMapMessage {
    std::uint64_t file_id{};
    std::uint32_t patch_index{};
};

struct ControlMessage {
    char command{'X'};
    std::uint32_t value_seconds{};
};

using SystemPayload = std::variant<QueueSizeUpdateMessage, FileMetaMessage, FilePatchMapMessage, ControlMessage>;

struct SystemMessage {
    SystemMessageType type{};
    SystemPayload payload{};
};

inline std::vector<std::uint8_t> encode_system_message(const SystemMessage& message) {
    ByteWriter writer;
    writer.write(static_cast<std::uint16_t>(message.type));
    std::visit(
        [&](const auto& payload) {
            using T = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<T, QueueSizeUpdateMessage>) {
                writer.write(payload.queue_size);
            } else if constexpr (std::is_same_v<T, FileMetaMessage>) {
                writer.write(payload.file_id);
                writer.write(static_cast<std::uint32_t>(payload.utf8_name.size()));
                writer.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(payload.utf8_name.data()), payload.utf8_name.size()));
                writer.write(payload.original_size_bytes);
                writer.write(payload.total_patches);
                writer.write_bytes(std::span<const std::uint8_t>(payload.sha256.data(), payload.sha256.size()));
            } else if constexpr (std::is_same_v<T, FilePatchMapMessage>) {
                writer.write(payload.file_id);
                writer.write(payload.patch_index);
            } else if constexpr (std::is_same_v<T, ControlMessage>) {
                const std::uint8_t command_byte = static_cast<std::uint8_t>(payload.command);
                writer.write_bytes(std::span<const std::uint8_t>(&command_byte, 1));
                writer.write(payload.value_seconds);
            }
        },
        message.payload);

    return writer.move_buffer();
}

inline FileMetaMessage decode_file_meta(ByteReader& reader) {
    FileMetaMessage meta;
    meta.file_id = reader.read<std::uint64_t>();
    auto name_size = reader.read<std::uint32_t>();
    auto name_bytes = reader.read_bytes(name_size);
    meta.utf8_name.assign(reinterpret_cast<const char*>(name_bytes.data()), name_bytes.size());
    meta.original_size_bytes = reader.read<std::uint64_t>();
    meta.total_patches = reader.read<std::uint32_t>();
    auto hash_bytes = reader.read_bytes(meta.sha256.size());
    std::copy(hash_bytes.begin(), hash_bytes.end(), meta.sha256.begin());
    return meta;
}

inline ControlMessage decode_control(ByteReader& reader) {
    ControlMessage msg;
    auto command_bytes = reader.read_bytes(1);
    msg.command = static_cast<char>(command_bytes[0]);
    msg.value_seconds = reader.read<std::uint32_t>();
    return msg;
}

inline SystemMessage decode_system_message(std::span<const std::uint8_t> data) {
    ByteReader reader(data);
    SystemMessage message;
    message.type = static_cast<SystemMessageType>(reader.read<std::uint16_t>());
    switch (message.type) {
        case SystemMessageType::QueueSizeUpdate: {
            QueueSizeUpdateMessage payload;
            payload.queue_size = reader.read<std::uint32_t>();
            message.payload = payload;
            break;
        }
        case SystemMessageType::FileMeta: {
            message.payload = decode_file_meta(reader);
            break;
        }
        case SystemMessageType::FilePatchMap: {
            FilePatchMapMessage payload;
            payload.file_id = reader.read<std::uint64_t>();
            payload.patch_index = reader.read<std::uint32_t>();
            message.payload = payload;
            break;
        }
        case SystemMessageType::Control: {
            message.payload = decode_control(reader);
            break;
        }
        default:
            throw std::runtime_error("Unknown system message type");
    }
    return message;
}

}  // namespace sv::common::protocol

