#include "sub0squad/job_spec.hpp"

#include "json_util.hpp"

namespace sub0squad {

std::string JobSpec::toJson() const
{
    json::Writer w;
    w.beginObject().kv("schema", "job-spec-1");
    w.key("argv").beginArray();
    for (const auto& a : argv) w.str(a);
    w.endArray();
    w.kv("cwd", cwd);
    w.key("env").beginObject();
    for (const auto& [k, v] : env) w.kv(k, v);
    w.endObject();
    w.kv("tool", tool).kv("tier", tier).kv("owner", owner).kv("dataClass", dataClass);
    w.kv("wallSeconds", wallSeconds).kv("graceSeconds", graceSeconds).kv("expectedEventIntervalSec", expectedEventIntervalSec);
    w.key("resumeArgv").beginArray();
    for (const auto& a : resumeArgv) w.str(a);
    w.endArray();
    w.key("adapter").beginObject().kv("type", adapter.type).kv("path", adapter.path).kv("schema", adapter.schema).endObject();
    w.endObject();
    return w.take();
}

std::expected<JobSpec, std::string> JobSpec::fromJson(std::string_view text)
{
    JobSpec spec;
    std::string schema;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto field : obj) {
                const std::string_view key = field.unescaped_key();
                auto value = field.value();
                if (key == "schema") schema = json::toString(value);
                else if (key == "argv" || key == "resumeArgv") {
                    auto& dst = key == "argv" ? spec.argv : spec.resumeArgv;
                    for (auto item : value.get_array()) dst.push_back(json::toString(item.value()));
                } else if (key == "cwd") spec.cwd = json::toString(value);
                else if (key == "env") {
                    for (auto kv : value.get_object()) {
                        std::string k{std::string_view{kv.unescaped_key()}};
                        spec.env[k] = json::toString(kv.value());
                    }
                } else if (key == "tool") spec.tool = json::toString(value);
                else if (key == "tier") spec.tier = json::toString(value);
                else if (key == "owner") spec.owner = json::toString(value);
                else if (key == "dataClass") spec.dataClass = json::toString(value);
                else if (key == "wallSeconds") spec.wallSeconds = value.get_int64();
                else if (key == "graceSeconds") spec.graceSeconds = value.get_int64();
                else if (key == "expectedEventIntervalSec") spec.expectedEventIntervalSec = value.get_int64();
                else if (key == "adapter") {
                    for (auto kv : value.get_object()) {
                        const std::string_view k = kv.unescaped_key();
                        if (k == "type") spec.adapter.type = json::toString(kv.value());
                        else if (k == "path") spec.adapter.path = json::toString(kv.value());
                        else if (k == "schema") spec.adapter.schema = json::toString(kv.value());
                    }
                }
            }
        });
    } catch (const simdjson::simdjson_error& e) {
        return std::unexpected(std::string{"job spec is not valid JSON of the expected shape: "} + e.what());
    }
    if (!schema.empty() && schema != "job-spec-1") return std::unexpected("unsupported spec schema '" + schema + "'");
    if (spec.argv.empty() || spec.argv[0].empty()) return std::unexpected("job spec needs a non-empty argv");
    if (spec.tier != "stub" && spec.tier != "local") return std::unexpected("tier '" + spec.tier + "' is not available (only stub and local)");
    if (spec.wallSeconds < 0 || spec.graceSeconds < 0) return std::unexpected("negative duration in job spec");
    return spec;
}

}  // namespace sub0squad
