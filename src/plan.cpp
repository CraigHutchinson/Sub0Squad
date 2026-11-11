#include "sub0squad/plan.hpp"
#include "sub0squad/harness.hpp"

#include "json_util.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace sub0squad {

namespace {
std::string knownHarnesses() { std::string s; for (const auto& n : harnessNames()) s += (s.empty() ? "" : ", ") + n; return s; }
}  // namespace

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
    return !id.empty() && id.size() <= 60 && std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'; });
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

namespace {
// Tolerant reading, because small models write what comes naturally: a single string where a list is wanted, a number or a boolean written as text.
void readStrings(simdjson::ondemand::value v, std::vector<std::string>& out)
{
    if (v.type().value() == simdjson::ondemand::json_type::string) { out.push_back(json::toString(v)); return; }
    for (auto x : v.get_array()) out.push_back(json::toString(x.value()));
}
int readInt(simdjson::ondemand::value v)
{
    if (v.type().value() == simdjson::ondemand::json_type::string) return std::atoi(json::toString(v).c_str());
    if (v.type().value() == simdjson::ondemand::json_type::number) return static_cast<int>(v.get_double());
    return static_cast<int>(v.get_int64());
}
bool readBool(simdjson::ondemand::value v)
{
    if (v.type().value() == simdjson::ondemand::json_type::string) { const std::string t = json::toString(v); return t == "true" || t == "yes" || t == "1"; }
    return v.get_bool();
}
}  // namespace

std::expected<Plan, std::string> Plan::fromJson(std::string_view text, int cardBudgetTokens)
{
    Plan plan;
    std::string schema;
    std::string where = "the plan";   // where the parser is, so a type error can say which field is wrong
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = f.unescaped_key();
                auto v = f.value();
                if (k == "schema") schema = json::toString(v);
                else if (k == "task") plan.task = json::toString(v);
                else if (k == "harness") plan.harness = json::toString(v);
                else if (k == "project") plan.project = json::toString(v);
                else if (k == "reasons") { where = "the plan: reasons"; readStrings(v.value(), plan.reasons); }
                else if (k == "items") {
                    for (auto itv : v.get_array()) {
                        WorkItem it;
                        for (auto g : itv.get_object()) {
                            const std::string_view gk = g.unescaped_key();
                            auto gv = g.value();
                            where = "item #" + std::to_string(plan.items.size() + 1) + (it.id.empty() ? std::string{} : " ('" + it.id + "')") + ", field '" + std::string{gk} + "'";
                            if (gk == "id") it.id = json::toString(gv);
                            else if (gk == "title") it.title = json::toString(gv);
                            else if (gk == "goal") it.goal = json::toString(gv);
                            else if (gk == "complexity") it.complexity = readInt(gv.value());
                            else if (gk == "persona") it.persona = json::toString(gv);
                            else if (gk == "card") {
                                for (auto cf : gv.get_object()) {
                                    const std::string_view ck = cf.unescaped_key();
                                    auto cv = cf.value();
                                    if (ck == "context") it.card.context = json::toString(cv);
                                    else if (ck == "contract") it.card.contract = json::toString(cv);
                                    else if (ck == "acceptance") { where += ".acceptance"; readStrings(cv.value(), it.card.acceptance); }
                                    else if (ck == "include") { where += ".include"; readStrings(cv.value(), it.card.include); }
                                    else if (ck == "notes") { where += ".notes"; readStrings(cv.value(), it.card.notes); }
                                }
                            } else if (gk == "gate") {
                                for (auto cf : gv.get_object()) {
                                    const std::string_view ck = cf.unescaped_key();
                                    auto cv = cf.value();
                                    if (ck == "kind") it.gate.kind = json::toString(cv);
                                    else if (ck == "minTests") it.gate.minTests = readInt(cv.value());
                                    else if (ck == "mustFailOnStub") it.gate.mustFailOnStub = readBool(cv.value());
                                    else if (ck == "tests") { where += ".tests"; readStrings(cv.value(), it.gate.tests); }
                                }
                            }
                            else if (gk == "files") readStrings(gv.value(), it.files);
                            else if (gk == "deps") readStrings(gv.value(), it.deps);
                        }
                        plan.items.push_back(std::move(it));
                    }
                }
            }
        });
    } catch (const simdjson::simdjson_error& e) {
        return std::unexpected("plan is not valid JSON of the expected shape (at " + where + "): " + e.what());
    }
    if (schema != "plan-1" && schema != "plan-2") return std::unexpected("unsupported plan schema '" + schema + "'");
    if (plan.harness != "" && !findHarness(plan.harness)) return std::unexpected("unknown project harness '" + plan.harness + "' (known: " + knownHarnesses() + ")");
    if (auto ok = plan.validate(cardBudgetTokens); !ok) return std::unexpected(ok.error());
    return plan;
}


