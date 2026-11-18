#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace sub0squad::portal {

/// Build the read-only aggregate state from the registered local sources in docs/PORTAL_WORK_OVERVIEW.md.
/// The caller supplies the existing status reader and owns the sampler thread.
[[nodiscard]] std::string workOverview(const std::filesystem::path& registry, std::string localState,
    const std::function<std::string(const std::filesystem::path&)>& readHome);

/// Select a registered home's cached state. An unknown source returns an empty string.
[[nodiscard]] std::string selectWorkSource(const std::string& state, const std::string& source);

/// Read a registered public report by opaque ID. Registry roots and allowed report names are checked at read time.
[[nodiscard]] std::string readWorkReport(const std::filesystem::path& registry, const std::string& id);

} // namespace sub0squad::portal
