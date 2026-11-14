// The self-mending ledger's table and its pure parts (mends.hpp, contract.hpp; catalogue rows 59 to 63), without a model or a daemon. Pinned here:
// the table (registry/mends.json equals the built-in one; every mend's fixture is matched by that mend), the matcher (the FIRST error only, the captured
// name, typographic quotes), the `silence-unused` code edit, and the contract of a file read from the harness's own stub (which is what decides whether an
// undefined name is mechanically mendable). The fixtures undefined-reference.txt, unused-but-set.txt and tests-failed.txt are the gate outputs of the real
// run-v10 (attempts 1, 3 and 6, the workflow path shortened); never-defined.txt is written by hand in GCC's wording.
// That the ENGINE applies a mend at most twice and then lets the ladder go on is in test_workflow; the whole loop through a daemon is in test_heal.
#include "check.hpp"
#include "sub0squad/contract.hpp"
#include "sub0squad/fork.hpp"
#include "sub0squad/harness.hpp"
#include "sub0squad/harness_common.hpp"
#include "sub0squad/mends.hpp"
#include "sub0squad/outline.hpp"
#include "sub0squad/sandbox.hpp"

#include <fstream>
#include <sstream>

using namespace sub0squad;
namespace fs = std::filesystem;

static std::string slurp(const fs::path& p) { std::ifstream in(p, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
static bool contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
static std::string errorOf(const std::string& json) { auto t = parseMends(json); return t ? std::string{"(parsed)"} : t.error(); }
static std::string table(const std::string& mend) { return "{\"schema\":\"mends-1\",\"mends\":[" + mend + "]}"; }

int main()
{
    const fs::path root = SUB0SQUAD_SOURCE_DIR;
    // The table: the file in the registry is the built-in one, byte for byte; it parses; every mend names a fixture that exists and that IT matches.
    CHECK_EQ(slurp(root / "registry" / "mends.json"), builtinMendsJson());
    const auto& mends = builtinMends();
    CHECK_EQ(mends.size(), static_cast<std::size_t>(4));
    for (const Mend& m : mends) {
        const std::string fixture = slurp(root / m.fixture);
        CHECK(!fixture.empty());
        const auto hit = matchMend({m}, fixture);   // two mends may share a pattern (an undefined reference): the gate takes the first whose REQUIREMENT holds
        CHECK(hit.has_value() && hit->name == m.name && hit->action == m.action && hit->maxPerItem == 2);
        if (!hit || hit->name != m.name) std::fprintf(stderr, "mend %s does not match its fixture %s (matched: %s)\n", m.name.c_str(), m.fixture.c_str(), hit ? hit->name.c_str() : "nothing");
    }
    // run-v10 attempt 1: the linker's undefined reference to the contract's entry point. The name is captured without its parameter list.
    {
        const auto m = matchMend(mends, slurp(root / "tests/data/mends/undefined-reference.txt"));
        CHECK(m.has_value());
        if (m) {
            CHECK(m->name == "undefined-contract-symbol" && m->action == "add-section" && m->requires_ == "contract" && m->symbol == "calc::evaluate");
            CHECK(contains(m->error, "undefined reference to 'calc::evaluate("));       // the linker's backtick became a plain quote
            auto back = MendMatch::fromJson(m->toJson());
            CHECK(back.has_value() && back->symbol == m->symbol && back->name == m->name && back->action == m->action && back->error == m->error && back->maxPerItem == 2);
        }
    }
    // run-v10 attempts 3 to 5: one unused-but-set variable, with GCC's typographic quotes; the place is read from the error line.
    {
        const auto m = matchMend(mends, slurp(root / "tests/data/mends/unused-but-set.txt"));
        CHECK(m.has_value());
        if (m) CHECK(m->name == "unused-entity" && m->action == "silence-unused" && m->symbol == "hasExp" && m->file == "src/calc.cpp" && m->line == 321 && m->column == 10);
    }
    // a prototype with no definition: the captured declaration is reduced to the function's name
    {
        const auto m = matchMend(mends, slurp(root / "tests/data/mends/never-defined.txt"));
        CHECK(m.has_value());
        if (m) CHECK(m->name == "helper-never-defined" && m->action == "add-section" && m->requires_ == "prototype" && m->symbol == "parseNumber" && m->line == 14);
    }
    // run-v10 attempt 6: tests fail for semantic reasons. Nothing mechanical: no mend.
    CHECK(!matchMend(mends, slurp(root / "tests/data/mends/tests-failed.txt")).has_value());
    // The FIRST error only: after a syntax error a compiler reports undeclared names that are its own confusion. A mendable text on a LATER line is not matched.
    {
        const std::string later = "GATE FAIL: does not compile: src/calc.cpp\n  src/calc.cpp:9:20: error: missing terminating \" character\n  src/calc.cpp:30:5: error: 'evaluate' was not declared in this scope\n";
        CHECK(!matchMend(mends, later).has_value());
        CHECK(contains(firstErrorLine(later), "missing terminating"));
        const std::string first = "GATE FAIL: does not compile: src/calc.cpp\n  src/calc.cpp:30:5: error: 'evaluate' was not declared in this scope\n  src/calc.cpp:44:1: error: expected '}'\n";
        const auto m = matchMend(mends, first);
        CHECK(m.has_value() && m->name == "undefined-contract-symbol" && m->symbol == "evaluate" && m->line == 30);
        CHECK(firstErrorLine("GATE PASS: dev-engine attempt 1\n").empty() && !matchMend(mends, "").has_value());
        // Python's wording of the same defect, and a parameter
        CHECK(matchMend(mends, "ImportError: cannot import name 'evaluate' from 'calc.engine'\n").value_or(MendMatch{}).symbol == "evaluate");
        const auto p = matchMend(mends, "  src/calc.cpp:7:41: error: unused parameter 'degrees' [-Werror=unused-parameter]\n");
        CHECK(p.has_value() && p->action == "silence-unused" && p->symbol == "degrees" && p->column == 41);
    }
    // Reading a table: what is wrong is named with the mend; a table can add a mend or replace a built-in one, never invent an action.
    {
        const std::string ok = "{\"name\":\"mine\",\"class\":\"M\",\"match\":[\"no member named '(\\\\w+)'\"],\"requires\":\"contract\",\"action\":\"add-section\",\"maxPerItem\":1,\"fixture\":\"x\"}";
        auto mine = parseMends(table(ok));
        CHECK(mine.has_value() && mine->size() == 1 && (*mine)[0].maxPerItem == 1);
        if (mine) {
            const auto merged = mergedMends(mends, *mine);
            CHECK_EQ(merged.size(), mends.size() + 1);
            CHECK(matchMend(merged, "src/a.cpp:3:9: error: no member named 'evaluate' in namespace 'calc'\n").value_or(MendMatch{}).name == "mine");
            Mend replaced = mends[2];
            replaced.maxPerItem = 1;
            CHECK(mergedMends(mends, {replaced}).size() == mends.size() && mergedMends(mends, {replaced})[2].maxPerItem == 1);   // same name: the built-in one is replaced
        }
        CHECK(contains(errorOf("{\"mends\":[]}"), "needs \"schema\": \"mends-1\""));
        CHECK(contains(errorOf("nonsense"), "bad mends table"));
        CHECK(contains(errorOf(table("{\"name\":\"a\",\"match\":[\"x(y)\"],\"requires\":\"contract\",\"action\":\"run-shell\"}")), "mend 'a': action 'run-shell' is not a mend action"));
        CHECK(contains(errorOf(table("{\"name\":\"a\",\"match\":[\"x(y)\"],\"requires\":\"luck\",\"action\":\"add-section\"}")), "requires 'luck' must be contract, declared, prototype or location"));
        CHECK(contains(errorOf(table("{\"name\":\"a\",\"match\":[\"x(y)\"],\"requires\":\"contract\",\"action\":\"silence-unused\"}")), "so it requires location"));
        CHECK(contains(errorOf(table("{\"name\":\"a\",\"match\":[\"x(y\"],\"requires\":\"contract\",\"action\":\"add-section\"}")), "is not a regular expression"));
        CHECK(contains(errorOf(table("{\"name\":\"a\",\"match\":[\"no group\"],\"requires\":\"contract\",\"action\":\"add-section\"}")), "captures no name"));
        CHECK(contains(errorOf(table("{\"name\":\"a\",\"match\":[],\"requires\":\"contract\",\"action\":\"add-section\"}")), "needs at least one pattern"));
        CHECK(contains(errorOf(table("{\"name\":\"a\",\"match\":[\"x(y)\"],\"requires\":\"contract\",\"action\":\"add-section\",\"maxPerItem\":9}")), "maxPerItem must be 1..5"));
        CHECK(contains(errorOf(table("{\"name\":\"Bad Name\",\"match\":[\"x(y)\"],\"requires\":\"contract\",\"action\":\"add-section\"}")), "must be 1 to 40 characters"));
        CHECK(contains(errorOf(table("{\"name\":\"a\",\"match\":[\"x(y)\"],\"requires\":\"contract\",\"action\":\"add-section\",\"shell\":\"rm\"}")), "unknown key 'shell'"));
        CHECK(contains(errorOf(table(ok + "," + ok)), "the name is used twice"));
    }
    // silence-unused: `[[maybe_unused]]` where the name is declared. run-v10's own line first.
    {
        const std::string src = "static double parseNumber(Cursor& c)\n{\n    bool hasExp = false;\n    return 0;\n}\n";
        const auto a = silenceUnused(src, "src/calc.cpp", 3, 10, "hasExp");
        CHECK(a.has_value() && contains(*a, "    [[maybe_unused]] bool hasExp = false;\n") && contains(*a, "static double parseNumber(Cursor& c)\n{\n"));
        if (a) CHECK(!silenceUnused(*a, "src/calc.cpp", 3, 27, "hasExp").has_value());                        // already marked: the same error again is not this mend's
        const auto param = silenceUnused("double evaluate(const std::string& expr, bool degrees)\n{\n", "src/calc.cpp", 1, 47, "degrees");
        CHECK(param.has_value() && contains(*param, "(const std::string& expr, [[maybe_unused]] bool degrees)"));
        const auto firstParam = silenceUnused("double f(std::map<int, int> m, int k)\n", "a.cpp", 1, 29, "m");
        CHECK(firstParam.has_value() && contains(*firstParam, "f([[maybe_unused]] std::map<int, int> m, int k)"));   // the comma inside the template is not a boundary
        const auto loop = silenceUnused("    for (int i = 0; i < n; ++i) {}\n", "a.cpp", 1, 14, "i");
        CHECK(loop.has_value() && contains(*loop, "for ([[maybe_unused]] int i = 0;"));
        const auto wrongColumn = silenceUnused("\tint unused = 1;\n", "a.cpp", 1, 99, "unused");
        CHECK(wrongColumn.has_value() && contains(*wrongColumn, "\t[[maybe_unused]] int unused = 1;"));       // the column is off: the name is found on the line
        CHECK(!silenceUnused("    int a, b;\n", "a.cpp", 1, 12, "b").has_value());                              // a second declarator cannot take the attribute there
        CHECK(!silenceUnused(src, "src/calc.cpp", 3, 10, "other").has_value());                                // the name is not on that line
        CHECK(!silenceUnused(src, "src/calc.cpp", 30, 1, "hasExp").has_value());
        CHECK(!silenceUnused("x = 1\n", "calc/engine.py", 1, 1, "x").has_value());                             // not C or C++: no such mend
        CHECK_EQ(silenceUnused("int x = 1;", "a.cpp", 1, 5, "x").value_or("?"), std::string("[[maybe_unused]] int x = 1;"));   // a file without a final newline keeps none
    }
    // The contract of a file is what the harness's own stub of it defines, with the header's signature (parameter names, no default arguments).
    {
        const Harness* cpp = findHarness("cpp-cmake");
        const auto c = contractOf(cpp, "src/calc.cpp");
        CHECK_EQ(c.size(), static_cast<std::size_t>(1));
        if (c.size() == 1) CHECK(c[0].name == "evaluate" && c[0].scope == "calc" && c[0].signature == "double evaluate(const std::string& expr, bool degrees)");
        CHECK(contractOf(cpp, "src/nothing.cpp").empty() && contractOf(nullptr, "src/calc.cpp").empty());
        const auto py = contractOf(findHarness("py-unittest"), "calc/engine.py");
        CHECK(!py.empty());
        bool evaluate = false;
        for (const auto& f : py) { evaluate = evaluate || f.name == "evaluate"; CHECK(f.name[0] != '_' && f.signature.rfind("def ", 0) == 0 && f.scope.empty()); }
        CHECK(evaluate);
        // a stub with a static helper, a main and two contract functions; the header declares one of them with defaults
        const std::string stub = "#include \"x.hpp\"\nnamespace geo {\nstatic int helper(int)\n{\n    return 0;\n}\ndouble area(double, double)\n{\n    return 0;\n}\nint sides(int)\n{\n    return 0;\n}\n}\nint main()\n{\n    return 0;\n}\n";
        const auto g = contractFunctions("src/geo.cpp", stub, {"namespace geo {\n// area of a rectangle\ndouble area(double width, double height = 1.0);\n}\n"});
        CHECK_EQ(g.size(), static_cast<std::size_t>(2));
        if (g.size() == 2) CHECK(g[0].name == "area" && g[0].signature == "double area(double width, double height)" && g[0].scope == "geo" && g[1].name == "sides" && g[1].signature == "int sides(int)");
        // the skeleton the model declared, without the contract function: the harness declares it inside the namespace; a wrong signature is replaced
        const std::string declared = "#include \"x.hpp\"\n\nnamespace geo {\n\n@@ static double half(double x)\n\n}  // namespace geo\n";
        const std::string with = withContractDeclarations(declared, "src/geo.cpp", g);
        CHECK(contains(with, "@@ static double half(double x)\n") && contains(with, "@@ double area(double width, double height)\n@@ int sides(int)\n}  // namespace geo\n"));
        const std::string wrong = withContractDeclarations("namespace geo {\n@@ float area(float w)\n}\n", "src/geo.cpp", {g[0]});
        CHECK(contains(wrong, "@@ double area(double width, double height)\n") && !contains(wrong, "float"));
        CHECK(contains(withContractDeclarations("@@ static int f()\n", "src/geo.cpp", {g[0]}), "@@ double geo::area(double width, double height)\n"));   // no namespace opened: qualified
        // which contract functions a file lacks; adding one as a stub makes it defined, keeps the rest, and puts restored functions back before it
        const std::string file = "#include \"x.hpp\"\n\nnamespace geo {\n\nint sides(int n)\n{\n    return n;\n}\n\n}  // namespace geo\n";
        const auto missing = undefinedContract(file, "src/geo.cpp", g);
        CHECK(missing.size() == 1 && missing[0].name == "area");
        const std::string added = withStubFunction(file, "src/geo.cpp", g[0].signature, g[0].scope, {"static double half(double x)\n{\n    return x / 2;\n}\n"});
        CHECK(undefinedContract(added, "src/geo.cpp", g).empty());
        CHECK(contains(added, "not implemented: area") && contains(added, "int sides(int n)\n{\n    return n;\n}\n") && contains(added, "#include <stdexcept>"));
        CHECK(added.find("static double half") < added.find("double area(double width, double height)\n") && added.find("not implemented: area") < added.find("}  // namespace geo"));
        const std::string pyAdded = withStubFunction("import math\n\n\ndef other(x):\n    return x\n", "pkg/m.py", "def evaluate(expr, degrees=False)", "");
        CHECK(contains(pyAdded, "def other(x):\n    return x\n") && contains(pyAdded, "def evaluate(expr, degrees=False):\n    raise NotImplementedError(\"not implemented: evaluate\")"));
        // a prototype in the file is the signature of a helper that was declared and never defined
        const std::string protos = "static double parseNumber(Cursor& c);\nstatic int other();\n\nstatic int other()\n{\n    return parseNumber(c);\n}\n";
        CHECK_EQ(prototypeOf(protos, "parseNumber"), std::string("static double parseNumber(Cursor& c)"));
        CHECK(prototypeOf(protos, "missing").empty() && prototypeOf("    return parseNumber(c);\n    parseNumber(c);\n", "parseNumber").empty());
    }
    // Row 71, from run-v11's real files: a class that declares member functions and defines none. Every declared member is found with the signature its
    // out-of-class definition has; the skeleton gets one `@@` line each, which the harness expands to stubs named with their class, so a member
    // `Parser::evaluate` and the contract's free `evaluate` are two sections; the real link error names a member, and the mend's pattern captures it.
    {
        const std::string real = slurp(root / "tests/data/mends/member-skeleton.cpp");   // run-v11 attempt 7, as the gate saw it
        const auto members = memberDeclarations(real, "src/calc.cpp");
        CHECK_EQ(members.size(), static_cast<std::size_t>(18));
        const auto sig = [&](const std::string& q) { for (const auto& m : members) if (m.qualified() == q) return m.signature + " @" + m.scope; return std::string{}; };
        CHECK_EQ(sig("Parser::Parser"), std::string("Parser::Parser(const std::string& expr, bool degrees) @calc"));
        CHECK_EQ(sig("Parser::parse"), std::string("double Parser::parse() @calc"));
        CHECK_EQ(sig("Parser::isEnd"), std::string("bool Parser::isEnd() const @calc"));
        CHECK_EQ(sig("Parser::evaluateFunction"), std::string("double Parser::evaluateFunction(const std::string& name, const std::vector<double>& args) @calc"));
        CHECK_EQ(undefinedMembers(real, "src/calc.cpp").size(), members.size());          // none is defined: `evaluate` is, and it is not a member
        CHECK(overloadedMembers(real, "src/calc.cpp").empty());
        CHECK(memberDeclarations("class A:\n    def f(self): pass\n", "a.py").empty());      // C and C++ only
        // what does not count: a data member, a defaulted or deleted or pure function, an operator, a member defined in the class, a std::function field
        const std::string odd = "struct S {\n    int n = f(2);\n    S() = default;\n    S(const S&) = delete;\n    virtual double area() const = 0;\n    bool operator==(const S& o) const;\n"
                                "    int twice(int x) { return 2 * x; }\n    std::function<double(double)> fn;\n    virtual int size() const override;\n    static S make(int n = 3);\n    int size(int scale);\n};\n";
        const auto oddMembers = memberDeclarations(odd, "s.hpp");
        CHECK_EQ(oddMembers.size(), static_cast<std::size_t>(3));
        if (oddMembers.size() == 3) { CHECK_EQ(oddMembers[0].signature, std::string("int S::size() const")); CHECK_EQ(oddMembers[1].signature, std::string("S S::make(int n)")); }
        CHECK(overloadedMembers(odd, "s.hpp") == std::vector<std::string>{"S::size"});       // two sections cannot share a name: the skeleton check refuses it
        // The skeleton as the model declared it (attempt 3: the class has its own `evaluate`), completed by the harness.
        const std::string declared = slurp(root / "tests/data/mends/member-skeleton-evaluate.cpp");
        const std::string start = declared.substr(0, declared.find("double evaluate(const std::string& expr, bool degrees);")) + "@@ double evaluate(const std::string& expr, bool degrees)\n\n}  // namespace calc\n";
        const std::string withMembers = withMemberDeclarations(start, "src/calc.cpp");
        CHECK(contains(withMembers, "@@ Parser::Parser(const std::string& expr, bool degrees)\n") && contains(withMembers, "@@ double Parser::evaluate()\n") && contains(withMembers, "@@ double Parser::toRad(double x)\n"));
        CHECK(withMembers.find("@@ double Parser::evaluate()") < withMembers.rfind("}  // namespace calc"));     // inside the namespace the class is in
        CHECK(undefinedMembers(withMembers, "src/calc.cpp").empty());                           // an `@@` line counts: the harness will define it
        CHECK_EQ(withMemberDeclarations(withMembers, "src/calc.cpp"), withMembers);             // and it is added once
        const Skeleton sk = expandSkeleton(withMembers, "src/calc.cpp");
        CHECK(sk.error.empty());                                                               // `Parser::evaluate` and `evaluate` are not "declared twice"
        CHECK(contains(sk.text, "double Parser::evaluate()\n{\n    throw std::logic_error(\"not implemented: Parser::evaluate\");\n}\n"));
        CHECK(contains(sk.text, "Parser::Parser(const std::string& expr, bool degrees)\n{\n    (void)expr;\n    (void)degrees;\n    throw std::logic_error(\"not implemented: Parser::Parser\");\n}\n"));
        CHECK(contains(sk.text, "not implemented: evaluate\"") && undefinedMembers(sk.text, "src/calc.cpp").empty());
        // the real gate text of that attempt
        const std::string gate = slurp(root / "tests/data/mends/undefined-member.txt");
        const auto all = matchMend(mends, gate);
        CHECK(all && all->name == "undefined-contract-symbol" && all->symbol == "calc::Parser::Parser");   // by pattern alone it is the first mend; its requirement (a CONTRACT function) does not hold ...
        const auto hit = matchMend({mends[1]}, gate);
        CHECK(hit && hit->name == "undefined-declared-member" && hit->requires_ == "declared" && hit->action == "add-section" && hit->symbol == "calc::Parser::Parser");   // ... and the gate goes on to this one
    }
    // Rows 25/26 and 70, from run-v11's real near pass: the failing tests are found by name and quoted as they are written.
    {
        const std::string gate = slurp(root / "tests/data/mends/near-pass.txt");
        CHECK(gate.rfind("GATE FAIL: 2 of 22 tests failed", 0) == 0);
        const std::string tests = "#include \"helpers/check.hpp\"\nstatic double ev(const char* e) { return calc::evaluate(e); }\n\nTEST(basic_addition) { CHECK_NEAR(ev(\"1+2\"), 3, 1e-9); }\n"
                                  "TEST(basic_power_right_associativity) {\n    CHECK_NEAR(ev(\"2^3^2\"), 512, 1e-9);\n}\n\nTEST(precedence_and_unary_minus)\n{\n    CHECK_NEAR(ev(\"-2^2\"), -4, 1e-9);\n    CHECK_NEAR(ev(\"2+3*4\"), 14, 1e-9);\n}\n";
        const std::string quoted = failingTestSources(gate, {{"tests/eval.test.cpp", tests}});
        CHECK(quoted.rfind("The failing tests as they are written:\n--- tests/eval.test.cpp, basic_power_right_associativity\nTEST(basic_power_right_associativity) {\n    CHECK_NEAR(ev(\"2^3^2\"), 512, 1e-9);\n}\n", 0) == 0);
        CHECK(contains(quoted, "--- tests/eval.test.cpp, precedence_and_unary_minus\nTEST(precedence_and_unary_minus)\n{\n    CHECK_NEAR(ev(\"-2^2\"), -4, 1e-9);\n    CHECK_NEAR(ev(\"2+3*4\"), 14, 1e-9);\n}\n"));
        CHECK(!contains(quoted, "basic_addition"));                                           // only the tests that fail
        CHECK(failingTestSources("GATE FAIL: does not compile: src/calc.cpp\n", {{"tests/eval.test.cpp", tests}}).empty());
        const std::string py = "def test_add():\n    assert ev('1+2') == 3\n\ndef test_power():\n    assert ev('2^3^2') == 512\n    assert ev('2^0') == 1\n\ndef test_last():\n    pass\n";
        CHECK_EQ(failingTestSources("GATE FAIL: 1 of 3 tests failed\nnot ok 2 - test_power\n", {{"tests/test_calc.py", py}}), std::string("The failing tests as they are written:\n--- tests/test_calc.py, test_power\ndef test_power():\n    assert ev('2^3^2') == 512\n    assert ev('2^0') == 1\n\n"));
        // a constructor's member-initializer list is the section's to write: the declaration is the skeleton's, the list is kept
        CHECK_EQ(applySectionSignature("Parser::Parser(const std::string& e, bool d) : expr_(e), pos_{0}, degrees_(d)\n{\n}\n", "Parser::Parser(const std::string& expr, bool degrees)", "", "src/calc.cpp"),
                 std::string("Parser::Parser(const std::string& expr, bool degrees)\n    : expr_(e), pos_{0}, degrees_(d)\n{\n}\n"));
        // every failed attempt says why (row 72): the record round-trips, quotes and newlines included, and is gone when cleared
        const fs::path dir = makeTempDir("why");
        harnessutil::recordAttemptFailure(dir, "agent", "format", "no reply was kept: 6 call(s)\nthe last refusal: too many arguments to function 'app::Result app::run(const std::string&)' \"x\"");
        CHECK_EQ(harnessutil::attemptFailureJson(dir), std::string("{\"schema\":\"attempt-failure-1\",\"role\":\"agent\",\"kind\":\"format\",\"reason\":\"no reply was kept: 6 call(s)\\nthe last refusal: too many arguments to function 'app::Result app::run(const std::string&)' \\\"x\\\"\"}"));
        harnessutil::clearAttemptFailure(dir);
        CHECK(harnessutil::attemptFailureJson(dir).empty());
        removeTree(dir);
    }
    // `mend-apply`, the job the engine starts for `silence-unused`: attempt N is attempt N-1's output with the one edit, everything else kept; a place the
    // harness cannot mend fails the job (the ladder then decides), and nothing but silence-unused is applied without a model.
    {
        const fs::path d = makeTempDir("mend-apply");
        const fs::path before = d / "items" / "dev" / "attempt-3" / "out";
        fs::create_directories(before / "src");
        { std::ofstream(before / "src" / "calc.cpp") << "static double parseNumber()\n{\n    bool hasExp = false;\n    return 0;\n}\n"; std::ofstream(before / "src" / "other.cpp") << "int other;\n"; }
        { std::ofstream(d / "items" / "dev" / "attempt-3" / "fork.json") << "{\"failure\":\"compile\"}\n"; }
        const std::vector<std::string> base = {"--workflow-dir", d.string(), "--item", "dev", "--attempt", "4", "--file", "src/calc.cpp", "--symbol", "hasExp"};
        const auto with = [&](std::vector<std::string> extra) { auto a = base; a.insert(a.end(), extra.begin(), extra.end()); return a; };
        CHECK_EQ(runMendApply(with({"--action", "silence-unused", "--line", "3", "--column", "10"})), 0);
        const fs::path now = d / "items" / "dev" / "attempt-4";
        CHECK_EQ(slurp(now / "out" / "src" / "calc.cpp"), std::string("static double parseNumber()\n{\n    [[maybe_unused]] bool hasExp = false;\n    return 0;\n}\n"));
        CHECK_EQ(slurp(now / "out" / "src" / "other.cpp"), std::string("int other;\n"));                       // the rest of the attempt is carried over untouched
        CHECK(fs::exists(now / "fork.json") && contains(slurp(now / "mend.txt"), "silence-unused: `hasExp` at src/calc.cpp:3"));
        CHECK_EQ(slurp(before / "src" / "calc.cpp").find("maybe_unused"), std::string::npos);                    // the earlier attempt is evidence: not edited
        CHECK_EQ(runMendApply(with({"--action", "silence-unused", "--line", "4", "--column", "1"})), 1);        // the name is not on that line
        CHECK_EQ(runMendApply(with({"--action", "add-section", "--line", "3", "--column", "10"})), 64);         // needs a model: not this command's
        CHECK_EQ(runMendApply({"--workflow-dir", d.string(), "--item", "dev", "--attempt", "4", "--action", "silence-unused", "--file", "../../etc/passwd", "--symbol", "x", "--line", "1"}), 64);
        removeTree(d);
    }
    return finish();
}
