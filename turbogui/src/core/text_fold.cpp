// FC 27 LE Turbo GUI - accent-agnostic search text (see text_fold.h)
#include "core/text_fold.h"

#include <cstdint>

namespace turbo {

namespace {

// The ASCII text of each code point; nullptr = kept as it is. Generated from Unicode NFKD (Python unicodedata) with the
// letters it does not decompose added by hand (ł ø đ ħ ı ŧ ß æ œ þ ð ĳ ŀ ŉ ŋ ſ and the Latin Extended-B letters with a stroke
// or hook), lower case.
static const char* const kLatin[0x250 - 0xC0] = {
    /* U+00C0 */ "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    /* U+00D0 */ "d", "n", "o", "o", "o", "o", "o", nullptr, "o", "u", "u", "u", "u", "y", "th", "ss",
    /* U+00E0 */ "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    /* U+00F0 */ "d", "n", "o", "o", "o", "o", "o", nullptr, "o", "u", "u", "u", "u", "y", "th", "y",
    /* U+0100 */ "a", "a", "a", "a", "a", "a", "c", "c", "c", "c", "c", "c", "c", "c", "d", "d",
    /* U+0110 */ "d", "d", "e", "e", "e", "e", "e", "e", "e", "e", "e", "e", "g", "g", "g", "g",
    /* U+0120 */ "g", "g", "g", "g", "h", "h", "h", "h", "i", "i", "i", "i", "i", "i", "i", "i",
    /* U+0130 */ "i", "i", "ij", "ij", "j", "j", "k", "k", "k", "l", "l", "l", "l", "l", "l", "l",
    /* U+0140 */ "l", "l", "l", "n", "n", "n", "n", "n", "n", "n", "n", "n", "o", "o", "o", "o",
    /* U+0150 */ "o", "o", "oe", "oe", "r", "r", "r", "r", "r", "r", "s", "s", "s", "s", "s", "s",
    /* U+0160 */ "s", "s", "t", "t", "t", "t", "t", "t", "u", "u", "u", "u", "u", "u", "u", "u",
    /* U+0170 */ "u", "u", "u", "u", "w", "w", "y", "y", "y", "z", "z", "z", "z", "z", "z", "s",
    /* U+0180 */ "b", "b", "b", "b", "b", "b", "o", "c", "c", "d", "d", "d", "d", nullptr, "e", "e",
    /* U+0190 */ "e", "f", "f", "g", "g", "hv", "i", "i", "k", "k", "l", "l", "m", "n", "n", "o",
    /* U+01A0 */ "o", "o", "oi", "oi", "p", "p", "r", nullptr, nullptr, nullptr, nullptr, "t", "t", "t", "t", "u",
    /* U+01B0 */ "u", "u", "v", "y", "y", "z", "z", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* U+01C0 */ nullptr, nullptr, nullptr, nullptr, "dz", "dz", "dz", "lj", "lj", "lj", "nj", "nj", "nj", "a", "a", "i",
    /* U+01D0 */ "i", "o", "o", "u", "u", "u", "u", "u", "u", "u", "u", "u", "u", "e", "a", "a",
    /* U+01E0 */ "a", "a", "ae", "ae", "g", "g", "g", "g", "k", "k", "o", "o", "o", "o", nullptr, nullptr,
    /* U+01F0 */ "j", "dz", "dz", "dz", "g", "g", "hv", "w", "n", "n", "a", "a", "ae", "ae", "o", "o",
    /* U+0200 */ "a", "a", "a", "a", "e", "e", "e", "e", "i", "i", "i", "i", "o", "o", "o", "o",
    /* U+0210 */ "r", "r", "r", "r", "u", "u", "u", "u", "s", "s", "t", "t", "y", "y", "h", "h",
    /* U+0220 */ "n", "d", "ou", "ou", "z", "z", "a", "a", "e", "e", "o", "o", "o", "o", "o", "o",
    /* U+0230 */ "o", "o", "y", "y", "l", "n", "t", "j", "db", "qp", "a", "c", "c", "l", "t", "s",
    /* U+0240 */ "z", nullptr, nullptr, "b", "u", nullptr, "e", "e", "j", "j", "q", "q", "r", "r", "y", "y",
};
static const char* const kLatinAdditional[0x1F00 - 0x1E00] = {
    /* U+1E00 */ "a", "a", "b", "b", "b", "b", "b", "b", "c", "c", "d", "d", "d", "d", "d", "d",
    /* U+1E10 */ "d", "d", "d", "d", "e", "e", "e", "e", "e", "e", "e", "e", "e", "e", "f", "f",
    /* U+1E20 */ "g", "g", "h", "h", "h", "h", "h", "h", "h", "h", "h", "h", "i", "i", "i", "i",
    /* U+1E30 */ "k", "k", "k", "k", "k", "k", "l", "l", "l", "l", "l", "l", "l", "l", "m", "m",
    /* U+1E40 */ "m", "m", "m", "m", "n", "n", "n", "n", "n", "n", "n", "n", "o", "o", "o", "o",
    /* U+1E50 */ "o", "o", "o", "o", "p", "p", "p", "p", "r", "r", "r", "r", "r", "r", "r", "r",
    /* U+1E60 */ "s", "s", "s", "s", "s", "s", "s", "s", "s", "s", "t", "t", "t", "t", "t", "t",
    /* U+1E70 */ "t", "t", "u", "u", "u", "u", "u", "u", "u", "u", "u", "u", "v", "v", "v", "v",
    /* U+1E80 */ "w", "w", "w", "w", "w", "w", "w", "w", "w", "w", "x", "x", "x", "x", "y", "y",
    /* U+1E90 */ "z", "z", "z", "z", "z", "z", "h", "t", "w", "y", nullptr, "s", nullptr, nullptr, "ss", nullptr,
    /* U+1EA0 */ "a", "a", "a", "a", "a", "a", "a", "a", "a", "a", "a", "a", "a", "a", "a", "a",
    /* U+1EB0 */ "a", "a", "a", "a", "a", "a", "a", "a", "e", "e", "e", "e", "e", "e", "e", "e",
    /* U+1EC0 */ "e", "e", "e", "e", "e", "e", "e", "e", "i", "i", "i", "i", "o", "o", "o", "o",
    /* U+1ED0 */ "o", "o", "o", "o", "o", "o", "o", "o", "o", "o", "o", "o", "o", "o", "o", "o",
    /* U+1EE0 */ "o", "o", "o", "o", "u", "u", "u", "u", "u", "u", "u", "u", "u", "u", "u", "u",
    /* U+1EF0 */ "u", "u", "y", "y", "y", "y", "y", "y", "y", "y", "ll", "ll", nullptr, nullptr, nullptr, nullptr,
};

// One UTF-8 sequence at s[i]: its code point and length; 0 length = not a valid sequence (the byte is copied as it is)
size_t decode(std::string_view s, size_t i, uint32_t& cp) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    auto cont = [&](size_t k) { return i + k < s.size() && (static_cast<unsigned char>(s[i + k]) & 0xC0u) == 0x80u; };
    if (c < 0x80) { cp = c; return 1; }
    if ((c & 0xE0u) == 0xC0u && c >= 0xC2u && cont(1)) {
        cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
        return 2;
    }
    if ((c & 0xF0u) == 0xE0u && cont(1) && cont(2)) {
        cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
        if (cp < 0x800) return 0;
        return 3;
    }
    if ((c & 0xF8u) == 0xF0u && cont(1) && cont(2) && cont(3)) {
        cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
             ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
        if (cp < 0x10000 || cp > 0x10FFFF) return 0;
        return 4;
    }
    return 0;
}

}  // namespace

std::string fold_search(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
            ++i;
            continue;
        }
        uint32_t cp = 0;
        const size_t n = decode(s, i, cp);
        if (n == 0) {
            out += static_cast<char>(c);
            ++i;
            continue;
        }
        const char* rep = nullptr;
        if (cp >= 0xC0 && cp < 0x250) rep = kLatin[cp - 0xC0];
        else if (cp >= 0x1E00 && cp < 0x1F00) rep = kLatinAdditional[cp - 0x1E00];
        else if (cp >= 0x300 && cp < 0x370) rep = "";  // a combining mark (text already in NFD): dropped
        if (rep) out += rep;
        else out.append(s.data() + i, n);
        i += n;
    }
    return out;
}

bool fold_contains(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return true;
    const std::string n = fold_search(needle);
    if (n.empty()) return true;
    return fold_search(hay).find(n) != std::string::npos;
}

}  // namespace turbo
