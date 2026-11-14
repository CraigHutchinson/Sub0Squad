// What is read back from results: the failure excerpt a worker is shown, and the quality findings over sources.
#include "workflow_sim.hpp"
#include <sstream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "sub0squad/harness.hpp"
#include "sub0squad/harness_web.hpp"
#include "sub0squad/briefing.hpp"
#include "sub0squad/canvas.hpp"
#include "sub0squad/division.hpp"
#include "sub0squad/edits.hpp"
#include "sub0squad/outline.hpp"
#include "sub0squad/overseer.hpp"
#include "sub0squad/qa.hpp"
#include "sub0squad/recall.hpp"
#include "sub0squad/trace.hpp"
#include "sub0squad/triage.hpp"
#include "sub0squad/mends.hpp"
#include "sub0squad/workflow.hpp"

int main()
{
    { // failure detail: expected and actual values reach the worker
        const std::string tap = "TAP version 13\nnot ok 1 - adds\n  ---\n  location: '/x/a.test.mjs:2:1'\n  error: |-\n    Expected values to be strictly equal:\n  code: 'ERR_ASSERTION'\n  expected: 3\n  actual: 2\n  ...\n# tests 1\n# pass 0\n# fail 1\n";
        const std::string ex = webFailureExcerpt(tap);
        CHECK(ex.find("FAIL 1 - adds") != std::string::npos || ex.find("FAIL adds") != std::string::npos);
        CHECK(ex.find("expected 3 but the code gave 2") != std::string::npos);
        CHECK(ex.find("a.test.mjs:2:1") != std::string::npos);
    }
    { // QA: two modules that each wrote the same helper and the same block; a long function; tests and node_modules are left alone.
        const auto root = std::filesystem::current_path() / "qa-test";
        removeTree(root);
        std::filesystem::create_directories(root / "src");
        std::filesystem::create_directories(root / "tests");
        const std::string shared = "function isDigit(c) {\n  return c >= '0' && c <= '9';\n}\nfunction readNumber(text, start) {\n  let end = start;\n  while (end < text.length && isDigit(text[end])) end += 1;\n  const value = Number(text.slice(start, end));\n  return { value, end };\n}\n";
        { std::ofstream o(root / "src" / "lexer.js"); o << shared << "export function tokenize(text) {\n  return [];\n}\n"; }
        { std::ofstream o(root / "src" / "functions.js"); o << "export function parseArg(text) {\n  return null;\n}\n" << shared; }
        { std::ofstream o(root / "src" / "big.js"); o << "export function big() {\n"; for (int i = 0; i < 45; ++i) o << "  const v" << i << " = compute(" << i << ", 'distinct text number " << i << "');\n"; o << "  return 0;\n}\n"; }
        { std::ofstream o(root / "tests" / "dup.test.mjs"); o << shared; }
        const auto f = qaScan(root);
        bool dupHelper = false, dupBlock = false, longFn = false, testsFlagged = false;
        for (const auto& x : f) {
            if (x.axis == "duplication" && x.what.find("'isDigit' is declared in 2 files") != std::string::npos) dupHelper = true;
            if (x.axis == "duplication" && x.what.find("lines appear twice") != std::string::npos) dupBlock = true;
            if (x.axis == "simplicity" && x.what.find("'big' is 48 lines long") != std::string::npos) longFn = true;
            for (const auto& w : x.where) if (w.find("tests/") != std::string::npos) testsFlagged = true;
        }
        CHECK(dupHelper && dupBlock && longFn && !testsFlagged);
        removeTree(root);
    }
    return finish();
}
