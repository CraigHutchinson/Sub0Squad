#include "check.hpp"
#include "sub0squad/journal.hpp"

#include <fstream>
#include <sstream>

using namespace sub0squad;
namespace fs = std::filesystem;

static std::string slurp(const fs::path& p) { std::ifstream in(p, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }

int main()
{
    const fs::path dir = makeTempDir("journal");
    {
        auto j = Journal::open(dir);
        CHECK(j.has_value());
        CHECK(j->append("a", "{\"x\":1}").has_value());
        CHECK(j->append("b", "{\"s\":\"q\\\"uote\\n\"}").has_value());
        CHECK(j->append("c", "{}", false).has_value());
    }
    auto r = Journal::replay(dir);
    CHECK(r.has_value());
    CHECK_EQ(r->records.size(), 3u);
    CHECK_EQ(r->records[1].type, std::string{"b"});
    CHECK(!r->tornTail);

    // Truncation fuzz: cut the file at every byte; replay must never fail and must return a prefix of the records.
    const std::string full = slurp(dir / "journal-000001.ndjson");
    for (std::size_t cut = 0; cut <= full.size(); ++cut) {
        const fs::path d2 = makeTempDir("jcut");
        { std::ofstream o(d2 / "journal-000001.ndjson", std::ios::binary); o.write(full.data(), static_cast<std::streamsize>(cut)); }
        auto rr = Journal::replay(d2);
        CHECK(rr.has_value());
        if (rr) {
            CHECK(rr->records.size() <= 3u);
            for (std::size_t i = 0; i < rr->records.size(); ++i) CHECK_EQ(rr->records[i].seq, i + 1);
            // Reopening truncates the torn tail; a new append continues the sequence and replays cleanly.
            auto j2 = Journal::open(d2);
            CHECK(j2.has_value());
            if (j2) {
                CHECK(j2->append("n", "{}").has_value());
                auto r3 = Journal::replay(d2);
                CHECK(r3.has_value() && !r3->tornTail && r3->records.size() == rr->records.size() + 1);
            }
        }
        removeTree(d2);
    }

    // Corruption in the middle is reported, not silently skipped.
    {
        const fs::path d3 = makeTempDir("jbad");
        std::string bad = full;
        bad[bad.find('\n') / 2] = '#';
        { std::ofstream o(d3 / "journal-000001.ndjson", std::ios::binary); o << bad; }
        CHECK(!Journal::replay(d3).has_value());
        removeTree(d3);
    }
    removeTree(dir);
    return finish();
}
