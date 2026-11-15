#include "wc/wc.hpp"

#include <cctype>

namespace wc {

std::size_t countWords(const std::string& text)
{
    std::size_t words = 0;
    bool inWord = false;
    for (const char c : text) {
        const bool space = std::isspace(static_cast<unsigned char>(c)) != 0;
        if (!space && !inWord) ++words;
        inWord = !space;
    }
    return words;
}

}  // namespace wc
