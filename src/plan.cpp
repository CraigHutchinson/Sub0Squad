#include "sub0squad/plan.hpp"

#include "json_util.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace sub0squad {

std::string Plan::toJson() const
{
    json::Writer w;
    w.beginObject().kv("schema", "plan-2").kv("task", task).kv("harness", harness).kv("project", project);
    w.key("reasons").beginArray();
    for (const auto& r : reasons) w.str(r);
    w.endArray().key("items").beginArray();
    for (const auto& it : items) {
        w.beginObject().kv("id", it.id).kv("title", it.title).kv("persona", it.persona).kv("goal", it.goal).kv("complexity", it.complexity);
        w.key("card").beginObject().kv("context", it.card.context).kv("contract", it.card.contract);
        w.key("acceptance").beginArray(); for (const auto& x : it.card.acceptance) w.str(x); w.endArray();
        w.key("include").beginArray(); for (const auto& x : it.card.include) w.str(x); w.endArray();
        w.key("notes").beginArray(); for (const auto& x : it.card.notes) w.str(x); w.endArray();
        w.endObject();
        w.key("gate").beginObject().kv("kind", it.gate.kind).kv("minTests", it.gate.minTests).kvBool("mustFailOnStub", it.gate.mustFailOnStub);
        w.key("tests").beginArray(); for (const auto& x : it.gate.tests) w.str(x); w.endArray();
        w.endObject();
        w.key("files").beginArray();
        for (const auto& f : it.files) w.str(f);
        w.endArray().key("deps").beginArray();
        for (const auto& d : it.deps) w.str(d);
        w.endArray().endObject();
    }
    w.endArray().endObject();
    return w.take();
}

int WorkCard::estimatedTokens() const
{
    std::size_t n = context.size() + contract.size();
    for (const auto& x : acceptance) n += x.size();
    for (const auto& x : notes) n += x.size();
    return static_cast<int>(n / 4);
}

const WorkItem* Plan::find(std::string_view id) const
{
    for (const auto& it : items) if (it.id == id) return &it;
    return nullptr;
}

namespace {
bool validId(const std::string& id)
{
    return !id.empty() && id.size() <= 40 && std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; });
}
bool validPath(const std::string& p)
{
    if (p.empty() || p[0] == '/' || p.find('\\') != std::string::npos) return false;
    std::size_t pos = 0;
    while (pos <= p.size()) {
        auto e = p.find('/', pos);
        const std::string part = p.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        if (part.empty() || part == "." || part == "..") return false;
        if (e == std::string::npos) break;
        pos = e + 1;
    }
    return true;
}
}  // namespace