std::expected<void, std::string> Plan::validate(int cardBudgetTokens) const
{
    const Plan& plan = *this;
    if (plan.harness != "" && !findHarness(plan.harness)) return std::unexpected("unknown project harness '" + plan.harness + "' (known: " + knownHarnesses() + ")");
    if (plan.items.empty()) return std::unexpected("plan has no items");
    if (plan.items.size() > 32) return std::unexpected("plan has more than 32 items");
    std::set<std::string> ids;
    std::map<std::string, std::string> fileOwner;
    for (const auto& it : plan.items) {
        if (!validId(it.id)) return std::unexpected("invalid item id '" + it.id + "'");
        if (!ids.insert(it.id).second) return std::unexpected("duplicate item id '" + it.id + "'");
        if (it.complexity < 1 || it.complexity > 5) return std::unexpected("item '" + it.id + "': complexity must be 1..5");
        // "integrator" is a coordinator: it writes nothing, waits for its parts and runs its own level's tests over them (the V's ascent)
        if (it.persona != "developer" && it.persona != "tester" && it.persona != "integrator") return std::unexpected("item '" + it.id + "': unknown persona '" + it.persona + "'");
        if (it.card.estimatedTokens() > cardBudgetTokens)
            return std::unexpected("item '" + it.id + "': work card is about " + std::to_string(it.card.estimatedTokens()) + " tokens, over the budget of " + std::to_string(cardBudgetTokens));
        if (!it.gate.kind.empty() && it.gate.kind != "node-test") return std::unexpected("item '" + it.id + "': unknown gate kind '" + it.gate.kind + "'");
        if (it.gate.kind == "node-test" && it.gate.tests.empty()) return std::unexpected("item '" + it.id + "': a node-test gate needs test files");
        if (!plan.harness.empty() && it.gate.kind.empty()) return std::unexpected("item '" + it.id + "': a harness project needs a gate");
        for (const auto& t : it.gate.tests) if (!validPath(t)) return std::unexpected("item '" + it.id + "': bad gate test path '" + t + "'");
        for (const auto& t : it.card.include) if (!validPath(t)) return std::unexpected("item '" + it.id + "': bad include path '" + t + "'");
        if (it.files.empty() && it.persona != "integrator") return std::unexpected("item '" + it.id + "' has no editable files");
        if (it.persona == "integrator" && (it.deps.empty() || it.gate.tests.empty())) return std::unexpected("item '" + it.id + "': an integrator needs parts to wait for and tests to run");
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
    return {};
}

std::expected<Plan, std::string> Plan::amended(const Plan& base, const std::string& parentId, const Plan& fragment, int maxDepth, int cardBudgetTokens)
{
    const WorkItem* parent = base.find(parentId);
    if (!parent) return std::unexpected("no item '" + parentId + "' to replace");
    const int depth = static_cast<int>(std::count(parentId.begin(), parentId.end(), '.'));
    if (depth >= maxDepth) return std::unexpected("item '" + parentId + "' is already at the maximum division depth (" + std::to_string(maxDepth) + ")");
    if (fragment.items.size() < 2) return std::unexpected("a division needs at least two items");
    std::set<std::string> fragIds, depended;
    for (const auto& f : fragment.items) fragIds.insert(f.id);
    for (const auto& f : fragment.items) for (const auto& d : f.deps) depended.insert(d);
    // The V: on a web project the parent stays in the plan as a coordinator that integrates its parts. Each part must be gradeable on its own:
    // a part whose gate is the parent's test file could only pass when every other part's code exists too (the first real page item).
    const Harness* harness = findHarness(base.harness);
    const bool coordinate = harness && harness->coordinators && parent->persona != "integrator";
    if (coordinate) {
        const std::set<std::string> parentTests(parent->gate.tests.begin(), parent->gate.tests.end());
        int developers = 0;
        for (const auto& f : fragment.items) if (f.persona == "developer") ++developers;
        if (developers >= 2)
            for (const auto& f : fragment.items)
                if (f.persona == "developer")
                    for (const auto& t : f.gate.tests)
                        if (parentTests.count(t)) return std::unexpected("part '" + f.id + "' is graded by the parent's test file '" + t + "', which needs the other parts' code too: give each part its own tests (a tester item first)");
    }
    std::set<std::string> covered;
    Plan out = base;
    out.items.clear();
    std::vector<std::string> leaves;
    std::vector<WorkItem> children;
    for (const auto& f : fragment.items) {
        WorkItem c = f;
        c.id = parentId + "." + f.id;
        c.deps.clear();
        if (f.deps.empty()) c.deps = parent->deps;
        for (const auto& d : f.deps) c.deps.push_back(parentId + "." + d);
        for (const auto& file : f.files) covered.insert(file);
        if (!depended.count(f.id)) leaves.push_back(c.id);
        children.push_back(std::move(c));
    }
    for (const auto& file : parent->files)
        if (!covered.count(file)) return std::unexpected("the division does not cover '" + file + "', which the original item delivered");
    for (const auto& it : base.items) {
        if (it.id == parentId) {
            for (auto& c : children) out.items.push_back(c);
            if (coordinate) {  // the same id, so whatever depended on the parent still does; it now waits for the integration too
                WorkItem co;
                co.id = parentId;
                co.title = parent->title + " (integration)";
                co.goal = "Integrate the parts of '" + parentId + "' and pass its tests";
                co.persona = "integrator";
                co.complexity = 1;
                co.deps = leaves;
                co.card = parent->card;
                co.gate = parent->gate;
                co.gate.mustFailOnStub = false;
                out.items.push_back(std::move(co));
            }
            continue;
        }
        WorkItem copy = it;
        std::vector<std::string> deps;
        for (const auto& d : it.deps) {
            if (d != parentId || coordinate) deps.push_back(d);
            else for (const auto& leaf : leaves) deps.push_back(leaf);
        }
        copy.deps = deps;
        out.items.push_back(std::move(copy));
    }
    // include lists that named the parent's output stay valid: children deliver the same files
    if (auto ok = out.validate(cardBudgetTokens); !ok) return std::unexpected("the division is not a valid plan: " + ok.error());
    return out;
}

}  // namespace sub0squad
