#include "cmd_process.hpp"

#include "sub0squad/process.hpp"

#include <cstdio>
#include <expected>
#include <fstream>
#include <sstream>

namespace sub0squad {

namespace fs = std::filesystem;

int runProcessCommand(const std::vector<std::string>& args, const fs::path& stateDir)
{
    const fs::path dir = stateDir / "processes";
    const auto usage = [] {
        std::fputs("process list | show NAME | tools | check FILE\n"
                   "process add NAME [--from BASE] [--without STAGE]... [--set STAGE.PARAMETER=VALUE]... [--describe TEXT]\n"
                   "process add NAME --file FILE\n"
                   "  a process is a workflow as data: an ordered list of stages. A stage of kind `job` runs one registered tool (`process tools`); the stage of kind\n"
                   "  `items` runs the plan's items (which executor writes them, which rungs of the escalation ladder exist, the wall times). When a stage is entered\n"
                   "  and when it is done are conditions over the workflow's state (`process show standard` is the worked example).\n"
                   "  `add` writes <state>/processes/NAME.json: a copy of BASE (default standard) with stages removed and values set, or FILE once it is checked.\n"
                   "  Use it with `do \"...\" --process NAME`. `check` says what is wrong with a file without registering it.\n", stderr);
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
    if (sub == "tools") {
        for (const StageTool& t : stageTools()) std::printf("%-20s %s%s\n", t.name, t.what, t.needsPlan ? " (needs a plan)" : "");
        return 0;
    }
    const auto readFile = [](const std::string& path) -> std::expected<Process, std::string> {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::unexpected("cannot read " + path);
        std::stringstream ss;
        ss << in.rdbuf();
        auto p = Process::fromJson(ss.str());
        if (!p) return std::unexpected(path + ": " + p.error());
        return p;
    };
    if (sub == "check") {
        if (args.size() < 2) return usage();
        const auto p = readFile(args[1]);
        if (!p) { std::fprintf(stderr, "%s\n", p.error().c_str()); return 1; }
        std::printf("%s: process '%s' is valid, %zu stage(s):", args[1].c_str(), p->name.c_str(), p->stages.size());
        for (const auto& s : p->stages) std::printf(" %s", s.name.c_str());
        std::printf("\n");
        return 0;
    }
    if (sub == "add") {
        if (args.size() < 2) return usage();
        const std::string name = args[1];
        std::string from = "standard", describe, fromFile;
        std::vector<std::pair<std::string, std::string>> sets;
        std::vector<std::string> without;
        for (std::size_t i = 2; i < args.size(); ++i) {
            if (args[i] == "--from" && i + 1 < args.size()) from = args[++i];
            else if (args[i] == "--file" && i + 1 < args.size()) fromFile = args[++i];
            else if (args[i] == "--without" && i + 1 < args.size()) without.push_back(args[++i]);
            else if (args[i] == "--describe" && i + 1 < args.size()) describe = args[++i];
            else if (args[i] == "--set" && i + 1 < args.size()) {
                const std::string kv = args[++i];
                const auto eq = kv.find('=');
                if (eq == std::string::npos) { std::fprintf(stderr, "--set takes PATH=VALUE, not '%s'\n", kv.c_str()); return 2; }
                sets.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
            } else { std::fprintf(stderr, "process add: unknown option '%s'\n", args[i].c_str()); return 2; }
        }
        if (!fromFile.empty() && (!sets.empty() || !without.empty() || from != "standard")) { std::fprintf(stderr, "process add: --file takes the process as written; it cannot be combined with --from, --without or --set\n"); return 2; }
        auto base = fromFile.empty() ? findProcess(from, dir) : readFile(fromFile);
        if (!base) { std::fprintf(stderr, "%s%s\n", fromFile.empty() ? "--from: " : "", base.error().c_str()); return 1; }
        Process p = *base;
        if (!fromFile.empty() && p.name != name) { std::fprintf(stderr, "%s holds a process named '%s', not '%s'\n", fromFile.c_str(), p.name.c_str(), name.c_str()); return 1; }
        p.name = name;
        if (fromFile.empty()) { p.version = 1; p.description = describe.empty() ? "based on " + from : describe; }
        else if (!describe.empty()) p.description = describe;
        for (const auto& stage : without)
            if (auto r = p.removeStage(stage); !r) { std::fprintf(stderr, "--without %s: %s\n", stage.c_str(), r.error().c_str()); return 1; }
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
