// Files (platform_file.hpp, docs/PLATFORM.md): whole reads and writes in binary mode, atomic replacement, appends with permissions, sync and
// truncate, modification times, and whole writes and reads on a descriptor.
#include "check.hpp"
#include "sub0squad/platform_file.hpp"

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>   // _pipe(): the test's own fixture
#else
#include <unistd.h>   // pipe(): the test's own fixture
#endif

#include <string>

using namespace sub0squad::platform;
namespace fs = std::filesystem;

int main()
{
    const auto dir = makeTempDir("platform-file");
    {   // whole files: bytes kept exactly (CR, NUL), parents created, a missing file is nullopt or ""
        const std::string bytes = std::string("line one\r\nline two\n") + '\0' + "after nul";
        CHECK(writeFile(dir / "a" / "b" / "data.bin", bytes).has_value());
        CHECK(readFile(dir / "a" / "b" / "data.bin") == bytes);
        CHECK(!readFile(dir / "missing").has_value());
        CHECK(readFileText(dir / "missing").empty());
        CHECK(writeFile(dir / "a" / "b" / "data.bin", "short").has_value());   // truncated, not overwritten in place
        CHECK(readFileText(dir / "a" / "b" / "data.bin") == "short");
        CHECK(!writeFile(dir / "a" / "b" / "data.bin" / "under-a-file", "x").has_value());
    }
    {   // atomic replacement leaves no temporary behind
        CHECK(writeFileAtomic(dir / "atomic.json", "{\"v\":1}").has_value());
        CHECK(writeFileAtomic(dir / "atomic.json", "{\"v\":2}").has_value());
        CHECK(readFileText(dir / "atomic.json") == "{\"v\":2}");
        CHECK(!fs::exists(dir / "atomic.json.tmp"));
    }
    {   // appends: created with the owner-only default, every write at the end, sync and truncate
        auto f = AppendFile::open(dir / "log.ndjson");
        CHECK(f.has_value() && f->isOpen());
        if (f) {
            CHECK(f->append("one\n").has_value());
            CHECK(f->append("two\n").has_value());
            CHECK(f->sync().has_value());
            CHECK(readFileText(dir / "log.ndjson") == "one\ntwo\n");
            CHECK(f->truncate(4).has_value());
            CHECK(f->append("three\n").has_value());   // O_APPEND: after the cut, not at the old offset
            CHECK(readFileText(dir / "log.ndjson") == "one\nthree\n");
            AppendFile moved = std::move(*f);
            CHECK(moved.isOpen() && !f->isOpen());
            CHECK(!f->append("x").has_value());
        }
        const auto perms = fs::status(dir / "log.ndjson").permissions();
#if defined(_WIN32)
        CHECK((perms & fs::perms::owner_write) != fs::perms::none);   // Windows keeps only read-only or not
#else
        CHECK((perms & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
#endif
        CHECK(appendToFile(dir / "log.ndjson", "four\n").has_value());
        CHECK(readFileText(dir / "log.ndjson") == "one\nthree\nfour\n");
        CHECK(!AppendFile::open(dir / "no" / "such" / "dir" / "f").has_value());
    }
    {   // the modification time moves with a write
        const auto t = modifiedSeconds(dir / "log.ndjson");
        CHECK(t.has_value() && *t > 1.6e9);
        CHECK(!modifiedSeconds(dir / "missing").has_value());
    }
    {   // a descriptor: everything written arrives; end of stream is 0
        int fds[2];
#if defined(_WIN32)
        CHECK(::_pipe(fds, 4096, _O_BINARY) == 0);
        CHECK(writeAll(fds[1], "frame").has_value());
        ::_close(fds[1]);
#else
        CHECK(::pipe(fds) == 0);
        CHECK(writeAll(fds[1], "frame").has_value());
        ::close(fds[1]);
#endif
        char buf[16];
        std::string got;
        for (;;) {
            auto n = readSome(fds[0], buf, sizeof buf);
            CHECK(n.has_value());
            if (!n || *n == 0) break;
            got.append(buf, *n);
        }
#if defined(_WIN32)
        ::_close(fds[0]);
#else
        ::close(fds[0]);
#endif
        CHECK(got == "frame");
        CHECK(!writeAll(-1, "x").has_value());
    }
    ::removeTree(dir);   // check.hpp's, which retries; platform::removeTree is also in scope
    return finish();
}
