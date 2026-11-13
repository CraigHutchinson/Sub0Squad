#include "sub0squad/predicate.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>

namespace sub0squad {

struct Predicate::Node
{
    enum class Kind { either, both, negate, compare, number, truth,
                      always, planPresent, stageDone, stageActive, stageFailed, itemsAll, itemsAny, itemsCount, jobSucceeded, jobFailed, jobActive, jobTries, openBlocking };
    Kind kind = Kind::truth;
    bool isNumber = false;                      ///< the type of the node's value: a number or a truth value
    std::int64_t literal = 0;                   ///< number, truth (0 or 1)
    std::string name;                           ///< a stage, a step or an item state ("" for `items.count()`)
    std::string op;                             ///< compare
    std::vector<std::shared_ptr<const Node>> kids;
    std::size_t begin = 0, end = 0;             ///< the node's text, as offsets into the predicate
};

namespace {

using Node = Predicate::Node;
using NodePtr = std::shared_ptr<const Node>;

bool isState(std::string_view s) { return s == "pending" || s == "running" || s == "accepted" || s == "failed"; }

/// A recursive-descent parser; the first error stops it (`error` is set, every later step returns at once).
struct Parser
{
    std::string_view text;
    const Vocabulary& vocabulary;
    std::size_t at = 0;
    std::string error;

    void fail(std::size_t where, const std::string& what) { if (error.empty()) error = "column " + std::to_string(where + 1) + ": " + what; }
    void skip() { while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at; }
    bool eat(std::string_view token)
    {
        skip();
        if (text.substr(at, token.size()) != token) return false;
        at += token.size();
        return true;
    }
    std::string ident()
    {
        skip();
        const std::size_t from = at;
        while (at < text.size() && (std::isalnum(static_cast<unsigned char>(text[at])) || text[at] == '-' || text[at] == '_')) ++at;
        return std::string{text.substr(from, at - from)};
    }
    static std::shared_ptr<Node> make(Node::Kind kind, std::size_t begin) { auto n = std::make_shared<Node>(); n->kind = kind; n->begin = begin; return n; }
    NodePtr done(std::shared_ptr<Node> n) { n->end = at; return n; }

