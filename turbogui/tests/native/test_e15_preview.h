// Native tests (5.0c E15-preview, Cassio 10-30: "team audio needs preview buttons"; briefs\5.0c\E15-teamaudio.md Part B). The Commentary name card
// lists the clubs the loaded commentary language has a recording for; every club row now has the shared play button (ui_callname_play.cpp
// callname_play_button, kind Club, id "##play_c<club id>"). Its wav is <turbo_output>\cache\teamaudio\<lang>\<club id>.wav (club_clips.py --decode),
// the loaded language only. A row with no wav shows a DISABLED button whose tooltip is "No FC 27 audio for this name in <lang>".
// Behavioural: the cases draw the real page (club_40_test::show) with a fake WavPlayer, so the old card (no play button on the row) FAILS by assertion.
//   a  each club row has a play button "##play_c<id>"; the "No club name" row has none
//   b  a click on a row whose wav is in the language's folder plays exactly that file; a click on a row without one plays nothing
//   c  pure: the Club kind's state (disabled + tooltip, enabled, key never shared with Generic, one language folder)
// Included by test_main.cpp after test_e15_donor_list.h; e15p_test::ui_cases(app, ui, le) is called right after e15_test::ui_cases.
#pragma once
#include <set>

namespace e15p_test {

using namespace turbo;

static const char* kLang = "eng_e15p";  // its own name: the picker caches the list per language, the audio folder is per language

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
    {  // club 1001 has its clip; 1002 does not
        std::ofstream w((dir / "1001.wav").string(), std::ios::binary);
        w << "RIFF....WAVEfmt ";
    }
    {
        const fs::path cdir = app.bridge.root() / "turbo" / "callnames" / "clubs";
        fs::create_directories(cdir, ec);
        json j;
        j["lang"] = kLang;
        j["ids"] = std::vector<int>{1001, 1002, 1003};
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
        e15_test::reveal(ui, "Save##cvsave");
        ui.frames(2);
    };

    run_case("E15 preview a: every club row has a play button (##play_c<id>); the 'No club name' row has none", [&] {
        open_card();
        CHECK(ui.find("##play_c1001") != nullptr, "club 1001 (has a clip) has a play button");
        CHECK(ui.find("##play_c1002") != nullptr, "club 1002 (no clip) has a play button too (disabled, with the reason)");
        CHECK(ui.find("##play_c1003") != nullptr, "club 1003 has a play button");
        CHECK(ui.find("##play_c0") == nullptr, "the 'No club name' row has no play button");
        club_40_test::hide(ui);
    });

    run_case("E15 preview b: a click plays <lang folder> + id.wav; a row without a wav plays nothing", [&] {
        open_card();
        fake->played.clear();
        CHECK(ui.click("##play_c1002"), "disabled button pressed (nothing may happen)");
        ui.frames(2);
        CHECK(fake->played.empty(), "club 1002 has no wav: nothing played");
        CHECK(ui.click("##play_c1001"), "play pressed on club 1001");
        ui.frames(2);
        CHECK(fake->played.size() == 1, fmt("one wav started (%zu)", fake->played.size()));
        if (!fake->played.empty()) {
            const std::string p = fake->played.front();
            CHECK(p.find("teamaudio") != std::string::npos && p.find(kLang) != std::string::npos && p.size() > 8 && p.compare(p.size() - 8, 8, "1001.wav") == 0,
                  "the wav is in the loaded language's folder: " + p);
        }
        club_40_test::hide(ui);
    });

#ifdef TURBO_E15_CLUB_AUDIO
    run_case("E15 preview c: the Club kind - disabled with the reason, enabled with a wav, its own state, one language folder", [&] {
        MasterAudio a;
        a.wav_dir = dir.u8string();
        a.lang = kLang;
        MasterAudio other;
        other.wav_dir = (app.turbo_output() / "cache" / "teamaudio" / "eng_other").u8string();
        other.lang = "eng_other";
        CallnamePlayer pl;
        std::set<std::string> files = {(dir / "1001.wav").u8string()};
        pl.set_exists([&](const fs::path& p) { return files.count(p.u8string()) > 0; });
        pl.set_seconds([](const fs::path&) { return 1.0; });
        auto fk = std::make_shared<FakeWav>();
        pl.set_player(fk);
        CallnamePlayer::Button b = pl.button(a, CallnameAudioKind::Club, 1002, 0.0);
        CHECK(!b.enabled && b.tip == std::string("No FC 27 audio for this name in ") + kLang, "no wav: disabled with the reason: " + b.tip);
        b = pl.button(a, CallnameAudioKind::Club, 1001, 0.0);
        CHECK(b.enabled && !b.playing, "a wav in the folder: enabled: " + b.tip);
        b = pl.button(other, CallnameAudioKind::Club, 1001, 0.0);
        CHECK(!b.enabled && b.tip == "No FC 27 audio for this name in eng_other", "another language's folder has no 1001.wav: disabled: " + b.tip);
        CHECK(!pl.click(a, CallnameAudioKind::Club, 1001, 0.0).empty() && fk->played.size() == 1, "click plays the wav");
        CHECK(pl.playing(CallnameAudioKind::Club, 1001, 0.5) && !pl.playing(CallnameAudioKind::Generic, 1001, 0.5) && !pl.playing(CallnameAudioKind::Own, 1001, 0.5),
              "Club 1001 playing is not Generic 1001 or Own 1001");
        MasterAudio none;
        none.lang = kLang;
        b = pl.button(none, CallnameAudioKind::Club, 1001, 0.0);
        CHECK(!b.enabled && b.tip == std::string("No FC 27 audio for this name in ") + kLang, "no folder at all: the same reason: " + b.tip);
    });
#else
    run_case("E15 preview c: the Club kind - disabled with the reason, enabled with a wav, its own state, one language folder",
             [&] { CHECK(false, "no CallnameAudioKind::Club in this tree"); });
#endif
    app.callname_player.set_player(nullptr);
    fs::remove_all(dir, ec);
}

}  // namespace e15p_test
