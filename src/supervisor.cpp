#include "sub0squad/supervisor.hpp"

#include "json_util.hpp"
#include "sub0squad/platform_process.hpp"
#include "sub0squad/platform_file.hpp"

#include <cstdlib>

namespace sub0squad {

std::expected<platform::ProcessId, std::string> spawnShim(const std::filesystem::path& exe, const std::filesystem::path& runDir)
{
    if (std::getenv("SUB0SQUAD_IN_SHIM")) return std::unexpected("refusing to start a shim from inside a shim");
    using std::filesystem::perms;
    auto shim = platform::spawn({.argv = {exe.string(), "exec-shim", runDir.string()},
                                 .outputFile = runDir / "shim.log",
                                 .appendOutput = true,
                                 .outputPerms = perms::owner_read | perms::owner_write,
                                 .newSession = true});   // a new session and group: the shim leads its own tree and outlives the caller
    if (!shim) return std::unexpected("cannot start the shim: " + shim.error().message);
    return shim->id();   // the daemon reaps it by pid (daemon_jobs.cpp); the handle is not kept
}

std::optional<ShimInfo> readShimInfo(const std::filesystem::path& runDir)
{
    auto text = platform::readFile(runDir / "shim.json");
    if (!text) return std::nullopt;
    ShimInfo info;
    try {
        json::withObject(*text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = f.unescaped_key();
                if (k == "pid") info.pid = f.value().get_int64();
                else if (k == "startTicks") info.startTicks = static_cast<std::uint64_t>(f.value().get_int64());
            }
        });
    } catch (const simdjson::simdjson_error&) { return std::nullopt; }
    return info;
}

std::optional<ExitRecord> readExitRecord(const std::filesystem::path& runDir)
{
    auto text = platform::readFile(runDir / "exit.json");
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
