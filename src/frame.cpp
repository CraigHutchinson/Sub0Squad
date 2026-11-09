#include "sub0squad/frame.hpp"

#include <unistd.h>
#include <cerrno>
#include <cstring>

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

std::expected<void, std::string> writeFrameBlocking(int fd, std::string_view payload)
{
    const std::string f = encodeFrame(payload);
    const char* p = f.data();
    std::size_t left = f.size();
    while (left > 0) {
        const ssize_t n = ::write(fd, p, left);
        if (n < 0) { if (errno == EINTR) continue; return std::unexpected(std::string{"write: "} + std::strerror(errno)); }
        p += n;
        left -= static_cast<std::size_t>(n);
    }
    return {};
}

std::expected<std::string, std::string> readFrameBlocking(int fd)
{
    FrameBuffer fb;
    std::string out;
    char buf[4096];
    for (;;) {
        auto got = fb.next(out);
        if (!got) return std::unexpected(got.error());
        if (*got) return out;
        const ssize_t n = ::read(fd, buf, sizeof buf);
        if (n < 0) { if (errno == EINTR) continue; return std::unexpected(std::string{"read: "} + std::strerror(errno)); }
        if (n == 0) return std::unexpected("connection closed");
        fb.append(buf, static_cast<std::size_t>(n));
    }
}

}  // namespace sub0squad