    NodePtr expr()
    {
        NodePtr left = both();
        while (error.empty() && eat("||")) {
            const std::size_t opAt = at - 2;
            NodePtr right = both();
            if (!error.empty()) break;
            if (left->isNumber || right->isNumber) { fail(opAt, "'||' joins two conditions, not numbers"); break; }
            auto n = make(Node::Kind::either, left->begin);
            n->kids = {left, right};
            left = done(n);
        }
        return left;
    }
    NodePtr both()
    {
        NodePtr left = unary();
        while (error.empty() && eat("&&")) {
            const std::size_t opAt = at - 2;
            NodePtr right = unary();
            if (!error.empty()) break;
            if (left->isNumber || right->isNumber) { fail(opAt, "'&&' joins two conditions, not numbers"); break; }
            auto n = make(Node::Kind::both, left->begin);
            n->kids = {left, right};
            left = done(n);
        }
        return left;
    }
    NodePtr unary()
    {
        skip();
        const std::size_t from = at;
        if (text.substr(at, 1) == "!" && text.substr(at, 2) != "!=") {
            ++at;
            NodePtr inner = unary();
            if (error.empty() && inner->isNumber) fail(from, "'!' negates a condition, not a number");
            auto n = make(Node::Kind::negate, from);
            n->kids = {inner};
            return done(n);
        }
        return compare();
    }
    NodePtr compare()
    {
        NodePtr left = atom();
        if (!error.empty()) return left;
        skip();
        std::string op;
        for (const char* candidate : {"==", "!=", "<=", ">=", "<", ">"}) if (text.substr(at, std::string_view{candidate}.size()) == candidate) { op = candidate; break; }
        if (op.empty()) return left;
        const std::size_t opAt = at;
        at += op.size();
        NodePtr right = atom();
        if (!error.empty()) return left;
        if (left->isNumber != right->isNumber) { fail(opAt, "'" + op + "' compares two numbers or two conditions, not one of each"); return left; }
        if (!left->isNumber && op != "==" && op != "!=") { fail(opAt, "'" + op + "' compares numbers; conditions are compared with == or !="); return left; }
        auto n = make(Node::Kind::compare, left->begin);
        n->op = op;
        n->kids = {left, right};
        return done(n);
    }
    /// `(NAME)` after a path segment; `allowEmpty` for `count()`.
    std::string argument(bool allowEmpty)
    {
        if (!eat("(")) { fail(at, "expected '('"); return {}; }
        const std::size_t from = at;
        std::string name = ident();
        if (name.empty() && !allowEmpty) fail(from, "expected a name inside '()'");
        if (!eat(")")) fail(at, "expected ')'");
        return name;
    }
    NodePtr atom()
    {
        skip();
        const std::size_t from = at;
        if (at >= text.size()) { fail(at, "expected a condition"); return make(Node::Kind::truth, from); }
        if (eat("(")) {
            NodePtr inner = expr();
            if (error.empty() && !eat(")")) fail(at, "expected ')'");
            return inner;
        }
        if (std::isdigit(static_cast<unsigned char>(text[at]))) {
            auto n = make(Node::Kind::number, from);
            n->isNumber = true;
            while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at]))) {
                if (n->literal > 100000000) { fail(from, "the number is too large"); break; }
                n->literal = n->literal * 10 + (text[at++] - '0');
            }
            return done(n);
        }
        const std::string head = ident();
        if (head.empty()) { fail(from, std::string{"unexpected '"} + text[at] + "'"); return make(Node::Kind::truth, from); }
        if (head == "true" || head == "false") { auto n = make(Node::Kind::truth, from); n->literal = head == "true"; return done(n); }
        if (head == "always") return done(make(Node::Kind::always, from));
        const auto member = [&]() -> std::string {
            if (!eat(".")) { fail(at, "expected '.' after '" + head + "'"); return {}; }
            const std::size_t nameAt = at;
            std::string name = ident();
            if (name.empty()) fail(nameAt, "expected a name after '.'");
            return name;
        };
        if (head == "plan") {
            const std::size_t fieldAt = at + 1;
            const std::string field = member();
            if (error.empty() && field != "present") fail(fieldAt, "'plan' has only '.present', not '." + field + "'");
            return done(make(Node::Kind::planPresent, from));
        }
        if (head == "requests") {
            const std::size_t fieldAt = at + 1;
            const std::string field = member();
            if (error.empty() && field != "openBlocking") fail(fieldAt, "'requests' has only '.openBlocking', not '." + field + "'");
            return done(make(Node::Kind::openBlocking, from));
        }
        if (head == "stage") {
            const std::size_t nameAt = at + 1;
            const std::string name = member();
            if (!error.empty()) return make(Node::Kind::truth, from);
            if (std::find(vocabulary.stages.begin(), vocabulary.stages.end(), name) == vocabulary.stages.end()) {
                std::string known;
                for (const auto& s : vocabulary.stages) known += (known.empty() ? "" : ", ") + s;
                fail(nameAt, "'" + name + "' is not an earlier stage (a stage may read only the stages before it: " + (known.empty() ? "none" : known) + ")");
            }
            const std::size_t fieldAt = at + 1;
            const std::string field = member();
            auto n = make(field == "active" ? Node::Kind::stageActive : field == "failed" ? Node::Kind::stageFailed : Node::Kind::stageDone, from);
            if (error.empty() && field != "done" && field != "active" && field != "failed") fail(fieldAt, "a stage has '.done', '.active' and '.failed', not '." + field + "'");
            n->name = name;
            return done(n);
        }
        if (head == "items") {
            const std::size_t fieldAt = at + 1;
            const std::string field = member();
            if (!error.empty()) return make(Node::Kind::truth, from);
            if (field != "all" && field != "any" && field != "count") { fail(fieldAt, "'items' has '.all(STATE)', '.any(STATE)' and '.count(STATE)', not '." + field + "'"); return make(Node::Kind::truth, from); }
            const std::size_t stateAt = at + 1;
            const std::string state = argument(field == "count");
            if (error.empty() && !state.empty() && !isState(state)) fail(stateAt, "'" + state + "' is not an item state (pending, running, accepted, failed)");
            auto n = make(field == "all" ? Node::Kind::itemsAll : field == "any" ? Node::Kind::itemsAny : Node::Kind::itemsCount, from);
            n->isNumber = field == "count";
            n->name = state;
            return done(n);
        }
        if (head == "job") {
            const std::size_t stepAt = at + 1;
            const std::string step = argument(false);
            if (!error.empty()) return make(Node::Kind::truth, from);
            if (std::find(vocabulary.steps.begin(), vocabulary.steps.end(), step) == vocabulary.steps.end()) {
                std::string known;
                for (const auto& s : vocabulary.steps) known += (known.empty() ? "" : ", ") + s;
                fail(stepAt, "'" + step + "' is not the step of a job stage of this process (" + (known.empty() ? "it has none" : known) + ")");
            }
            const std::size_t fieldAt = at + 1;
            const std::string field = member();
            auto n = make(field == "failed" ? Node::Kind::jobFailed : field == "active" ? Node::Kind::jobActive : field == "tries" ? Node::Kind::jobTries : Node::Kind::jobSucceeded, from);
            if (error.empty() && field != "succeeded" && field != "failed" && field != "active" && field != "tries") fail(fieldAt, "a job has '.succeeded', '.failed', '.active' and '.tries', not '." + field + "'");
            n->isNumber = field == "tries";
            n->name = step;
            return done(n);
        }
        fail(from, "unknown name '" + head + "' (the vocabulary is: always, plan.present, stage.NAME.done, items.all(STATE), items.count(STATE), job(STEP).succeeded, requests.openBlocking)");
        return make(Node::Kind::truth, from);
    }
};

