// FC 27 LE Turbo GUI - accent-agnostic search text (text_fold.cpp). Every search box of the GUI compares fold_search(query)
// with fold_search(name): "Muller" finds "Müller", "sorloth" finds "Sørloth", "cagri" finds "Çağrı", "strasse" finds "Straße".
// Table-driven (no ICU, no locale): ASCII letters are lower-cased; the Latin letters of U+00C0..U+024F and U+1E00..U+1EFF lose
// their diacritics (NFKD-style, plus the letters Unicode does not decompose: ł l, đ d, ø o, ħ h, ı i, ŧ t, ...); ß ss, æ ae,
// œ oe, þ th, ð d, ĳ ij; combining marks (U+0300..U+036F) are dropped. Everything else (other scripts, symbols, invalid
// UTF-8 bytes) is kept byte for byte, so the output is still valid UTF-8 when the input was.
// Fold both sides once per list rebuild, not every frame.
#pragma once
#include <string>
#include <string_view>

namespace turbo {

std::string fold_search(std::string_view utf8);

// fold_search(hay) contains fold_search(needle); an empty needle matches everything. For one-off checks; a list folds its
// rows once and calls find() on the folded text.
bool fold_contains(std::string_view hay, std::string_view needle);

// A row's folded text (filled once per rebuild, e.g. PlayerRow::name_fold) contains the folded query q; a row whose folded text
// is empty but whose raw text is not (a row made by hand) is folded here. An empty q matches everything.
inline bool fold_find(const std::string& folded, std::string_view raw, const std::string& q) {
    if (q.empty()) return true;
    if (!folded.empty() || raw.empty()) return folded.find(q) != std::string::npos;
    return fold_search(raw).find(q) != std::string::npos;
}

}  // namespace turbo
