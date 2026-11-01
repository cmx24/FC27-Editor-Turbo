// Native tests (Turbo X 1.0.1 L1, 08_FABLE_REVIEW_AND_NOON_PLAN.md section 2 #4 / section 9): the Commentary name card had no play control for
// the CURRENT club (the donor rows had one since 5.0c E15-preview; the current club is not a row). Now a play button "##play_c<club id>" sits in
// front of "Commentary says: ...": the same shared button (ui_callname_play.cpp callname_play_button, kind Club), the same wav,
// <turbo_output>\cache\teamaudio\<loaded lang>\<club id>.wav. Behavioural: the card is drawn for real (club_40_test::show) with a fake WavPlayer,
// so the 1.0 card (no button for the current club) FAILS by assertion.
//   a  the current club (7) has a play button; a click plays exactly <lang folder>\7.wav
//   b  no wav for the current club: the button is there, disabled, and a click plays nothing
// Included by test_main.cpp after test_e15_preview.h; cvplay_test::ui_cases(app, ui, le) is called right after e15p_test::ui_cases.
#pragma once

namespace cvplay_test {

using namespace turbo;

static const char* kLang = "eng_cvplay";  // its own language folder: nothing else writes there

struct FakeWav : WavPlayer {
    std::vector<std::string> played;  // full paths
    bool play(const fs::path& p) override {
        played.push_back(p.u8string());
        return true;
    }
    void stop() override {}
};

static void ui_cases(App& app, Ui& ui, const fs::path& /*le*/) {
    ui.frames(3);
    if (!app.connected()) app.refresh();
    ui.frames(3);

    const fs::path dir = app.turbo_output() / "cache" / "teamaudio" / kLang;
    std::error_code ec;
    fs::create_directories(dir, ec);
    {
        const fs::path cdir = app.bridge.root() / "turbo" / "callnames" / "clubs";
        fs::create_directories(cdir, ec);
        json j;
        j["lang"] = kLang;
        j["ids"] = std::vector<int>{7, 1001};
        std::ofstream((cdir / (std::string(kLang) + ".json")).string()) << j.dump();
    }
    auto fake = std::make_shared<FakeWav>();
    app.callname_player.set_player(fake);

    auto open_card = [&]() {
        app.sel_team = 7;
        app.callnames.lang = kLang;
        club_40_test::show(ui, draw_club_identity_page);
        for (int k = 0; k < 4; ++k) {
            app.callnames.lang = kLang;
            ui.frames(2);
        }
        return e15_test::reveal(ui, "##play_c7");
    };

    run_case("1.0.1 L1 club voice play a: the current club has a play button; a click plays <lang folder>\\<club id>.wav", [&] {
        {
            std::ofstream w((dir / "7.wav").string(), std::ios::binary);
            w << "RIFF....WAVEfmt ";
        }
        ui.frames(310);  // the club audio folder is looked at again every 5 s (ui_callname_play.cpp club_audio)
        CHECK(open_card(), "the current club (7) has a play button ##play_c7 on the Commentary name card");
        fake->played.clear();
        CHECK(ui.click("##play_c7"), "play pressed on the current club");
        ui.frames(2);
        CHECK(fake->played.size() == 1, fmt("one wav started (%zu)", fake->played.size()));
        if (!fake->played.empty()) {
            const std::string p = fake->played.front();
            const bool is7 = p.size() >= 6 && p.compare(p.size() - 5, 5, "7.wav") == 0 && (p[p.size() - 6] == '/' || p[p.size() - 6] == '\\');
            CHECK(is7 && p.find("teamaudio") != std::string::npos && p.find(kLang) != std::string::npos,
                  "the wav is the current club's, in the loaded language's folder: " + p);
        }
        app.callname_player.stop();
        ui.frames(2);
        club_40_test::hide(ui);
    });

    run_case("1.0.1 L1 club voice play b: no clip for the current club: the button is there, disabled, a click plays nothing", [&] {
        fs::remove(dir / "7.wav", ec);
        ui.frames(310);  // the 5 s re-check
        CHECK(open_card(), "the current club (7) still has its play button (disabled, with the reason)");
        fake->played.clear();
        ui.click("##play_c7");
        ui.frames(2);
        CHECK(fake->played.empty(), fmt("no wav for club 7: nothing played (%zu)", fake->played.size()));
        club_40_test::hide(ui);
    });

    // TX-102 R2-O2: the button plays the clip IN EFFECT (Fable's 1.0.1 review note): a donor voice set -> the donor's clip; "no club name" -> disabled
    run_case("TX-102 R2-O2 club voice play c: a donor voice in effect (7 says 1001's name): the button plays the donor's clip, not the club's own", [&] {
        for (const char* n : {"7.wav", "1001.wav"}) {
            std::ofstream w((dir / n).string(), std::ios::binary);
            w << "RIFF....WAVEfmt ";
        }
        clubvoice::Entry e;
        e.teamid = 7;
        e.voice_of = 1001;
        e.scope = "";  // not assigned: in effect in every career
        app.club_voice.upsert(e);
        CHECK(app.club_voice.find_in_effect(7, app.reapply.career) && app.club_voice.find_in_effect(7, app.reapply.career)->voice_of == 1001, "the donor entry is in effect");
        ui.frames(310);  // the 5 s re-check of the club audio folder
        app.sel_team = 7;
        app.callnames.lang = kLang;
        club_40_test::show(ui, draw_club_identity_page);
        for (int k = 0; k < 4; ++k) {
            app.callnames.lang = kLang;
            ui.frames(2);
        }
        CHECK(e15_test::reveal(ui, "##play_c1001"), "the card's play button is the donor's (##play_c1001)");
        fake->played.clear();
        CHECK(ui.click("##play_c1001"), "play pressed");
        ui.frames(2);
        CHECK(fake->played.size() == 1, fmt("one wav started (%zu)", fake->played.size()));
        if (!fake->played.empty()) {
            const std::string p = fake->played.front();
            const bool is1001 = p.size() >= 9 && p.compare(p.size() - 8, 8, "1001.wav") == 0;
            CHECK(is1001 && p.find(kLang) != std::string::npos, "the wav is the donor's (1001), in the loaded language's folder: " + p);
        }
        app.callname_player.stop();
        ui.frames(2);
        club_40_test::hide(ui);
        app.club_voice.forget(7, "");
    });

    run_case("TX-102 R2-O2 club voice play d: \"no club name\" in effect: the button is there, disabled, a click plays nothing", [&] {
        clubvoice::Entry e;
        e.teamid = 7;
        e.voice_of = 0;
        e.scope = "";
        app.club_voice.upsert(e);
        CHECK(open_card(), "the current club (7) has its play button (disabled)");
        fake->played.clear();
        ui.click("##play_c7");
        ui.frames(2);
        CHECK(fake->played.empty(), fmt("no club name: nothing played (%zu)", fake->played.size()));
        club_40_test::hide(ui);
        app.club_voice.forget(7, "");
        fs::remove(dir / "7.wav", ec);
        fs::remove(dir / "1001.wav", ec);
    });

    app.callname_player.set_player(nullptr);
    fs::remove_all(dir, ec);
}

}  // namespace cvplay_test