std::int64_t value(const Node& n, const Snapshot& s)
{
    using K = Node::Kind;
    const auto stage = [&]() -> Snapshot::StageFacts { const auto it = s.stages.find(n.name); return it == s.stages.end() ? Snapshot::StageFacts{} : it->second; };
    const auto job = [&]() -> Snapshot::JobFacts { const auto it = s.jobs.find(n.name); return it == s.jobs.end() ? Snapshot::JobFacts{} : it->second; };
    const auto count = [&](const std::string& state) { return state == "pending" ? s.pending : state == "running" ? s.running : state == "accepted" ? s.accepted : state == "failed" ? s.failed : s.pending + s.running + s.accepted + s.failed; };
    switch (n.kind) {
        case K::either: return value(*n.kids[0], s) || value(*n.kids[1], s);
        case K::both: return value(*n.kids[0], s) && value(*n.kids[1], s);
        case K::negate: return !value(*n.kids[0], s);
        case K::compare: {
            const std::int64_t a = value(*n.kids[0], s), b = value(*n.kids[1], s);
            return n.op == "==" ? a == b : n.op == "!=" ? a != b : n.op == "<" ? a < b : n.op == "<=" ? a <= b : n.op == ">" ? a > b : a >= b;
        }
        case K::number: case K::truth: return n.literal;
        case K::always: return 1;
        case K::planPresent: return s.planPresent;
        case K::stageDone: return stage().done;
        case K::stageActive: return stage().active;
        case K::stageFailed: return stage().failed;
        case K::itemsAll: return count("") > 0 && count(n.name) == count("");   // `all` over nothing is false: a stage whose items do not exist yet is not done
        case K::itemsAny: return count(n.name) > 0;
        case K::itemsCount: return count(n.name);
        case K::jobSucceeded: return job().succeeded;
        case K::jobFailed: return job().failed;
        case K::jobActive: return job().active;
        case K::jobTries: return job().tries;
        case K::openBlocking: return s.openBlocking;
    }
    return 0;
}

/// The atom that keeps `n` from having the truth value `want`; "" when it has it.
std::string why(const Node& n, bool want, const Snapshot& s, std::string_view text)
{
    using K = Node::Kind;
    if ((value(n, s) != 0) == want) return {};
    switch (n.kind) {
        case K::negate: return why(*n.kids[0], !want, s, text);
        case K::both: case K::either:
            for (const auto& kid : n.kids) if (std::string w = why(*kid, want, s, text); !w.empty()) return w;
            return {};
        default: break;
    }
    const std::string own{text.substr(n.begin, n.end - n.begin)};
    return want ? own : n.kind == K::compare ? "!(" + own + ")" : "!" + own;
}

}  // namespace

std::expected<Predicate, std::string> Predicate::parse(std::string_view text, const Vocabulary& vocabulary)
{
    if (text.size() > 400) return std::unexpected("a predicate is at most 400 characters");
    Parser p{text, vocabulary, 0, {}};
    NodePtr root = p.expr();
    if (p.error.empty()) { p.skip(); if (p.at != text.size()) p.fail(p.at, std::string{"unexpected '"} + text[p.at] + "'"); }
    if (p.error.empty() && root->isNumber) p.fail(0, "a predicate is a condition, not a number (compare it with a literal)");
    if (!p.error.empty()) return std::unexpected(p.error);
    Predicate out;
    out.text_ = std::string{text};
    out.root_ = std::move(root);
    return out;
}

bool Predicate::eval(const Snapshot& snapshot) const { return root_ && value(*root_, snapshot) != 0; }

std::string Predicate::firstFalse(const Snapshot& snapshot) const { return root_ ? why(*root_, true, snapshot, text_) : std::string{"(empty)"}; }

}  // namespace sub0squad
