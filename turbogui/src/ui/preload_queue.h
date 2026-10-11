// FC 27 LE Turbo GUI - the order in which pictures are loaded in the background (ui/preload.h uses it).
// Pure: no ImGui, no App, so the native tests can check the order and the cancellation.
//
// Order (what Cassio is most likely to open first, 2026-10-11):
//   1. minifaces of the user's club        (Players list / Teams > Squad open on the user's club)
//   2. minifaces of every player listed    (the Players list as filtered now)
//   3. the real-face chooser               (minifaces of the real faces; cached 3D head renders, turbo_output\cache\faces3d)
//   4. tattoo previews
//   5. hair / facial hair / boots / gloves / accessory galleries (Live Editor's hash list)
//   6. club crests
//   7. managers' real-face heads
// The first kFirstPerKind pictures of every kind come before the rest of any kind, so each gallery's first screens are
// ready early even when one list is huge (18 000 listed players); the remainder follows in the same kind order.
#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

namespace turbo {

enum class PreloadKind : int { ClubMiniface = 0, ListedMiniface, FaceChooser, Tattoo, Gallery, Crest, ManagerHead, kCount };
constexpr int kPreloadKinds = static_cast<int>(PreloadKind::kCount);

inline const char* preload_kind_name(PreloadKind k) {
    switch (k) {
        case PreloadKind::ClubMiniface: return "your club's faces";
        case PreloadKind::ListedMiniface: return "listed players' faces";
        case PreloadKind::FaceChooser: return "real-face chooser";
        case PreloadKind::Tattoo: return "tattoos";
        case PreloadKind::Gallery: return "hair / body galleries";
        case PreloadKind::Crest: return "crests";
        case PreloadKind::ManagerHead: return "manager heads";
        default: return "?";
    }
}

class PreloadQueue {
public:
    static constexpr size_t kFirstPerKind = 400;

    struct Item {
        std::string path;   // legacy path (data/ui/...) or an absolute cache file (faces3d renders, `file` = true)
        PreloadKind kind = PreloadKind::ClubMiniface;
        bool file = false;
        bool settled = false;  // decoded (handed to the worker), missing in the game, or invalid
        bool asked = false;    // asked from Turbo's Lua side (LegacyImages::ask_background accepted it, or it was known)
        bool refused = false;  // Lua's background list was full when it was asked: still pending and unasked, asked again later
        size_t order = 0;      // insertion index within its kind
    };

    // false: empty, a duplicate, or the queue was cancelled (reset() first)
    bool add(PreloadKind kind, const std::string& path, bool file = false) {
        if (cancelled_ || path.empty() || !seen_.insert(path).second) return false;
        Item it;
        it.path = path;
        it.kind = kind;
        it.file = file;
        it.order = added_[static_cast<int>(kind)]++;
        items_.push_back(std::move(it));
        sorted_ = false;
        return true;
    }

    // Puts the items in loading order (see the header). Called once after collecting; add() after it re-sorts lazily.
    void finalize() {
        if (sorted_) return;
        std::stable_sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) {
            const int la = a.order >= kFirstPerKind ? 1 : 0, lb = b.order >= kFirstPerKind ? 1 : 0;
            if (la != lb) return la < lb;
            if (a.kind != b.kind) return static_cast<int>(a.kind) < static_cast<int>(b.kind);
            return a.order < b.order;
        });
        sorted_ = true;
        pos_ = 0;
    }

    // The next item still waiting, in order, round robin (a Waiting item is looked at again later); nullptr when none
    Item* next_pending() {
        finalize();
        if (settled_ >= items_.size()) return nullptr;
        for (size_t n = 0; n < items_.size(); ++n) {
            if (pos_ >= items_.size()) pos_ = 0;
            Item& it = items_[pos_++];
            if (!it.settled) return &it;
        }
        return nullptr;
    }

    void settle(Item& it) {
        if (it.settled) return;
        unrefuse(it);
        it.settled = true;
        ++settled_;
        ++settled_kind_[static_cast<int>(it.kind)];
    }
    // The background list was full (LegacyImages::Ask::Full): the item stays pending and unasked; next_pending() brings
    // it round again and the caller asks once more. Counted once however often it is refused.
    void refuse(Item& it) {
        if (it.settled || it.refused) return;
        it.refused = true;
        ++refused_;
    }
    // Asked (accepted), or already known to the Lua side: nothing more to ask for it
    void accept(Item& it) {
        unrefuse(it);
        it.asked = true;
    }
    size_t refused() const { return refused_; }

    // The career left (or the setting was switched off): forget everything; add() is refused until reset()
    void cancel() {
        clear();
        cancelled_ = true;
    }
    // Empty and accepting again (the next career)
    void reset() {
        clear();
        cancelled_ = false;
    }

    bool cancelled() const { return cancelled_; }
    bool done() const { return settled_ >= items_.size(); }
    size_t size() const { return items_.size(); }
    size_t settled() const { return settled_; }
    size_t pending() const { return items_.size() - settled_; }
    size_t count(PreloadKind k) const { return added_[static_cast<int>(k)]; }
    size_t settled(PreloadKind k) const { return settled_kind_[static_cast<int>(k)]; }
    const std::vector<Item>& items() const { return items_; }
    std::vector<Item>& items() { return items_; }

private:
    void clear() {
        items_.clear();
        seen_.clear();
        for (int i = 0; i < kPreloadKinds; ++i) added_[i] = settled_kind_[i] = 0;
        settled_ = refused_ = pos_ = 0;
        sorted_ = true;
    }
    void unrefuse(Item& it) {
        if (!it.refused) return;
        it.refused = false;
        --refused_;
    }

    std::vector<Item> items_;
    std::unordered_set<std::string> seen_;
    size_t added_[kPreloadKinds] = {};
    size_t settled_kind_[kPreloadKinds] = {};
    size_t settled_ = 0, refused_ = 0, pos_ = 0;
    bool sorted_ = true, cancelled_ = false;
};

}  // namespace turbo
