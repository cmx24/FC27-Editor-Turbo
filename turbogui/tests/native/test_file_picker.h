// FC 27 LE Turbo GUI - native tests of the in-overlay file picker (ui/file_picker.cpp) and the Export / Import dialogs
// that use it (ui/ui_presets.cpp). Included by test_main.cpp after its Ui driver; run from test_ui().
#pragma once
#include "ui/file_picker.h"

static void test_file_picker_ui(App& app, Ui& ui, SimMemory& mem, uint64_t mailbox) {
    run_case("file picker: names, extensions, listing, folders made in-process, remembered folders", [&] {
        CHECK(safe_file_name("a<b>c:d\"e/f\\g|h?i*j") == "a_b_c_d_e_f_g_h_i_j", "Windows-reserved characters replaced");
        CHECK(safe_file_name("  name. . ") == "name", "leading spaces, trailing dots and spaces dropped");
        CHECK(safe_file_name("   ").empty(), "nothing left");
        CHECK(preset_safe_name("Rossi, Mattia") == "Rossi_Mattia", "Lua preset.safe_name: runs become one '_'");
        CHECK(preset_safe_name("__Saka__") == "Saka" && preset_safe_name("").compare("player") == 0, "trimmed, 'player' when empty");
        CHECK(preset_safe_name(std::string(80, 'x')).size() == 60, "at most 60 characters");
        CHECK(has_ext("a/B.CSV", {".csv", ".json"}) && !has_ext("a/b.txt", {".csv"}) && has_ext("a/b.txt", {}), "extension filter");

        fs::path d = g_out / "picker_world";
        fs::remove_all(d);
        std::string err;
        CHECK(ensure_folder(d / "x" / "y", &err), "nested folders created: " + err);
        CHECK(fs::is_directory(d / "x" / "y"), "exists");
        CHECK(ensure_folder(d / "x" / "y"), "existing folder is fine");
        std::ofstream(d / "x" / "y" / "blocker") << "file";
        CHECK(!ensure_folder(d / "x" / "y" / "blocker" / "z", &err) && !err.empty(), "a file in the way is reported");
        for (const char* f : {"b.csv", "A.json", "c.txt", "d.CSV"}) std::ofstream(d / f) << "1";
        fs::create_directories(d / "Zeta");
        std::vector<fs::path> dirs, files;
        list_folder(d, {".csv", ".json"}, dirs, files);
        CHECK(dirs.size() == 2 && dirs[0].filename() == "x" && dirs[1].filename() == "Zeta", "folders sorted case-insensitively");
        CHECK(files.size() == 3 && files[0].filename() == "A.json" && files[1].filename() == "b.csv" && files[2].filename() == "d.CSV",
              fmt("only the asked extensions, sorted (%zu)", files.size()));
        CHECK(existing_files({d / "b.csv", d / "nope.csv"}).size() == 1, "existing_files");

        // export clashes: one player by exact names, a list by "_<playerid>" suffix
        std::ofstream(d / "Saka_1001.json") << "{}";
        std::ofstream(d / "Other_1002.csv") << "x";
        std::ofstream(d / "Other_10020.csv") << "x";
        auto c1 = export_clashes(d, d, true, true, true, "Saka_1001", {});
        CHECK(c1.size() == 1 && c1[0].filename() == "Saka_1001.json", fmt("one player: its JSON (%zu)", c1.size()));
        CHECK(export_clashes(d, d, false, true, false, "Saka_1001", {}).empty(), "CSV only: no clash");
        auto c2 = export_clashes(d, d, true, true, false, "", {1001, 1002});
        CHECK(c2.size() == 2, fmt("list: Saka_1001.json and Other_1002.csv, not Other_10020.csv (%zu)", c2.size()));

        // remembered folders survive a reload of the store
        fs::path store = d / "gui_folders.json";
        fs::path keep = remembered_folder("export.json");
        folder_store(store);
        remember_folder("t.key", d / "x");
        CHECK(fs::exists(store), "store written");
        folder_store(d / "other.json");
        CHECK(remembered_folder("t.key").empty(), "another store knows nothing");
        folder_store(store);
        CHECK(remembered_folder("t.key") == d / "x", "read back from the file");
        fs::remove_all(d / "x");
        CHECK(remembered_folder("t.key").empty(), "a folder that is gone is not offered");
        folder_store(app.bridge.dir() / "gui_folders.json");   // back to the GUI's own store
        (void)keep;
    });

    // A command the GUI queued in the mailbox, read back and cancelled (like test_ui's command run)
    auto take = [&](const std::string& what) {
        json cmd;
        if (!app.busy()) {
            CHECK(false, "no command queued by " + what);
            return cmd;
        }
        cmd = json::parse(mem.read_cstr(mailbox + 0x20, 0x1000), nullptr, false);
        CHECK(ui.click("Cancel"), "Cancel after " + what);
        return cmd;
    };
    auto open_player = [&](int64_t pid) {
        app.request_tab = 0;
        ui.frames(2);
        if (!ui.click(std::to_string(pid), "##plist")) app.sel_player = pid;
        ui.frames(2);
    };

    run_case("UI: Export chooses folders and a file name inside the overlay, makes the folders, asks before replacing", [&] {
        fs::path dest = g_out / "picker_exports";
        fs::remove_all(dest);
        fs::create_directories(dest / "sub");
        open_player(1001);
        const PlayerRow* p = app.model.player(1001);
        CHECK(p != nullptr, "player 1001");
        if (!p) return;
        const std::string base = preset_safe_name(p->name) + "_1001";

        CHECK(ui.click("Export...", "##pedit"), "export dialog");
        CHECK(ui.click("This player", "##pexport"), "one player (the dialog keeps the last scope)");
        CHECK(ui.click("Browse...##pexjsonpick", "##pexport"), "JSON folder: Browse...");
        CHECK(ui.find("Use this folder##fp", "##pexjsonpick") != nullptr, "folder picker drawn in the overlay");
        CHECK(ui.find("Open in Explorer##fp", "##pexjsonpick") != nullptr, "Explorer only as its own button");
        CHECK(ui.type_into(ui.find("##fppath", "##pexjsonpick"), path_text(dest)), "type the folder");
        CHECK(ui.click("New folder##fp", "##pexjsonpick"), "new folder box");
        CHECK(ui.type_into(ui.find("##fpnewdir", "##pexjsonpick"), "made here"), "new folder name + Enter");
        CHECK(fs::is_directory(dest / "made here"), "folder made by Turbo itself");
        CHECK(ui.click("Up##fp", "##pexjsonpick"), "Up");
        CHECK(ui.type_into(ui.find("##fppath", "##pexjsonpick"), path_text(dest / "sub")), "open sub");
        CHECK(ui.click("Use this folder##fp", "##pexjsonpick"), "use it");
        CHECK(ui.find("Use this folder##fp", "##pexjsonpick") == nullptr, "picker closed");
        CHECK(ui.type_into(ui.find("CSV folder", "##pexport"), path_text(dest / "csv" / "deep")), "a CSV folder that does not exist yet");
        CHECK(ui.click("Export player", "##pexport"), "Export player");
        json cmd = take("Export player");
        const json& o = cmd["overrides"];
        CHECK(o.value("json_dir", "") == path_text(dest / "sub"), "json_dir = the picked folder: " + o.dump());
        CHECK(o.value("preset_dir", "") == path_text(dest / "csv" / "deep"), "preset_dir = the typed folder");
        CHECK(o.value("name", "") == base, "file name sent: " + o.value("name", ""));
        CHECK(fs::is_directory(dest / "csv" / "deep"), "missing folder created in-process before the command (no cmd.exe mkdir)");
        CHECK(remembered_folder("export.json") == dest / "sub", "JSON folder remembered");
        CHECK(fs::exists(app.bridge.dir() / "gui_folders.json"), "remembered in turbo_output\\gui_folders.json");

        // the same export again, with its JSON already there: nothing is sent until the user agrees
        std::ofstream(dest / "sub" / (base + ".json")) << "{}";
        CHECK(ui.click("Export...", "##pedit"), "export dialog again");
        CHECK(ui.click("Export player", "##pexport"), "Export player");
        ui.frames(2);
        CHECK(!app.busy(), "existing file: not sent yet");
        CHECK(ui.find("Replace and export", "##pexport") != nullptr, "replace confirmation");
        CHECK(ui.click("Back##pexport", "##pexport"), "Back");
        CHECK(ui.find("Export player", "##pexport") != nullptr && !app.busy(), "back to the dialog, nothing sent");
        CHECK(ui.click("Export player", "##pexport"), "Export player");
        ui.frames(2);  // the auto-resizing dialog grows one frame after the confirmation appears (clipped until then)
        CHECK(ui.click("Replace and export", "##pexport"), "replace");
        json again = take("Replace and export");
        CHECK(again["overrides"].value("name", "") == base, "sent after the confirmation");

        // folder and file name in one go (Save picker); it does not ask itself, the export lists the files it replaces
        CHECK(ui.click("Export...", "##pedit"), "export dialog, third time");
        CHECK(ui.click("Browse...##pexname", "##pexport"), "file name: Browse...");
        CHECK(ui.find("Save##fp", "##pexnamepick") != nullptr, "save picker");
        CHECK(ui.type_into(ui.find("##fppath", "##pexnamepick"), path_text(dest)), "folder");
        CHECK(ui.type_into(ui.find("File name##fpname", "##pexnamepick"), "My: Export"), "file name");
        CHECK(ui.click("Save##fp", "##pexnamepick"), "Save");
        CHECK(ui.click("Export player", "##pexport"), "Export player");
        json named = take("Export player (Save picker)");
        CHECK(named["overrides"].value("name", "") == "My_ Export", "name from the picker, made safe: " + named["overrides"].value("name", ""));
        CHECK(named["overrides"].value("json_dir", "") == path_text(dest), "its folder became the JSON folder");
    });

    run_case("UI: Repair names... checks first, then repairs (player_presets repair_names)", [&] {
        open_player(1001);
        CHECK(ui.click("Repair names...", "##pedit"), "Repair names... button next to the preset buttons");
        CHECK(ui.find("Check##prepair", "##prepair") != nullptr, "the dialog explains and offers Check");
        CHECK(ui.click("Check##prepair", "##prepair"), "Check");
        json check = take("Repair names (check)");
        CHECK(check.value("module", "") == "player_presets" && check["overrides"].value("mode", "") == "repair_names" &&
                  check["overrides"].value("check", false),
              "check only: " + check.dump());
        CHECK(ui.click("Repair names...", "##pedit"), "dialog again");
        CHECK(ui.click("Repair names##prepair", "##prepair"), "Repair names");
        json fix = take("Repair names");
        CHECK(fix["overrides"].value("mode", "") == "repair_names" && !fix["overrides"].contains("check"), "repair writes: " + fix.dump());
        CHECK(ui.click("Repair names...", "##pedit"), "dialog a third time");
        CHECK(ui.click("Cancel##prepair", "##prepair"), "Cancel");
        ui.frames(2);
        CHECK(!app.busy(), "Cancel sends nothing");
    });

    run_case("UI: Import's Browse... is the in-overlay picker (CSV / JSON only)", [&] {
        fs::path dir = g_out / "picker_import";
        fs::remove_all(dir);
        fs::create_directories(dir);
        {
            std::ofstream f(dir / "pick_me.csv", std::ios::binary);
            f << "playerid,firstname,surname,playerjerseyname,commonname,overallrating,potential,preferredposition1\r\n"
              << "1001,Pre,Set,PRESET,,77,88,25\r\n";
        }
        std::ofstream(dir / "not_me.txt") << "x";
        open_player(1001);
        CHECK(ui.click("Import...", "##pedit"), "import dialog");
        CHECK(ui.click("This player: " + app.model.player(1001)->name + " (ID 1001)", "##pimport"), "onto this player");
        CHECK(ui.click("Browse...##preset", "##pimport"), "Browse...");
        CHECK(ui.type_into(ui.find("##fppath", "##pfbrowser"), path_text(dir)), "folder");
        CHECK(ui.find("not_me.txt", "##pfbrowser") == nullptr, "other files hidden");
        CHECK(ui.click("pick_me.csv", "##pfbrowser"), "click the preset");
        CHECK(ui.find("##fppath", "##pfbrowser") == nullptr, "picker closed");
        CHECK(ui.click("Import onto player", "##pimport"), "import");
        json cmd = take("Import onto player");
        CHECK(cmd["overrides"].value("file", "") == path_text(dir / "pick_me.csv"), "picked file imported: " + cmd.dump());
        CHECK(remembered_folder("import.preset") == dir, "import folder remembered");
    });
}
