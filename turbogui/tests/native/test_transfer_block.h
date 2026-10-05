// Native tests: the Block Offers actions of the "transfer_list" game call (core/transfer_list.h kActionBlockOffers /
// kActionUnblockOffers / kActionQueryBlock, docs/re/player_status_roles.md section 2, docs/re/transfer_lists.md section 9).
// Included by test_main.cpp after ListWorld and FakeListGame (the synthetic career with the block record: the cache at
// TM+0x2D38 with vectors A / B and the TransferManager's block list at TM+0x2F50, and a fake game whose toggle does what the
// game's ToggleTransferBlock does to them).
#pragma once
#include <algorithm>

static void test_transfer_block() {
    using namespace turbo;
    using namespace turbo::tl;
    const Fns fns{ListWorld::kFnAddT, ListWorld::kFnAddL, ListWorld::kFnRemove, ListWorld::kHelperVt, ListWorld::kDaoVt, ListWorld::kTmVt,
                  ListWorld::kPcmVt, ListWorld::kUmVt, ListWorld::kFnToggle, ListWorld::kCacheVt, ListWorld::kBlockDaoVt};
    auto req_for = [](int action, int pid, int club = ListWorld::kNapoli) {
        Request q;
        q.action = action;
        q.player = pid;
        q.club = club;
        q.comm = ListWorld::kComm;
        q.managers = ListWorld::kManagers;
        return q;
    };
    auto has = [](const std::string& text, const char* part) { return text.find(part) != std::string::npos; };

    run_case("block offers: block, query, already blocked and unblock end to end (the toggle is called only when the state must flip)", [&] {
        ListWorld w;
        FakeListGame g(w);
        w.add_player(4001, 0);
        w.add_player(4900, 0);
        w.block_in_memory(4900);  // another player of the club who is blocked already
        w.set_block_list([&] {
            auto l = w.block_list();
            l.push_back({4901, kBlockFlagReleased});  // a released player: flag 1 in the list (and in vector B), not "blocked by you"
            return l;
        }());
        w.set_block_b({4901});
        Result r = run(w.mem, g, fns, req_for(kActionQueryBlock, 4001));
        CHECK(r.ok && r.stage == "done" && r.before == 0 && r.after == 0 && !r.called && g.toggles == 0, "query: " + r.message);
        CHECK(has(r.message, "offers are not blocked"), "query text: " + r.message);
        CHECK(r.at.block_cache == ListWorld::kCache && r.at.block_dao == ListWorld::kBlockDao && r.at.tm == ListWorld::kTm, "cache and inner dao located");
        r = run(w.mem, g, fns, req_for(kActionQueryBlock, 4900));
        CHECK(r.ok && r.before == 1 && r.after == 1 && has(r.message, "offers are blocked"), "query of a blocked player: " + r.message);
        r = run(w.mem, g, fns, req_for(kActionQueryBlock, 4901));
        CHECK(r.ok && r.before == 0 && has(r.message, "released-player"), "a released player is not blocked by you: " + r.message);
        // block
        r = run(w.mem, g, fns, req_for(kActionBlockOffers, 4001));
        CHECK(r.ok && r.stage == "done" && r.called && r.before == 0 && r.after == 1 && g.toggles == 1, "blocked: " + r.message);
        CHECK(g.last_helper == ListWorld::kDao + kDaoHelper, "the helper sub-object is passed as this");
        CHECK(w.in_a(4001) && w.list_flag(4001) == kBlockFlagOffers, "he is in vector A and in the block list with flag 0");
        CHECK(has(r.message, "block offers done") && has(r.message, "now blocked"), "message: " + r.message);
        CHECK(r.found && r.status_before == 0 && r.status_after == 0, "contract status carried separately: " + std::to_string(r.status_before));
        CHECK(w.in_a(4900) && w.list_flag(4900) == 0 && w.list_flag(4901) == kBlockFlagReleased && w.block_b().size() == 1, "the other players' entries are untouched");
        // already blocked: succeeds without calling
        r = run(w.mem, g, fns, req_for(kActionBlockOffers, 4001));
        CHECK(r.ok && !r.called && r.before == 1 && r.after == 1 && g.toggles == 1 && has(r.message, "already blocked") && has(r.message, "nothing was called"),
              "already blocked, not called: " + r.message);
        r = run(w.mem, g, fns, req_for(kActionQueryBlock, 4001));
        CHECK(r.ok && r.before == 1 && r.after == 1 && !r.called, "query sees blocked: " + r.message);
        // unblock
        r = run(w.mem, g, fns, req_for(kActionUnblockOffers, 4001));
        CHECK(r.ok && r.called && r.before == 1 && r.after == 0 && g.toggles == 2, "unblocked: " + r.message);
        CHECK(!w.in_a(4001) && w.list_flag(4001) == -1, "he is in neither vector A nor the block list");
        CHECK(has(r.message, "unblock offers done") && has(r.message, "no longer blocked"), "message: " + r.message);
        CHECK(w.in_a(4900) && w.list_flag(4900) == 0 && w.list_flag(4901) == kBlockFlagReleased, "the other players' entries are still untouched");
        r = run(w.mem, g, fns, req_for(kActionUnblockOffers, 4001));
        CHECK(r.ok && !r.called && r.before == 0 && r.after == 0 && g.toggles == 2 && has(r.message, "not blocked") && has(r.message, "nothing was called"),
              "already unblocked, not called: " + r.message);
        CHECK(w.status(4001) == 0 && g.adds_t == 0 && g.adds_l == 0 && g.removes == 0, "the list functions were never called");
        CHECK(w.mem.failed_reads == 0, "no read outside the synthetic memory");
        // blocking a released player's pid moves his entry to flag 0 (the game's cache->Block does the same): not a user's player in practice, but the read-back holds
        w.add_player(4901, 0);
        r = run(w.mem, g, fns, req_for(kActionBlockOffers, 4901));
        CHECK(r.ok && r.before == 0 && r.after == 1 && w.list_flag(4901) == kBlockFlagOffers && w.block_b().empty(), "a flag-1 entry becomes flag 0: " + r.message);
    });

    run_case("block offers: only the user's own players block; the status query reads any player; a player without a contract record can be blocked", [&] {
        ListWorld w;
        FakeListGame g(w);
        w.add_player(4101, 0);
        Result r = run(w.mem, g, fns, req_for(kActionBlockOffers, 4101, 1));
        CHECK(!r.ok && r.stage == "validate" && has(r.message, "not your club (team 48)") && has(r.message, "blocks offers only for your own players") && !r.called,
              "another club refused: " + r.message);
        r = run(w.mem, g, fns, req_for(kActionUnblockOffers, 4101, 1));
        CHECK(!r.ok && r.stage == "validate" && has(r.message, "not your club"), "unblock for another club refused: " + r.message);
        r = run(w.mem, g, fns, req_for(kActionBlockOffers, 4101, 0));
        CHECK(!r.ok && has(r.message, "club is not known"), "unknown club refused: " + r.message);
        r = run(w.mem, g, fns, req_for(kActionQueryBlock, 4101, 0));
        CHECK(r.ok && r.before == 0, "the query reads any player: " + r.message);
        CHECK(g.toggles == 0 && w.list_flag(4101) == -1, "the game was never called");
        // the active user index picks the team, as for the list actions
        w.mem.wr(ListWorld::kUm + kUmIndex, static_cast<int32_t>(1));
        r = run(w.mem, g, fns, req_for(kActionBlockOffers, 4101, 1));
        CHECK(r.ok && r.at.user_team == 1 && r.after == 1, "second user's club: " + r.message);
        w.mem.wr(ListWorld::kUm + kUmIndex, static_cast<int32_t>(0));
        // no contract record: the block record is global by player id, the game's own squad action works for him too
        r = run(w.mem, g, fns, req_for(kActionBlockOffers, 4999));
        CHECK(r.ok && r.called && !r.found && r.status_before == -1 && r.before == 0 && r.after == 1 && w.in_a(4999), "no contract record: " + r.message);
        r = run(w.mem, g, fns, req_for(kActionQueryBlock, 4999));
        CHECK(r.ok && r.before == 1, "and the query sees him: " + r.message);
        CHECK(run(w.mem, g, fns, req_for(kActionBlockOffers, 0)).message.find("positive") != std::string::npos, "player 0 refused");
    });

    run_case("block offers: a listed player ends unlisted (the game's RemoveFromLists), reported in the message and the status fields", [&] {
        ListWorld w;
        FakeListGame g(w);
        w.add_player(4201, kStatusTransferListed);
        w.add_player(4202, kStatusLoanListed);
        w.add_player(4203, kStatusBothLists);
        w.add_player(4204, 3);  // another contract status: blocking does not need the list eligibility rule, and does not touch it
        struct Case { int pid; int32_t status; const char* word; } cases[] = {{4201, 7, "transfer listed"}, {4202, 8, "loan listed"}, {4203, 9, "transfer and loan listed"}};
        for (const auto& c : cases) {
            Result r = run(w.mem, g, fns, req_for(kActionBlockOffers, c.pid));
            CHECK(r.ok && r.called && r.before == 0 && r.after == 1, "blocked: " + r.message);
            CHECK(r.status_before == c.status && r.status_after == kStatusNone && w.status(c.pid) == kStatusNone, "status " + std::to_string(c.status) + " -> 0");
            CHECK(has(r.message, c.word) && has(r.message, "took him off the list"), "reported: " + r.message);
            r = run(w.mem, g, fns, req_for(kActionUnblockOffers, c.pid));
            CHECK(r.ok && r.before == 1 && r.after == 0 && r.status_before == 0 && r.status_after == 0 && !has(r.message, "took him off"),
                  "unblocking does not report a list change: " + r.message);
        }
        Result r = run(w.mem, g, fns, req_for(kActionBlockOffers, 4204));
        CHECK(r.ok && r.status_before == 3 && r.status_after == 3 && !has(r.message, "took him off"), "an unrelated status is left alone: " + r.message);
        CHECK(g.removes == 0 && g.adds_t == 0 && g.adds_l == 0, "the list functions were never called");
    });

    run_case("block offers: a corrupted block record is refused before anything is called (vectors, sizes, vtables)", [&] {
        ListWorld w;
        FakeListGame g(w);
        w.add_player(4301, 0);
        w.block_in_memory(4302);
        const uint64_t cache = ListWorld::kCache, tm = ListWorld::kTm;
        auto save = [&](uint64_t a) {
            uint64_t v = 0;
            w.mem.rd(a, v);
            return v;
        };
        auto refused = [&](const char* what, const char* text, const std::function<void()>& corrupt, const std::function<void()>& restore) {
            corrupt();
            for (int action : {kActionBlockOffers, kActionUnblockOffers, kActionQueryBlock}) {
                Result r = run(w.mem, g, fns, req_for(action, 4301));
                CHECK(!r.ok && r.stage == "validate" && has(r.message, text) && !r.called, std::string(what) + " (" + action_name(action) + "): " + r.message);
            }
            CHECK(g.toggles == 0, std::string(what) + ": the game was not called");
            restore();
            Result ok = run(w.mem, g, fns, req_for(kActionQueryBlock, 4301));
            CHECK(ok.ok, std::string(what) + ": restored: " + ok.message);
        };
        // vector A (cache +0x10 / +0x18)
        const uint64_t a_b = save(cache + kBlockCacheABegin), a_e = save(cache + kBlockCacheAEnd);
        auto restore_a = [&] {
            w.mem.wr(cache + kBlockCacheABegin, a_b);
            w.mem.wr(cache + kBlockCacheAEnd, a_e);
        };
        refused("A begin after end", "is after end", [&] { w.mem.wr(cache + kBlockCacheABegin, a_b + 8); w.mem.wr(cache + kBlockCacheAEnd, a_b); }, restore_a);
        refused("A not whole elements", "not a whole number of 4-byte", [&] { w.mem.wr(cache + kBlockCacheAEnd, a_b + 6); }, restore_a);
        refused("A more than 2000 elements", "2001 elements", [&] { w.mem.wr(cache + kBlockCacheAEnd, a_b + 2001 * 4); }, restore_a);
        refused("A unmapped", "vector A", [&] { w.mem.wr(cache + kBlockCacheABegin, 0x31000000ULL); w.mem.wr(cache + kBlockCacheAEnd, 0x31000008ULL); }, restore_a);
        refused("A running into unmapped memory", "is not readable (16 bytes)",
                [&] { w.mem.wr(cache + kBlockCacheABegin, ListWorld::kVecA + 0x3FF8); w.mem.wr(cache + kBlockCacheAEnd, ListWorld::kVecA + 0x4008); }, restore_a);
        refused("A begin not a pointer", "not a pointer", [&] { w.mem.wr(cache + kBlockCacheABegin, 0x10ULL); w.mem.wr(cache + kBlockCacheAEnd, 0x18ULL); }, restore_a);
        // vector B (cache +0x30 / +0x38): the game's own Block erases from it
        const uint64_t b_b = save(cache + kBlockCacheBBegin), b_e = save(cache + kBlockCacheBEnd);
        auto restore_b = [&] {
            w.mem.wr(cache + kBlockCacheBBegin, b_b);
            w.mem.wr(cache + kBlockCacheBEnd, b_e);
        };
        refused("B begin after end", "vector B", [&] { w.mem.wr(cache + kBlockCacheBBegin, b_b + 4); w.mem.wr(cache + kBlockCacheBEnd, b_b); }, restore_b);
        // the TransferManager's block list (+0x2F50 / +0x2F58)
        const uint64_t l_b = save(tm + kTmBlockListBegin), l_e = save(tm + kTmBlockListEnd);
        auto restore_l = [&] {
            w.mem.wr(tm + kTmBlockListBegin, l_b);
            w.mem.wr(tm + kTmBlockListEnd, l_e);
        };
        refused("list begin after end", "block list", [&] { w.mem.wr(tm + kTmBlockListBegin, l_b + 8); w.mem.wr(tm + kTmBlockListEnd, l_b); }, restore_l);
        refused("list not whole 8-byte entries", "not a whole number of 8-byte", [&] { w.mem.wr(tm + kTmBlockListEnd, l_b + 12); }, restore_l);
        refused("list more than 2000 entries", "2001 elements", [&] { w.mem.wr(tm + kTmBlockListEnd, l_b + 2001 * 8); }, restore_l);
        refused("list unmapped", "block list", [&] { w.mem.wr(tm + kTmBlockListBegin, 0x31000000ULL); w.mem.wr(tm + kTmBlockListEnd, 0x31000010ULL); }, restore_l);
        // the objects
        refused("cache vtable", "not the CachedTransferblockDaoImpl", [&] { w.mem.wr(cache, 0x14B000000ULL); }, [&] { w.mem.wr(cache, ListWorld::kCacheVt); });
        refused("cache pointer empty", "TransferManager+0x2D38", [&] { w.mem.wr(tm + kTmBlockCache, 0ULL); }, [&] { w.mem.wr(tm + kTmBlockCache, ListWorld::kCache); });
        refused("inner dao vtable", "not the TransferblockDaoImpl", [&] { w.mem.wr(ListWorld::kBlockDao, 0x14B000000ULL); },
                [&] { w.mem.wr(ListWorld::kBlockDao, ListWorld::kBlockDaoVt); });
        refused("inner dao of another manager table", "belongs to another manager table", [&] { w.mem.wr(ListWorld::kBlockDao + kBlockDaoHub, ListWorld::kOwner); },
                [&] { w.mem.wr(ListWorld::kBlockDao + kBlockDaoHub, ListWorld::kManagers); });
        refused("inner dao pointer empty", "TransferblockDaoImpl", [&] { w.mem.wr(cache + kBlockCacheInner, 0ULL); },
                [&] { w.mem.wr(cache + kBlockCacheInner, ListWorld::kBlockDao); });
        // exactly 2000 elements is the limit and still fine
        std::vector<int32_t> many;
        for (int i = 0; i < kMaxBlockElements; ++i) many.push_back(100000 + i);
        w.set_block_a(many);
        std::vector<ListWorld::BlockEntry> entries;
        for (int i = 0; i < kMaxBlockElements; ++i) entries.push_back({100000 + i, 0});
        w.set_block_list(entries);
        Result r = run(w.mem, g, fns, req_for(kActionQueryBlock, 4301));
        CHECK(r.ok && r.before == 0, "2000 elements are accepted: " + r.message);
        r = run(w.mem, g, fns, req_for(kActionQueryBlock, 100007));
        CHECK(r.ok && r.before == 1, "and searched: " + r.message);
        w.set_block_a({});
        w.set_block_list({});
        w.block_in_memory(4302);
        r = run(w.mem, g, fns, req_for(kActionBlockOffers, 4301));
        CHECK(r.ok && r.called && w.in_a(4301) && w.in_a(4302) && g.toggles == 1, "the valid state blocks: " + r.message);
    });

    run_case("block offers: the cache and the block list must agree about the player (the toggle follows the cache, the save holds the list)", [&] {
        ListWorld w;
        FakeListGame g(w);
        w.add_player(4401, 0);
        w.add_player(4402, 0);
        w.set_block_a({4401});  // in the cache only
        auto l = w.block_list();
        l.push_back({4402, 0});  // in the list only
        w.set_block_list(l);
        for (int pid : {4401, 4402}) {
            for (int action : {kActionBlockOffers, kActionUnblockOffers}) {
                Result r = run(w.mem, g, fns, req_for(action, pid));
                CHECK(!r.ok && r.stage == "validate" && has(r.message, "block-offers cache says") && has(r.message, "nothing is called") && !r.called,
                      "disagreement refused: " + r.message);
            }
        }
        Result r = run(w.mem, g, fns, req_for(kActionQueryBlock, 4401));
        CHECK(r.ok && r.before == 1 && has(r.message, "disagrees"), "the query reports the cache's answer and the disagreement: " + r.message);
        r = run(w.mem, g, fns, req_for(kActionQueryBlock, 4402));
        CHECK(r.ok && r.before == 0 && has(r.message, "disagrees"), "and the other way round: " + r.message);
        CHECK(g.toggles == 0, "the game was never called");
    });

    run_case("block offers: vtable slot 30 must be the toggle Turbo resolved; each missing entry stops only what needs it", [&] {
        ListWorld w;
        FakeListGame g(w);
        w.add_player(4501, 0);
        w.mem.wr(ListWorld::kHelperVt + kHelperSlotToggleBlock * 8, 0x147F00000ULL);
        for (int action : {kActionBlockOffers, kActionUnblockOffers}) {
            Result r = run(w.mem, g, fns, req_for(action, 4501));
            CHECK(!r.ok && r.stage == "validate" && has(r.message, "slot 30") && has(r.message, "ToggleTransferBlock") && has(r.message, "layout mismatch") && !r.called,
                  "slot 30 mismatch (" + std::string(action_name(action)) + "): " + r.message);
        }
        // the query calls nothing, the list actions have their own slots: both still run
        Result r = run(w.mem, g, fns, req_for(kActionQueryBlock, 4501));
        CHECK(r.ok, "query without slot 30: " + r.message);
        r = run(w.mem, g, fns, req_for(kActionTransferList, 4501));
        CHECK(r.ok && r.after == kStatusTransferListed, "list action unaffected by slot 30: " + r.message);
        CHECK(g.toggles == 0, "the toggle was never called");
        w.mem.wr(ListWorld::kHelperVt + kHelperSlotToggleBlock * 8, ListWorld::kFnToggle);
        r = run(w.mem, g, fns, req_for(kActionBlockOffers, 4501));
        CHECK(r.ok && r.called, "slot 30 restored: " + r.message);
        // signatures not resolved
        w.add_player(4502, 0);
        Fns f = fns;
        f.toggle_block = 0;
        r = run(w.mem, g, f, req_for(kActionBlockOffers, 4502));
        CHECK(!r.ok && r.stage == "validate" && has(r.message, "uah_toggle_transfer_block") && has(r.message, "not resolved"), "toggle missing: " + r.message);
        r = run(w.mem, g, f, req_for(kActionQueryBlock, 4502));
        CHECK(r.ok, "query needs no toggle: " + r.message);
        r = run(w.mem, g, f, req_for(kActionLoanList, 4502));
        CHECK(r.ok && r.after == kStatusLoanListed, "list actions need no block entries: " + r.message);
        f = fns;
        f.cachedblock_vtable = 0;
        for (int action : {kActionBlockOffers, kActionUnblockOffers, kActionQueryBlock}) {
            r = run(w.mem, g, f, req_for(action, 4502));
            CHECK(!r.ok && has(r.message, "cachedblock_vtable"), "cache vtable missing: " + r.message);
        }
        f = fns;
        f.blockdao_vtable = 0;
        r = run(w.mem, g, f, req_for(kActionBlockOffers, 4502));
        CHECK(!r.ok && has(r.message, "blockdao_vtable"), "dao vtable missing: " + r.message);
        CHECK(std::string(Fns().missing_block(true)) == "uah_toggle_transfer_block" && std::string(Fns().missing_block(false)) == "cachedblock_vtable" &&
                  fns.missing_block(true) == nullptr,
              "missing_block names the first missing entry");
        // addresses outside FC27.exe
        Request q = req_for(kActionBlockOffers, 4502);
        q.image_base = 0x140000000ULL;
        q.image_size = 0x10000000ULL;
        f = fns;
        f.toggle_block = 0x7FF600001000ULL;
        r = run(w.mem, g, f, q);
        CHECK(!r.ok && has(r.message, "outside FC27.exe"), "toggle outside the image: " + r.message);
        f = fns;
        f.blockdao_vtable = 0x7FF600001000ULL;
        r = run(w.mem, g, f, q);
        CHECK(!r.ok && has(r.message, "outside FC27.exe"), "dao vtable outside the image: " + r.message);
        r = run(w.mem, g, fns, q);
        CHECK(r.ok && r.called, "inside the image: " + r.message);
    });

    run_case("block offers: the game failing, refusing or half-doing the toggle is reported, never assumed", [&] {
        ListWorld w;
        FakeListGame g(w);
        w.add_player(4601, 0);
        g.fail = true;
        Result r = run(w.mem, g, fns, req_for(kActionBlockOffers, 4601));
        CHECK(!r.ok && r.stage == "call" && has(r.message, "ToggleTransferBlock: boom") && !r.called && g.toggles == 1, "call failed: " + r.message);
        g.fail = false;
        g.noop = true;
        r = run(w.mem, g, fns, req_for(kActionBlockOffers, 4601));
        CHECK(!r.ok && r.stage == "check" && has(r.message, "the game refused") && has(r.message, "stayed not blocked") && r.called && r.before == 0 && r.after == 0,
              "block refused: " + r.message);
        g.noop = false;
        g.half = true;  // vector A changes, the block list does not
        r = run(w.mem, g, fns, req_for(kActionBlockOffers, 4601));
        CHECK(!r.ok && r.stage == "check" && has(r.message, "inconsistent") && has(r.message, "cache yes, list no") && r.called && r.after == 1,
              "half done: " + r.message);
        g.half = false;
        // heal it and repeat for the unblock direction
        w.set_block_a({});
        w.set_block_list({});
        w.block_in_memory(4601);
        g.noop = true;
        r = run(w.mem, g, fns, req_for(kActionUnblockOffers, 4601));
        CHECK(!r.ok && has(r.message, "the game refused") && has(r.message, "stayed blocked") && r.before == 1 && r.after == 1, "unblock refused: " + r.message);
        g.noop = false;
        g.half = true;
        r = run(w.mem, g, fns, req_for(kActionUnblockOffers, 4601));
        CHECK(!r.ok && has(r.message, "inconsistent") && has(r.message, "cache no, list yes") && r.after == 0, "unblock half done: " + r.message);
    });

    run_case("block offers: action codes and names (unknown codes stay refused, 7 / 8 / 9 are the block actions)", [&] {
        CHECK(kActionBlockOffers == 7 && kActionUnblockOffers == 8 && kActionQueryBlock == 9, "the Lua-visible codes");
        for (int a = 1; a <= 9; ++a) CHECK(valid_action(a), "valid " + std::to_string(a));
        CHECK(!valid_action(0) && !valid_action(10) && !valid_action(-1), "0 / 10 / -1 are not actions");
        CHECK(!is_block_action(6) && is_block_action(7) && is_block_action(8) && is_block_action(9) && !is_block_action(10), "block actions are 7..9");
        CHECK(changes_block(7) && changes_block(8) && !changes_block(9) && !changes_block(6), "only 7 and 8 call the toggle");
        CHECK(std::string(action_name(7)) == "block offers" && std::string(action_name(8)) == "unblock offers" && std::string(action_name(9)) == "block status", "names");
        CHECK(std::string(block_name(0)) == "not blocked" && std::string(block_name(1)) == "blocked" && std::string(block_name(-1)) == "unknown block state", "block_name");
        ListWorld w;
        FakeListGame g(w);
        Request q = req_for(10, 1);
        Result r = run(w.mem, g, fns, q);
        CHECK(!r.ok && has(r.message, "unknown transfer-list action 10") && g.toggles == 0, "action 10: " + r.message);
    });

    run_case("block offers: the Lua-visible contract through the mailbox call block (op 10, codes 7 / 8 / 9: ok, text, status, before, after)", [&] {
        ListWorld w;
        FakeListGame g(w);
        w.add_player(4701, kStatusTransferListed);
        SimMemory mbm;
        const uint64_t mb = 0x31000000ULL;
        mbm.map(mb, kMailboxSize);
        int seq = 100;
        // what Lua writes (TurboTransferList(code, pid, club)) and what turbo_game_call / transfer_list_request do with it
        auto op10 = [&](int code, int pid, int club) {
            const int64_t args[4] = {code, pid, static_cast<int64_t>(ListWorld::kComm), club};
            CHECK(write_call_request(mbm, mb, ++seq, kCallOpTransferList, args), "request written");
            GameCallBlock b;
            CHECK(read_call_block(mbm, mb, b) && b.op == kCallOpTransferList && b.seq == seq, "request read");
            Request req;
            req.action = static_cast<int>(b.args[0]);
            req.player = static_cast<int>(b.args[1]);
            req.comm = static_cast<uint64_t>(b.args[2]);
            req.club = static_cast<int>(b.args[3]);
            Result res;
            if (req.player <= 0 || !valid_action(req.action)) {
                res.message = req.player <= 0 ? "player id must be a positive number" : "unknown transfer-list action " + std::to_string(req.action);
            } else {
                res = run(w.mem, g, fns, req);
            }
            CHECK(write_call_result(mbm, mb, b.seq, res.ok ? kCallOk : kCallFailed, res.before, res.after, res.message), "result written");
            CHECK(read_call_block(mbm, mb, b) && b.result_seq == seq, "result read");
            return b;
        };
        GameCallBlock b = op10(9, 4701, 0);
        CHECK(b.status == kCallOk && b.out[0] == 0 && b.out[1] == 0 && has(b.text, "offers are not blocked"), "9 query: " + b.text);
        b = op10(7, 4701, ListWorld::kNapoli);
        CHECK(b.status == kCallOk && b.out[0] == 0 && b.out[1] == 1 && has(b.text, "now blocked") && has(b.text, "took him off the list") && g.toggles == 1,
              "7 block (before / after = block state 0 -> 1): " + b.text);
        CHECK(w.status(4701) == 0, "he ended unlisted");
        b = op10(7, 4701, ListWorld::kNapoli);
        CHECK(b.status == kCallOk && b.out[0] == 1 && b.out[1] == 1 && has(b.text, "already blocked") && g.toggles == 1, "7 again: succeeds, nothing called: " + b.text);
        b = op10(9, 4701, 0);
        CHECK(b.status == kCallOk && b.out[0] == 1 && b.out[1] == 1, "9 query after: blocked");
        b = op10(8, 4701, ListWorld::kNapoli);
        CHECK(b.status == kCallOk && b.out[0] == 1 && b.out[1] == 0 && has(b.text, "no longer blocked") && g.toggles == 2, "8 unblock (1 -> 0): " + b.text);
        b = op10(8, 4701, 1);
        CHECK(b.status == kCallFailed && has(b.text, "not your club"), "8 for another club: failed: " + b.text);
        b = op10(10, 4701, ListWorld::kNapoli);
        CHECK(b.status == kCallFailed && has(b.text, "unknown transfer-list action 10"), "10: failed: " + b.text);
        b = op10(7, 0, ListWorld::kNapoli);
        CHECK(b.status == kCallFailed && has(b.text, "positive"), "player 0: failed: " + b.text);
        // the list codes keep their meaning: before / after = contract status
        b = op10(1, 4701, ListWorld::kNapoli);
        CHECK(b.status == kCallOk && b.out[0] == 0 && b.out[1] == kStatusTransferListed, "1 transfer list still reports the contract status: " + b.text);
        b = op10(6, 4701, 0);
        CHECK(b.status == kCallOk && b.out[0] == kStatusTransferListed && b.out[1] == kStatusTransferListed, "6 query still reports the contract status: " + b.text);
        CHECK(b.text.size() < kMbCallTextSize, "texts fit the call block");
    });

    run_case("signatures: the Block Offers entries resolve on the game's bytes and equal scripts/re/player_status_signatures.json", [&] {
        const SignatureTable* t = builtin_signature_table("6AB9813C-211EF000");
        CHECK(t != nullptr, "built-in table");
        if (!t) return;
        // bytes read from fc27_image.bin (FC27.exe 1.0.140.64835) at each anchor (scripts/re/sig_player_status.py)
        struct Blob { const char* name; uint64_t va; uint64_t target; std::vector<uint8_t> bytes; };
        const std::vector<Blob> blobs = {
            {"uah_toggle_transfer_block", 0x147F688B8ULL, 0x147F688B8ULL,
             {0x89, 0x54, 0x24, 0x10, 0x55, 0x53, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00}},
            {"cachedblock_vtable", 0x147C27073ULL, 0x14B005400ULL,
             {0x49, 0x8B, 0x06, 0x48, 0x8D, 0x0D, 0x83, 0xE3, 0x3D, 0x03, 0x48, 0x89, 0x0A, 0x48, 0x89, 0x42, 0x08, 0x4C, 0x89, 0x62, 0x10, 0x4C, 0x89, 0x62,
              0x18}},
            {"blockdao_vtable", 0x147C27013ULL, 0x14B005630ULL,
             {0x48, 0x8D, 0x0D, 0x16, 0xE6, 0x3D, 0x03, 0x4C, 0x89, 0x60, 0x08, 0x48, 0x89, 0x08, 0x45, 0x33, 0xE4, 0x48, 0x89, 0x70, 0x10, 0xEB, 0x06, 0x45,
              0x33, 0xE4}},
        };
        for (const auto& b : blobs) {
            const Signature* s = t->find(b.name);
            CHECK(s != nullptr && !s->pattern.empty(), std::string("entry ") + b.name);
            if (!s) continue;
            std::vector<uint8_t> code(0x200, 0xCC);
            std::memcpy(code.data() + 0x100, b.bytes.data(), b.bytes.size());
            SigResult r = resolve_signature(*s, code.data(), code.size(), b.va - 0x100);
            CHECK(r.state == SigState::Found && r.match == b.va && r.address == b.target,
                  std::string(b.name) + " resolves: " + r.error + " (match " + hex_addr(r.match) + ", address " + hex_addr(r.address) + ")");
        }
        // the three together in one buffer, next to the list helper with the same first bytes (try_remove 89 54 24 10 53 55 ...): each matches once
        std::vector<uint8_t> all(0x1000, 0xCC);
        const uint8_t try_remove[] = {0x89, 0x54, 0x24, 0x10, 0x53, 0x55, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x4C, 0x8B, 0xF9, 0x48, 0x63, 0xEA};
        std::memcpy(all.data() + 0x800, try_remove, sizeof(try_remove));
        for (size_t i = 0; i < blobs.size(); ++i) std::memcpy(all.data() + 0x100 + i * 0x100, blobs[i].bytes.data(), blobs[i].bytes.size());
        for (size_t i = 0; i < blobs.size(); ++i) {
            SigResult r = resolve_signature(*t->find(blobs[i].name), all.data(), all.size(), 0x147000000ULL);
            CHECK(r.hits == 1 && r.match == 0x147000000ULL + 0x100 + i * 0x100, std::string(blobs[i].name) + " unique next to the list helpers");
        }
        // the signature JSON of the research track carries the same patterns
        fs::path json_path = fs::path("..") / "scripts" / "re" / "player_status_signatures.json";
        std::error_code ec;
        if (!fs::exists(json_path, ec)) json_path = fs::path("scripts") / "re" / "player_status_signatures.json";
        std::ifstream jf(json_path);
        CHECK(static_cast<bool>(jf), "player_status_signatures.json found at " + json_path.string());
        if (jf) {
            std::string text((std::istreambuf_iterator<char>(jf)), std::istreambuf_iterator<char>());
            SignatureTable file;
            std::string err;
            CHECK(parse_signature_table(text, file, err), "player_status_signatures.json parses: " + err);
            for (const auto& b : blobs) {
                const Signature* a = t->find(b.name);
                const Signature* f = file.find(b.name);
                CHECK(a && f && a->pattern == f->pattern && a->offset == f->offset && a->resolve == f->resolve,
                      std::string(b.name) + ": built-in entry equals the JSON");
            }
        }
    });
}
