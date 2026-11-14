// The portal's units that need no server (src/portal_impl.hpp): the page as it is embedded, and the read-only file view's path rules. The page's script
// is rendered by test_portal_page (tests/portal_page_test.js, with node); the server's routes run in test_readme (`up` starts a portal).
#include "check.hpp"
#include "portal_impl.hpp"

#include <fstream>

using namespace sub0squad;
using namespace sub0squad::portal;

static void put(const fs::path& p, const std::string& text) { fs::create_directories(p.parent_path()); std::ofstream(p) << text; }

int main()
{
    {   // The page: the markup with the script in its place, nothing left of the build's placeholder, the token placeholder the server fills still there.
        const std::string page = portalPage();
        CHECK(page.rfind("<!doctype html>", 0) == 0 && page.size() > 40000);
        CHECK(page.find("__PORTAL_SCRIPT__") == std::string::npos);
        CHECK(page.find("<script>\nconst $=id=>document.getElementById(id);") != std::string::npos);
        CHECK(page.find("const TOKEN='__PORTAL_TOKEN__';") != std::string::npos);
        CHECK(page.find("tickState();tickLive();") != std::string::npos);
        CHECK(page.size() >= 23 && page.compare(page.size() - 23, 23, "</script></body></html>") == 0);
        CHECK(page.find("lastFailure") != std::string::npos && page.find("healed") != std::string::npos);   // what the status says about an item is on the page
    }
    {   // Query strings.
        CHECK_EQ(urlDecode("a%20b+c%2Fd%zz"), std::string("a b c/d%zz"));
        CHECK_EQ(queryParam("/api/file?wf=w-000001&p=items%2Fa%2Fgate.txt", "p"), std::string("items/a/gate.txt"));
        CHECK_EQ(queryParam("/api/file?wf=w-000001&p=x", "wf"), std::string("w-000001"));
        CHECK(queryParam("/api/file?wf=w-000001", "p").empty() && queryParam("/api/file", "p").empty());
        CHECK_EQ(std::string(mimeOf("a/b.mjs")), std::string("text/javascript; charset=utf-8"));
        CHECK_EQ(std::string(mimeOf("gate.txt")), std::string("text/plain; charset=utf-8"));
        CHECK_EQ(std::string(mimeOf("noext")), std::string("text/plain; charset=utf-8"));
    }
    {   // The file view never leaves the sandbox: no absolute path, no `..`, no symbolic link that points outside; a job's files only by their exact names.
        const fs::path home = fs::temp_directory_path() / ("s0q-portal-" + std::to_string(::getpid()));
        const fs::path root = home / "state" / "workflows" / "w-000001";
        put(root / "items" / "a" / "gate.txt", "GATE PASS\n");
        put(root / "items" / "a" / "attempt-1" / "ws" / "copy.txt", "x");
        put(home / "secret.txt", "no");
        std::error_code ec;
        fs::create_symlink(home / "secret.txt", root / "link.txt", ec);
        fs::path out;
        CHECK(insideSandbox(root, "items/a/gate.txt", out) && out.filename() == "gate.txt");
        CHECK(!insideSandbox(root, "/etc/passwd", out) && !insideSandbox(root, "../../secret.txt", out) && !insideSandbox(root, "items/../../x", out) && !insideSandbox(root, "", out));
        CHECK(!ec && !insideSandbox(root, "link.txt", out));
        CHECK(jobFilePath(root, "@jobs/j-000006/run-1/stdout.log", out) && out.string().find("/state/jobs/j-000006/run-1/stdout.log") != std::string::npos);
        CHECK(!jobFilePath(root, "@jobs/j-000006/run-1/other.txt", out) && !jobFilePath(root, "@jobs/../run-1/stdout.log", out) && !jobFilePath(root, "@jobs/j-6x/run-1/stdout.log", out) && !jobFilePath(root, "items/a/gate.txt", out));
        const std::string listing = filesJson(root);
        CHECK(listing.find("{\"p\":\"items/a/gate.txt\",\"s\":10}") != std::string::npos && listing.find("copy.txt") == std::string::npos && listing.find("\"deliverables\":null") != std::string::npos);
        fs::remove_all(home, ec);
    }
    return finish();
}
