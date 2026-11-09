#include "sub0squad/host_guard.hpp"

#include "sub0squad/event_channel.hpp"

#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace sub0squad {

namespace fs = std::filesystem;

HostMemory readHostMemory()
{
    HostMemory m;
    std::ifstream in("/proc/meminfo");
    std::string key;
    long long kb;
    std::string unit;
    while (in >> key >> kb >> unit) {
        if (key == "MemTotal:") m.totalMiB = kb / 1024;
        else if (key == "MemAvailable:") m.availableMiB = kb / 1024;
    }
    return m;
}

std::int64_t freeDiskMiB(const fs::path& path)
{
    fs::path p = fs::absolute(path);
    std::error_code ec;
    while (!p.empty() && !fs::exists(p, ec)) p = p.parent_path();
    struct statvfs sv{};
    if (p.empty() || ::statvfs(p.c_str(), &sv) != 0) return -1;
    return static_cast<std::int64_t>(sv.f_bavail) * static_cast<std::int64_t>(sv.f_frsize) / (1024 * 1024);
}

std::expected<void, std::string> requireMemory(std::int64_t needMiB, std::int64_t reserveMiB)
{
    const HostMemory m = readHostMemory();
    if (needMiB + reserveMiB > m.availableMiB)
        return std::unexpected("not enough free memory: need " + std::to_string(needMiB) + " MiB plus a " + std::to_string(reserveMiB) + " MiB reserve, " + std::to_string(m.availableMiB) +
                               " MiB of " + std::to_string(m.totalMiB) + " MiB are available (stop or unload something first)");
    return {};
}

std::expected<void, std::string> requireDisk(const fs::path& path, std::int64_t needMiB, std::int64_t reserveMiB)
{
    const std::int64_t free = freeDiskMiB(path);
    if (free < 0) return std::unexpected("cannot read the free disk space for " + path.string());
    if (needMiB + reserveMiB > free)
        return std::unexpected("not enough free disk space at " + path.string() + ": need " + std::to_string(needMiB) + " MiB plus a " + std::to_string(reserveMiB) + " MiB reserve, " + std::to_string(free) + " MiB free");
    return {};
}

namespace {
std::string arg(const std::vector<std::string>& a, const std::string& k, const std::string& d = {})
{
    for (std::size_t i = 0; i + 1 < a.size(); ++i) if (a[i] == "--" + k) return a[i + 1];
    return d;
}

struct Run { int code = -1; std::string out; };
Run run(const std::vector<std::string>& argv, bool capture)
{
    Run r;
    int fds[2] = {-1, -1};
    if (capture && ::pipe(fds) != 0) return r;
    const pid_t pid = ::fork();
    if (pid == 0) {
        if (capture) { ::dup2(fds[1], 1); ::close(fds[0]); ::close(fds[1]); }
        std::vector<char*> a;
        for (const auto& s : argv) a.push_back(const_cast<char*>(s.c_str()));
        a.push_back(nullptr);
        ::execvp(a[0], a.data());
        _exit(127);
    }
    if (capture) {
        ::close(fds[1]);
        char buf[4096];
        ssize_t n;
        while ((n = ::read(fds[0], buf, sizeof buf)) > 0) r.out.append(buf, static_cast<std::size_t>(n));
        ::close(fds[0]);
    }
    int st = 0;
    ::waitpid(pid, &st, 0);
    r.code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return r;
}
}  // namespace

int runGuard(const std::vector<std::string>& args)
{
    const HostMemory m = readHostMemory();
    std::printf("memory: %lld MiB available of %lld MiB\n", static_cast<long long>(m.availableMiB), static_cast<long long>(m.totalMiB));
    int rc = 0;
    if (const std::string need = arg(args, "memory-mib"); !need.empty()) {
        auto ok = requireMemory(std::atoll(need.c_str()), std::atoll(arg(args, "reserve-mib", "2048").c_str()));
        std::printf("memory guard: %s\n", ok ? "ok" : ok.error().c_str());
        if (!ok) rc = 1;
    }
    if (const std::string path = arg(args, "disk-path"); !path.empty()) {
        std::printf("disk at %s: %lld MiB free\n", path.c_str(), static_cast<long long>(freeDiskMiB(path)));
        if (const std::string need = arg(args, "disk-mib"); !need.empty()) {
            auto ok = requireDisk(path, std::atoll(need.c_str()));
            std::printf("disk guard: %s\n", ok ? "ok" : ok.error().c_str());
            if (!ok) rc = 1;
        }
    }
    return rc;
}

