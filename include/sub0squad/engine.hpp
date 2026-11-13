#pragma once
/// @file engine.hpp
/// Engine profiles and a blocking OpenAI-compatible chat client (docs/DESIGN.md section 8). An engine is data: where it listens and
/// what it can do. Two kinds are used today and both speak the same protocol, which is the point of the seam:
///   - `external-server`: already running (for example the Ubuntu `qwen3-6` inference snap on 127.0.0.1:8342);
///   - `spawned-server`: started and torn down by the squad as a process job (llama.cpp `llama-server`).
/// The HTTP client is deliberately tiny (loopback only, Connection: close, no TLS); it sits behind this header so the Boost.Beast
/// stack of DECISIONS 18 can replace it without touching callers.

#include <cstddef>
#include <expected>
#include <optional>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sub0squad {

struct EngineProfile
{
    std::string name;                 ///< e.g. "qwen3-6" or "qwen27-iq1s"
    std::string kind = "external-server";
    std::string endpoint;             ///< base URL ending before /chat/completions, e.g. http://127.0.0.1:8342/v1
    std::string model;                ///< model id sent in requests
    int maxConcurrent = 1;            ///< concurrent requests the engine should be given (EngineCaps.maxConcurrentSequences)
    int contextTokens = 4096;
    bool thinking = false;            ///< allow reasoning output (off: faster and cheaper; on: the model reasons before answering, much slower on CPU)
    double temperature = -1, topP = -1;  ///< -1: the recommended default of the mode (below)
    int topK = -1;
    double minP = 0.0, presencePenalty = 0.0;  ///< card: min_p 0.0 for every mode; presence_penalty 1.5 is suggested for general non-thinking use but 0 for precise coding
    long long seed = -1;                       ///< >= 0: a base seed; the agent derives one per (item, attempt, turn) so a run replays. -1: random (the server default)
    /// Which server slot a request is pinned to. `kLeaseSlot` (the default): the slot of the job's lease (`SUB0SQUAD_LEASE_INDEX`), so the portal can
    /// attribute slot activity to a job. `kAnySlot`: not pinned; the server picks an idle slot (llama.cpp prefers the one whose cached prompt is most
    /// similar). A job that runs SEVERAL requests at once must use `kAnySlot`: pinned to its one lease slot they queue behind each other (run-v9: the
    /// forked sections of one job all ran on one slot; docs/INTERVENTIONS.md row 47). A value >= 0 pins to that slot.
    static constexpr int kLeaseSlot = -1, kAnySlot = -2;
    int slot = kLeaseSlot;
    bool cachePrompt = true;                   ///< false in deterministic runs: a cached prefix and a fresh computation can differ in the last digits
    std::string reasoningEffort;               ///< Qwen3.8 only (xhigh | medium | low): the Qwen3.6 template has no such control; empty = not sent
    /// Qwen's published recommendation: non-thinking 0.7 / top_p 0.8 / top_k 20, thinking 0.6 / 0.95 / 20. Greedy-like settings (0.2) make a retry
    /// reproduce the same mistake, which wastes the attempt.
    [[nodiscard]] double effectiveTemperature() const { return temperature >= 0 ? temperature : thinking ? 0.6 : 0.7; }
    [[nodiscard]] double effectiveTopP() const { return topP >= 0 ? topP : thinking ? 0.95 : 0.8; }
    [[nodiscard]] int effectiveTopK() const { return topK >= 0 ? topK : 20; }

    [[nodiscard]] std::string toJson() const;
    /// The command-line form `name=endpoint,model=..,kind=..,ctx=N,slots=N` (inverse of parse).
    [[nodiscard]] std::string toArg() const;
    /// `name=endpoint[,model=...][,kind=...][,ctx=N][,slots=N]` as given on the command line.
    [[nodiscard]] static std::expected<EngineProfile, std::string> parse(const std::string& text);
};

struct ChatResult
{
    std::string content, finishReason;
    long promptTokens = 0, completionTokens = 0;
    double promptMs = 0, predictedMs = 0;
    long cachedTokens = 0;            ///< prompt tokens the server did not have to process because the slot already held them (timings.cache_n)
    std::size_t reasoningChars = 0;   ///< characters of reasoning_content (thinking), which the completion token count includes
    std::string reasoning;            ///< the reasoning text itself when the server returns it (kept in the conversation trace)
};

struct ChatMessage { std::string role, content; };
/// Receives the text of a streaming completion as it is generated: the answer's new characters and the reasoning's new characters (either may be empty).
using DeltaSink = std::function<void(std::string_view content, std::string_view reasoning)>;
/// Asked after every piece of a streaming completion whether to CANCEL the request (docs/INTERVENTIONS.md row 34, early abort): it sees the answer so
/// far and returns true to close the connection, which stops the server generating. The result then has `finishReason` "aborted" and the text so far.
using StreamGuard = std::function<bool(const std::string& contentSoFar)>;
/// One non-streaming chat completion over a whole conversation (the agent's verify-and-repair loop continues the same conversation, so the
/// server can reuse its cache of the shared prefix). Sampling comes from the profile. `timeoutSec` bounds the whole call.
/// With a `sink` the request streams (server-sent events) and the sink sees every piece as it arrives, so a watcher can read the text while it is being
/// generated; the returned ChatResult is the same as without streaming (full text, finish reason, token counts, timings). With a `guard` the request
/// also streams, and is cancelled as soon as the guard says so.
[[nodiscard]] std::expected<ChatResult, std::string> chatCompleteMessages(const EngineProfile& engine, const std::vector<ChatMessage>& messages, int maxTokens, int timeoutSec,
                                                                          const DeltaSink& sink = {}, const StreamGuard& guard = {});
/// Single system+user turn. A negative `temperature` means "use the profile's".
[[nodiscard]] std::expected<ChatResult, std::string> chatComplete(const EngineProfile& engine, const std::string& system, const std::string& user,
                                                                  int maxTokens, double temperature, int timeoutSec);
/// Token count of `text` using the engine's own tokenizer (`<server root>/tokenize`); falls back to length/3.5 and says so via `estimated`.
[[nodiscard]] long countTokens(const EngineProfile& engine, const std::string& text, bool& estimated);

/// A request whose response body is handed to `onBody` in pieces as it arrives (chunked transfer decoded). Returns the HTTP status; a non-200 body goes to `onBody` too.
/// `stop` (optional) is asked after each piece: true closes the connection at once and the call returns the status as if the body had ended.
[[nodiscard]] std::expected<int, std::string> httpStream(const std::string& method, const std::string& url, const std::string& body, int timeoutSec,
                                                         const std::function<void(std::string_view)>& onBody, const std::function<bool()>& stop = {});

/// Plain HTTP helpers (also used by the web harness smoke check).
/// How many request slots a llama.cpp server has and how many are idle right now (from its `/slots`); nullopt when it cannot be read.
struct SlotCounts { int total = 0, idle = 0; };
[[nodiscard]] std::optional<SlotCounts> engineSlots(const EngineProfile& engine);

struct HttpResponse { int status = 0; std::string body; };
[[nodiscard]] std::expected<HttpResponse, std::string> httpRequest(const std::string& method, const std::string& url, const std::string& body, int timeoutSec);

}  // namespace sub0squad
