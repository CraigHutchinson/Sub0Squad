#pragma once
/// @file journal.hpp
/// Append-only durable record of everything that changes job state (`journal-1`, NDJSON). The journal is the source of truth;
/// live events are a view of it. One segment file in M1; a torn final line (crash mid-write) is discarded on open.

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sub0squad {

/// One replayed record. `body` is the raw JSON object stored under `d`.
struct JournalRecord
{
    std::uint64_t seq = 0;
    double ts = 0.0;  ///< wall-clock UTC seconds
    std::string type;
    std::string body;
};

/// Replay result for read-only consumers (the CLI when no daemon runs, `doctor`).
struct JournalReplay
{
    std::vector<JournalRecord> records;
    std::uint64_t validBytes = 0;  ///< file offset just after the last good record
    bool tornTail = false;         ///< a partial or unparseable final line was ignored
};

/// Writer. Not thread-safe: owned by the daemon's reactor thread.
class Journal
{
public:
    Journal() = default;
    Journal(const Journal&) = delete;
    Journal& operator=(const Journal&) = delete;
    Journal(Journal&& other) noexcept;
    Journal& operator=(Journal&& other) noexcept;
    ~Journal();

    /// Read every good record without modifying the file. Error: unreadable file, or corruption before the final line.
    [[nodiscard]] static std::expected<JournalReplay, std::string> replay(const std::filesystem::path& dir);

    /// Replay, drop a torn tail from the file, and open for append.
    [[nodiscard]] static std::expected<Journal, std::string> open(const std::filesystem::path& dir);

    /// Append one record. `bodyJson` must be a JSON object. `durable` flushes to disk (state transitions); progress notes may skip it.
    [[nodiscard]] std::expected<std::uint64_t, std::string> append(std::string_view type, std::string_view bodyJson, bool durable = true);

    [[nodiscard]] const std::vector<JournalRecord>& records() const { return records_; }
    [[nodiscard]] std::uint64_t nextSeq() const { return nextSeq_; }

private:
    int fd_ = -1;
    std::uint64_t nextSeq_ = 1;
    std::vector<JournalRecord> records_;
};

}  // namespace sub0squad
