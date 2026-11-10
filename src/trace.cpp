#include "sub0squad/trace.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>

namespace sub0squad {

namespace {

std::string now()
{
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char buf[32];
    std::strftime(buf, sizeof buf, "%H:%M:%S", std::localtime(&t));
    return buf;
}

void append(const std::filesystem::path& file, const std::string& text)
{
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream o(file, std::ios::binary | std::ios::app);
    o << text;
    o.flush();  // flushed at once: a killed job keeps every turn it finished
}

std::string fence(const std::string& body)
{
    std::string f = "```";
    while (body.find(f) != std::string::npos) f += "`";
    return f + "\n" + body + (body.empty() || body.back() != '\n' ? "\n" : "") + f + "\n";
}

}  // namespace

void traceBegin(const std::filesystem::path& file, const std::string& heading, const EngineProfile& e, int maxTokens)
{
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream o(file, std::ios::binary | std::ios::trunc);
    o << "# " << heading << "\n\nEngine `" << e.name << "` (" << e.endpoint << ", model `" << e.model << "`), temperature " << e.effectiveTemperature() << ", top_p " << e.effectiveTopP()
      << ", top_k " << e.effectiveTopK() << ", max tokens " << maxTokens << ", thinking " << (e.thinking ? "on" : "off") << (e.seed >= 0 ? ", run seed " + std::to_string(e.seed) : std::string{}) << ". Started " << now() << ".\n";
}

void traceSent(const std::filesystem::path& file, int turn, const std::vector<ChatMessage>& convo, std::size_t from, long long seed)
{
    std::string s = "\n## Turn " + std::to_string(turn) + " (" + now() + ")" + (seed >= 0 ? ", request seed " + std::to_string(seed) : std::string{}) + "\n\n";
    for (std::size_t i = from; i < convo.size(); ++i) s += "### Sent as " + convo[i].role + "\n\n" + fence(convo[i].content) + "\n";
    append(file, s);
}

void traceReply(const std::filesystem::path& file, int turn, const ChatResult& r)
{
    std::string s = "### Reply, turn " + std::to_string(turn) + " (" + now() + "): " + std::to_string(r.promptTokens) + " prompt tokens, " + std::to_string(r.completionTokens) + " completion tokens, finish `" + r.finishReason + "`";
    if (r.predictedMs > 0) { char b[64]; std::snprintf(b, sizeof b, ", %.1f tok/s decode", static_cast<double>(r.completionTokens) / (r.predictedMs / 1000.0)); s += b; }
    if (r.promptMs > 0) { char b[64]; std::snprintf(b, sizeof b, ", %.1f s prompt", r.promptMs / 1000.0); s += b; }
    s += "\n\n";
    if (!r.reasoning.empty()) s += "Reasoning the model returned (" + std::to_string(r.reasoning.size()) + " characters):\n\n" + fence(r.reasoning) + "\n";
    else if (r.reasoningChars > 0) s += "(the model reasoned for " + std::to_string(r.reasoningChars) + " characters that were not kept)\n\n";
    s += "Answer:\n\n" + fence(r.content) + "\n";
    append(file, s);
}

void traceVerdict(const std::filesystem::path& file, int turn, const std::string& text)
{
    append(file, "### Verdict, turn " + std::to_string(turn) + " (" + now() + ")\n\n" + fence(text) + "\n");
}

}  // namespace sub0squad
