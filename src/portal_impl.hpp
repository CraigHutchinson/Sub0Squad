#pragma once
/// @file portal_impl.hpp
/// The portal's private header: what its translation units share. `portal_sampler.cpp` samples the host and the engines into bounded series and renders
/// them as JSON; `portal_files.cpp` answers for the files of a workflow's sandbox (read only, never outside it); `portal.cpp` is the HTTP server and the
/// API routes; `portal_page.cpp` is the page.

#include "sub0squad/portal.hpp"

#include "json_util.hpp"
#include "sub0squad/engine.hpp"
#include "sub0squad/recall.hpp"

#include <arpa/inet.h>
#include <poll.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>

namespace sub0squad::portal {

namespace fs = std::filesystem;

constexpr std::size_t kKeep = 900;  // 15 minutes at 1 Hz

struct Ring
{
    std::deque<double> v;
    void push(double x) { v.push_back(x); if (v.size() > kKeep) v.pop_front(); }
    void write(json::Writer& w, std::size_t n) const
    {
        w.beginArray();
        for (std::size_t i = v.size() > n ? v.size() - n : 0; i < v.size(); ++i) w.num(v[i]);
        w.endArray();
    }
};

struct SlotLive
{
    bool processing = false;
    long promptTokens = 0, promptProcessed = 0, decoded = 0, remain = -1, idTask = -1;
    long prevDecoded = 0, prevProcessed = 0, prevTask = -2;
    double decodeTps = 0, prefillTps = 0;
    Ring decodeSeries, prefillSeries;
    std::deque<std::array<double, 3>> window;  ///< recent samples (seconds, tokens decoded, prompt tokens processed): the rate is a sum over the window
};

// /slots counts whole tokens, so one second at 5 to 8 tok/s reads as 5, 6, 7 or 8: the rate is taken over the last five samples instead,
// which resolves a fifth of a token per second.
constexpr std::size_t kRateWindow = 5;

struct ModelInfo
{
    bool have = false;
    double fetchedAt = 0;
    std::string alias, file, path, ftype, build;
    long long nParams = 0, sizeBytes = 0, nCtxTrain = 0;
    int totalSlots = 0;
};

struct EngineLive
{
    ModelInfo model;
    bool ok = false;
    std::string error;
    std::map<int, SlotLive> slots;
    Ring decode, prefill, active;
    std::deque<std::array<double, 3>> window;
    double lastT = 0;
};

struct Live
{
    std::mutex m;
    Ring cpu, mem, swap, load, gpu, vram, gtt;
    double memTotalMiB = 0, swapTotalMiB = 0, vramTotalMiB = 0, gttTotalMiB = 0;
    bool hasGpu = false;
    std::map<std::string, EngineLive> engines;
    std::set<std::string> bases;
    std::set<std::string> extra;
    std::string stateJson = "{\"workflows\":{\"workflows\":[]},\"jobs\":{\"jobs\":[]}}";  ///< refreshed by the sampler so a slow daemon never stalls the HTTP loop
};

// ---- portal_sampler.cpp: sampling and the live JSON ----
/// Monotonic seconds.
[[nodiscard]] double nowSec();
[[nodiscard]] std::string slurp(const fs::path& p);
[[nodiscard]] bool cpuTimes(double& busy, double& total);
[[nodiscard]] double meminfoKb(const std::string& text, const char* key);
void sampleSystem(Live& L, double& prevBusy, double& prevTotal);
void sampleEngine(Live& L, const std::string& base);
void fetchModel(Live& L, const std::string& base);
void discover(Live& L, const std::string& text);
[[nodiscard]] std::string liveJson(Live& L, std::size_t n);

// ---- portal_files.cpp: a workflow's files, read only; a path is served only when it resolves inside the sandbox or a job's run directory ----
[[nodiscard]] std::string sandboxOf(Live& L, const std::string& wf);
[[nodiscard]] std::string urlDecode(const std::string& s);
[[nodiscard]] std::string queryParam(const std::string& target, const std::string& name);
[[nodiscard]] bool insideSandbox(const fs::path& root, const std::string& rel, fs::path& out);
[[nodiscard]] const char* mimeOf(const std::string& path);
[[nodiscard]] bool jobFilePath(const fs::path& sandbox, const std::string& rel, fs::path& out);
[[nodiscard]] std::string filesJson(const fs::path& root);

}  // namespace sub0squad::portal
