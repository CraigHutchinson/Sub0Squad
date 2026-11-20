#include "sub0squad/frame.hpp"


namespace sub0squad {

std::string encodeFrame(std::string_view payload)
{
    std::string out;
    out.resize(4);
    const auto n = static_cast<std::uint32_t>(payload.size());
    for (int i = 0; i < 4; ++i) out[static_cast<std::size_t>(i)] = static_cast<char>((n >> (8 * i)) & 0xFF);
    out.append(payload);
    return out;
}

std::expected<bool, std::string> FrameBuffer::next(std::string& out)
{
    if (buffer_.size() < 4) return false;
    std::uint32_t n = 0;
    for (int i = 0; i < 4; ++i) n |= static_cast<std::uint32_t>(static_cast<unsigned char>(buffer_[static_cast<std::size_t>(i)])) << (8 * i);
    if (n > kMaxFrameBytes) return std::unexpected("frame of " + std::to_string(n) + " bytes exceeds the limit");
    if (buffer_.size() < 4u + n) return false;
    out.assign(buffer_, 4, n);
    buffer_.erase(0, 4u + n);
    return true;
}

std::expected<void, std::string> writeFrameBlocking(sub0platform::SocketStream& stream, std::string_view payload)
{
    const std::string f = encodeFrame(payload);
    if (auto w = stream.writeAll(f); !w) return std::unexpected("write: " + w.error().message);
    return {};
}

std::expected<std::string, std::string> readFrameBlocking(sub0platform::SocketStream& stream, std::optional<std::chrono::milliseconds> timeout)
{
    FrameBuffer fb;
    std::string out;
    char buf[4096];
    for (;;) {
        auto got = fb.next(out);
        if (!got) return std::unexpected(got.error());
        if (*got) return out;
        const auto n = stream.readSome(buf, sizeof buf, timeout);
        if (!n) return std::unexpected("read: " + n.error().message);
        if (!*n) return std::unexpected("timed out");
        if (**n == 0) return std::unexpected("connection closed");
        fb.append(buf, **n);
    }
}

}  // namespace sub0squad