std::expected<Plan, std::string> Plan::fromJson(std::string_view text, int cardBudgetTokens)
{
    Plan plan;
    std::string schema;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = f.unescaped_key();
                auto v = f.value();
                if (k == "schema") schema = json::toString(v);
                else if (k == "task") plan.task = json::toString(v);
                else if (k == "harness") plan.harness = json::toString(v);
                else if (k == "project") plan.project = json::toString(v);
                else if (k == "reasons") { for (auto r : v.get_array()) plan.reasons.push_back(json::toString(r.value())); }
                else if (k == "items") {
                    for (auto itv : v.get_array()) {
                        WorkItem it;
                        for (auto g : itv.get_object()) {
                            const std::string_view gk = g.unescaped_key();
                            auto gv = g.value();
                            if (gk == "id") it.id = json::toString(gv);
                            else if (gk == "title") it.title = json::toString(gv);
                            else if (gk == "goal") it.goal = json::toString(gv);
                            else if (gk == "complexity") it.complexity = static_cast<int>(gv.get_int64());
                            else if (gk == "persona") it.persona = json::toString(gv);
                            else if (gk == "card") {
                                for (auto cf : gv.get_object()) {
                                    const std::string_view ck = cf.unescaped_key();
                                    auto cv = cf.value();
                                    if (ck == "context") it.card.context = json::toString(cv);
                                    else if (ck == "contract") it.card.contract = json::toString(cv);
                                    else if (ck == "acceptance") { for (auto x : cv.get_array()) it.card.acceptance.push_back(json::toString(x.value())); }
                                    else if (ck == "include") { for (auto x : cv.get_array()) it.card.include.push_back(json::toString(x.value())); }
                                    else if (ck == "notes") { for (auto x : cv.get_array()) it.card.notes.push_back(json::toString(x.value())); }
                                }
                            } else if (gk == "gate") {
                                for (auto cf : gv.get_object()) {
                                    const std::string_view ck = cf.unescaped_key();
                                    auto cv = cf.value();
                                    if (ck == "kind") it.gate.kind = json::toString(cv);
                                    else if (ck == "minTests") it.gate.minTests = static_cast<int>(cv.get_int64());
                                    else if (ck == "mustFailOnStub") it.gate.mustFailOnStub = cv.get_bool();
                                    else if (ck == "tests") { for (auto x : cv.get_array()) it.gate.tests.push_back(json::toString(x.value())); }
                                }
                            }
                            else if (gk == "files") { for (auto x : gv.get_array()) it.files.push_back(json::toString(x.value())); }
                            else if (gk == "deps") { for (auto x : gv.get_array()) it.deps.push_back(json::toString(x.value())); }
                        }
                        plan.items.push_back(std::move(it));
                    }
                }
            }
        });
    } catch (const simdjson::simdjson_error& e) {
        return std::unexpected(std::string{"plan is not valid JSON of the expected shape: "} + e.what());
    }
    if (schema != "plan-1" && schema != "plan-2") return std::unexpected("unsupported plan schema '" + schema + "'");
    if (plan.harness != "" && plan.harness != "web-static") return std::unexpected("unknown project harness '" + plan.harness + "' (known: web-static)");
    if (plan.items.empty()) return std::unexpected("plan has no items");
    if (plan.items.size() > 32) return std::unexpected("plan has more than 32 items");
    std::set<std::string> ids;
    std::map<std::string, std::string> fileOwner;
    for (const auto& it : plan.items) {
        if (!validId(it.id)) return std::unexpected("invalid item id '" + it.id + "'");
        if (!ids.insert(it.id).second) return std::unexpected("duplicate item id '" + it.id + "'");
        if (it.complexity < 1 || it.complexity > 5) return std::unexpected("item '" + it.id + "': complexity must be 1..5");
        if (it.persona != "developer" && it.persona != "tester") return std::unexpected("item '" + it.id + "': unknown persona '" + it.persona + "'");
        if (it.card.estimatedTokens() > cardBudgetTokens)
            return std::unexpected("item '" + it.id + "': work card is about " + std::to_string(it.card.estimatedTokens()) + " tokens, over the budget of " + std::to_string(cardBudgetTokens));
        if (!it.gate.kind.empty() && it.gate.kind != "node-test") return std::unexpected("item '" + it.id + "': unknown gate kind '" + it.gate.kind + "'");
        if (it.gate.kind == "node-test" && it.gate.tests.empty()) return std::unexpected("item '" + it.id + "': a node-test gate needs test files");
        if (!plan.harness.empty() && it.gate.kind.empty()) return std::unexpected("item '" + it.id + "': a harness project needs a gate");
        for (const auto& t : it.gate.tests) if (!validPath(t)) return std::unexpected("item '" + it.id + "': bad gate test path '" + t + "'");
        for (const auto& t : it.card.include) if (!validPath(t)) return std::unexpected("item '" + it.id + "': bad include path '" + t + "'");
        if (it.files.empty()) return std::unexpected("item '" + it.id + "' has no editable files");
        for (const auto& f : it.files) {
            if (!validPath(f)) return std::unexpected("item '" + it.id + "': bad file path '" + f + "'");
            if (!plan.harness.empty()) {
                const bool isTest = f.rfind("tests/", 0) == 0;
                if (it.persona == "tester" && !isTest) return std::unexpected("item '" + it.id + "': a tester may only write under tests/, not '" + f + "'");
                if (it.persona == "developer" && isTest) return std::unexpected("item '" + it.id + "': a developer may not write tests ('" + f + "')");
            }
            auto [pos, fresh] = fileOwner.emplace(f, it.id);
            if (!fresh) return std::unexpected("items '" + pos->second + "' and '" + it.id + "' both edit '" + f + "' (editable sets must be disjoint)");
        }
    }
    for (const auto& it : plan.items)
        for (const auto& d : it.deps) {
            if (!ids.count(d)) return std::unexpected("item '" + it.id + "' depends on unknown item '" + d + "'");
            if (d == it.id) return std::unexpected("item '" + it.id + "' depends on itself");
        }
    // Acyclic: repeatedly remove items whose dependencies are gone.
    std::set<std::string> done;
    for (bool progress = true; progress && done.size() < plan.items.size();) {
        progress = false;
        for (const auto& it : plan.items) {
            if (done.count(it.id)) continue;
            if (std::all_of(it.deps.begin(), it.deps.end(), [&](const std::string& d) { return done.count(d) > 0; })) { done.insert(it.id); progress = true; }
        }
    }
    if (done.size() != plan.items.size()) return std::unexpected("plan dependencies contain a cycle");
    return plan;
}

}  // namespace sub0squad
