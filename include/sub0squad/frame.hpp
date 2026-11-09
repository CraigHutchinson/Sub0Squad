#pragma once
/// @file frame.hpp
/// Length-prefixed message framing for the control socket: a 4-byte little-endian length, then that many bytes of JSON.
/// (Sub0Pipeline's FrameReader belongs to its ITransport; the control channel is a plain socket, so this is the whole format.)

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace sub0squad {

constexpr std::size_t kMaxFrameBytes = 8u * 1024u * 1024u;

/// Encode one frame.
[[nodiscard]] std::string encodeFrame(std::string_view payload);

/// Incremental decoder for a non-blocking reader.
class FrameBuffer
{
public:
    void append(const char* data, std::size_t size) { buffer_.append(data, size); }
    /// Next complete frame, `nullopt`-like empty result when more bytes are needed; an error when the declared length is absurd.
    [[nodiscard]] std::expected<bool, std::string> next(std::string& out);

private:
    std::string buffer_;
};

/// Blocking helpers for the CLI side.
[[nodiscard]] std::expected<void, std::string> writeFrameBlocking(int fd, std::string_view payload);
[[nodiscard]] std::expected<std::string, std::string> readFrameBlocking(int fd);

}  // namespace sub0squad
