// The portal's one page: `web/portal.html` (markup and styles) with `web/portal.js` (the script) put where the markup says. Both are files of their own,
// so the script is tested by itself (tests/portal_page_test.js renders it against a recorded state); cmake/EmbedText.cmake compiles them in.
#include "sub0squad/portal.hpp"

#include <string>

namespace sub0squad {

namespace portalweb {
extern const char* const kPageHtml;
extern const char* const kPageScript;
}  // namespace portalweb

const char* portalPage()
{
    static const std::string page = [] {
        std::string html = portalweb::kPageHtml;
        const std::string mark = "__PORTAL_SCRIPT__";
        if (const auto at = html.find(mark); at != std::string::npos) html.replace(at, mark.size(), std::string{"\n"} + portalweb::kPageScript);
        while (!html.empty() && html.back() == '\n') html.pop_back();
        return html;
    }();
    return page.c_str();
}

}  // namespace sub0squad
