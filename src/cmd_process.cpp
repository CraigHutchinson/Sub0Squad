#include "cmd_process.hpp"

#include "sub0squad/process.hpp"

#include <cstdio>
#include <fstream>

namespace sub0squad {

namespace fs = std::filesystem;

int runProcessCommand(const std::vector<std::string>& args, const fs::path& stateDir)
{
    const fs::path dir = stateDir / "processes";
    const auto usage = [] {
        std::fputs("process list | show NAME | add NAME [--from BASE] [--set PATH=VALUE]... [--describe TEXT]\n"
                   "  a process is a workflow as data: which executor writes developers' and testers' items, which rungs of the escalation ladder exist, and the wall times\n"
                   "  `add` writes <state>/processes/NAME.json (a copy of BASE, default standard, with the --set values applied); use it with `do \"...\" --process NAME`\n", stderr);
        return 2;
    };
    if (args.empty()) return usage();
    const std::string& sub = args[0];
    if (sub == "list") {
        for (const auto& name : processNames(dir)) {
            const auto p = findProcess(name, dir);
            std::printf("%-20s %s\n", name.c_str(), p ? p->description.c_str() : ("(unreadable: " + p.error() + ")").c_str());
        }
        return 0;
    }
    if (sub == "show") {
        if (args.size() < 2) return usage();
        const auto p = findProcess(args[1], dir);
        if (!p) { std::fprintf(stderr, "%s\n", p.error().c_str()); return 1; }
        std::printf("%s\n", p->toJson().c_str());
        return 0;
    }
    if (sub == "add") {
        if (args.size() < 2) return usage();
        const std::string name = args[1];
        std::string from = "standard", describe;
        std::vector<std::pair<std::string, std::string>> sets;
        for (std::size_t i = 2; i < args.size(); ++i) {
            if (args[i] == "--from" && i + 1 < args.size()) from = args[++i];
            else if (args[i] == "--describe" && i + 1 < args.size()) describe = args[++i];
            else if (args[i] == "--set" && i + 1 < args.size()) {
                const std::string kv = args[++i];
                const auto eq = kv.find('=');
                if (eq == std::string::npos) { std::fprintf(stderr, "--set takes PATH=VALUE, not '%s'\n", kv.c_str()); return 2; }
                sets.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
            } else { std::fprintf(stderr, "process add: unknown option '%s'\n", args[i].c_str()); return 2; }
        }
        auto base = findProcess(from, dir);
        if (!base) { std::fprintf(stderr, "--from: %s\n", base.error().c_str()); return 1; }
        Process p = *base;
        p.name = name;
        p.version = 1;
        p.description = describe.empty() ? "based on " + from : describe;
        for (const auto& [path, value] : sets)
            if (auto r = p.set(path, value); !r) { std::fprintf(stderr, "--set %s=%s: %s\n", path.c_str(), value.c_str(), r.error().c_str()); return 1; }
        if (const std::string bad = p.validate(); !bad.empty()) { std::fprintf(stderr, "%s\n", bad.c_str()); return 1; }
        for (const Process* b : builtinProcesses()) if (b->name == name) { std::fprintf(stderr, "'%s' is a built-in process: pick another name\n", name.c_str()); return 1; }
        std::error_code ec;
        fs::create_directories(dir, ec);
        const fs::path file = dir / (name + ".json");
        if (fs::exists(file)) { std::fprintf(stderr, "%s exists: remove it or pick another name\n", file.c_str()); return 1; }
        std::ofstream(file) << p.toJson() << "\n";
        if (!std::ifstream(file)) { std::fprintf(stderr, "cannot write %s\n", file.c_str()); return 1; }
        std::printf("wrote %s\nuse it: sub0squad do \"write a calculator in C++\" --process %s\n", file.c_str(), name.c_str());
        return 0;
    }
    return usage();
}

}  // namespace sub0squad
