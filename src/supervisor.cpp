#include "sub0squad/supervisor.hpp"

#include "json_util.hpp"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>

extern char** environ;

namespace sub0squad {

std::expected<pid_t, std::string> spawnShim(const std::filesystem::path& exe, const std::filesystem::path& runDir)
{
    if (std::getenv("SUB0SQUAD_IN_SHIM")) return std::unexpected("refusing to start a shim from inside a shim");
    std::string selfPath = exe.string();
    char* self = selfPath.data();
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);  // new session and group: the shim leads its own tree
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    const std::string log = (runDir / "shim.log").string();
    posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    posix_spawn_file_actions_addclosefrom_np(&fa, 3);
    const std::string dir = runDir.string();
    char* argv[] = {self, const_cast<char*>("exec-shim"), const_cast<char*>(dir.c_str()), nullptr};
    pid_t pid = 0;
    const int rc = ::posix_spawn(&pid, self, &fa, &attr, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) return std::unexpected(std::string{"cannot start shim: "} + std::strerror(rc));
    return pid;
}

namespace {
std::optional<std::string> slurp(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
}  // namespace

std::optional<ShimInfo> readShimInfo(const std::filesystem::path& runDir)
{
    auto text = slurp(runDir / "shim.json");
    if (!text) return std::nullopt;
    ShimInfo info;
    try {
        json::withObject(*text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = f.unescaped_key();
                if (k == "pid") info.pid = static_cast<pid_t>(f.value().get_int64());
                else if (k == "startTicks") info.startTicks = static_cast<std::uint64_t>(f.value().get_int64());
            }
        });
    } catch (const simdjson::simdjson_error&) { return std::nullopt; }
    return info;
}

std::optional<ExitRecord> readExitRecord(const std::filesystem::path& runDir)
{
    auto text = slurp(runDir / "exit.json");
    if (!text) return std::nullopt;
    ExitRecord rec;
    rec.raw = *text;
    try {
        json::withObject(*text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = f.unescaped_key();
                auto v = f.value();
                if (k == "outcome") rec.outcome = json::toString(v);
                else if (k == "teardown") rec.teardown = json::toString(v);
                else if (k == "code") rec.code = static_cast<int>(v.get_int64());
                else if (k == "signal") rec.signal = static_cast<int>(v.get_int64());
            }
        });
    } catch (const simdjson::simdjson_error&) { return std::nullopt; }
    return rec;
}

}  // namespace sub0squad
