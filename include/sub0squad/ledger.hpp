#pragma once
/// @file ledger.hpp
/// `ledger-1`: an append-only record of every resource use a workflow job made (who, what, tier, tokens, notional cost), so local
/// versus frontier share is measurable and accountable (docs/DESIGN.md sections 15, 16). Costs are NOTIONAL for the stub tiers;
/// nothing is ever sent anywhere.

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace sub0squad {

struct LedgerRecord
{
    double ts = 0;
    std::string workflow, item, role, job, tier, outcome;
    int attempt = 0, run = 1;
    bool stub = false;       ///< true for every tier in this build: no live provider exists
    double durationSec = 0;
    double tokensIn = 0, tokensOut = 0;
    double costUnits = 0;    ///< notional
};

/// Notional price: local tiers cost nothing in units (their cost is time, recorded separately); frontier-stub tokens are priced
/// like a hosted model so the share cap and budgets can be exercised.
[[nodiscard]] double notionalCost(const std::string& tier, double tokensIn, double tokensOut);

[[nodiscard]] std::string toJson(const LedgerRecord& r);
/// Append one line (O_APPEND, one write). Returns false on failure.
[[nodiscard]] bool appendLedger(const std::filesystem::path& file, const LedgerRecord& r);
[[nodiscard]] std::vector<LedgerRecord> readLedger(const std::filesystem::path& file);

struct TierTotals { int attempts = 0; double tokensIn = 0, tokensOut = 0, costUnits = 0, durationSec = 0; };
struct LedgerSummary
{
    std::map<std::string, TierTotals> byTier;
    double frontierTokenShare = 0;  ///< frontier-stub tokens over all tokens, 0 when nothing was recorded
};
[[nodiscard]] LedgerSummary summarise(const std::vector<LedgerRecord>& records, const std::string& workflow);

}  // namespace sub0squad
