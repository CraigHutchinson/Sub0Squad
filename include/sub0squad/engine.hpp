#pragma once
/// @file engine.hpp
/// Engine profiles and a blocking OpenAI-compatible chat client (docs/DESIGN.md section 8). An engine is data: where it listens and
/// what it can do. Two kinds are used today and both speak the same protocol, which is the point of the seam:
///   - `external-server`: already running (for example the Ubuntu `qwen3-6` inference snap on 127.0.0.1:8342);
///   - `spawned-server`: started and torn down by the squad as a process job (llama.cpp `llama-server`).
/// The HTTP client is deliberately tiny (loopback only, Connection: close, no TLS); it sits behind this header so the Boost.Beast
/// stack of DECISIONS 18 can replace it without touching callers.

#include <expected>
#include <string>
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
    bool thinking = false;            ///< allow reasoning output (off: faster, cheaper, what the agents want)

    [[nodiscard]] std::string toJson() const;
    /// `name=endpoint[,model=...][,kind=...][,ctx=N][,slots=N]` as given on the command line.
    [[nodiscard]] static std::expected<EngineProfile, std::string> parse(const std::string& text);
};

struct ChatResult
{
    std::string content, finishReason;
    long promptTokens = 0, completionTokens = 0;
    double promptMs = 0, predictedMs = 0;
};

/// One non-streaming chat completion. `timeoutSec` bounds the whole call.
[[nodiscard]] std::expected<ChatResult, std::string> chatComplete(const EngineProfile& engine, const std::string& system, const std::string& user,
                                                                  int maxTokens, double temperature, int timeoutSec);
/// Token count of `text` using the engine's own tokenizer (`<server root>/tokenize`); falls back to length/3.5 and says so via `estimated`.
[[nodiscard]] long countTokens(const EngineProfile& engine, const std::string& text, bool& estimated);

/// Plain HTTP helpers (also used by the web harness smoke check).
struct HttpResponse { int status = 0; std::string body; };
[[nodiscard]] std::expected<HttpResponse, std::string> httpRequest(const std::string& method, const std::string& url, const std::string& body, int timeoutSec);

}  // namespace sub0squad