int runFetchModel(const std::vector<std::string>& args)
{
    const std::string url = arg(args, "url"), out = arg(args, "out");
    std::string sha = arg(args, "sha256");
    if (url.rfind("https://", 0) != 0 || out.empty()) { std::fprintf(stderr, "fetch-model: --url https://... and --out FILE are required\n"); return 64; }
    if (url.find("huggingface.co/") == std::string::npos && arg(args, "any-host").empty()) { std::fprintf(stderr, "fetch-model: only huggingface.co is allowed (pass --any-host 1 to override)\n"); return 64; }
    if (const char* token = std::getenv("HF_TOKEN"); token && *token) std::fprintf(stderr, "fetch-model: HF_TOKEN is set but this stopgap does not send it (gated models are not supported yet)\n");
    events::progress(0, 4, "inspecting");
    // 1. what are we about to download: size and checksum from the response headers (Hugging Face LFS files expose both).
    const Run head = run({"curl", "-sIL", "--max-time", "60", url}, true);
    long long size = -1;
    {
        std::istringstream in(head.out);
        std::string line;
        while (std::getline(in, line)) {
            std::string lower = line;
            for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower.rfind("x-linked-size:", 0) == 0) size = std::atoll(line.c_str() + 14);
            else if (size < 0 && lower.rfind("content-length:", 0) == 0) { const long long v = std::atoll(line.c_str() + 15); if (v > 100000) size = v; }
            else if (sha.empty() && lower.rfind("x-linked-etag:", 0) == 0) {
                const auto q1 = line.find('"'), q2 = line.rfind('"');
                if (q1 != std::string::npos && q2 > q1 + 1) sha = line.substr(q1 + 1, q2 - q1 - 1);
            }
        }
    }
    if (size <= 0) { std::fprintf(stderr, "fetch-model: could not determine the file size from the server\n"); return 2; }
    std::printf("file: %s\nsize: %lld bytes (%.2f GiB)\nsha256: %s\n", out.c_str(), size, static_cast<double>(size) / (1024.0 * 1024.0 * 1024.0), sha.empty() ? "(unknown, not verified)" : sha.c_str());
    std::fflush(stdout);
    // 2. guards: disk first, explicitly (a partial file already on disk counts toward what is needed)
    const fs::path outPath{out}, part = out + ".part";
    std::error_code ec;
    const long long have = fs::exists(part, ec) ? static_cast<long long>(fs::file_size(part, ec)) : 0;
    if (auto ok = requireDisk(outPath, (size - have) / (1024 * 1024) + 1); !ok) { std::fprintf(stderr, "fetch-model: %s\n", ok.error().c_str()); return 1; }
    if (fs::exists(outPath) && static_cast<long long>(fs::file_size(outPath, ec)) == size) std::printf("already complete: %s\n", out.c_str());
    else {
        fs::create_directories(outPath.parent_path(), ec);
        events::progress(1, 4, "downloading");
        events::metric("download.bytes_total", static_cast<double>(size));
        std::printf("downloading (resumable) from byte %lld\n", have);
        std::fflush(stdout);
        const Run dl = run({"curl", "-fL", "-C", "-", "--retry", "5", "--retry-delay", "5", "-sS", "-o", part.string(), url}, false);
        if (dl.code != 0) { std::fprintf(stderr, "fetch-model: download failed (curl exit %d); the partial file is kept for resume\n", dl.code); return 2; }
        if (static_cast<long long>(fs::file_size(part, ec)) != size) { std::fprintf(stderr, "fetch-model: size mismatch after download\n"); return 2; }
        fs::rename(part, outPath, ec);
        if (ec) { std::fprintf(stderr, "fetch-model: cannot move the file into place: %s\n", ec.message().c_str()); return 1; }
    }
    // 3. verify
    events::progress(3, 4, "verifying");
    if (!sha.empty()) {
        const Run sum = run({"sha256sum", out}, true);
        const std::string got = sum.out.substr(0, 64);
        if (got != sha) { std::fprintf(stderr, "fetch-model: CHECKSUM MISMATCH (expected %s, got %s); removing the file\n", sha.c_str(), got.c_str()); fs::remove(outPath, ec); return 2; }
        std::printf("sha256 verified\n");
    }
    events::progress(4, 4, "done");
    std::printf("fetched %s\n", out.c_str());
    return 0;
}

}  // namespace sub0squad
