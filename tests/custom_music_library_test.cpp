#include "kh3ap/features/music/audio/custom_music_library.h"
#include "kh3ap/bridge/serialization/json_limits.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main()
{
    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() / "kh3ap_library_test";
    fs::remove_all(folder);
    fs::create_directories(folder);
    for (const char* name : {"BGM_attraction.ogg", "my_song.ogg", "my_song.wav",
                             "Form_UltimaWeapon.ogg", "BGM_he_battle.wav"})
    {
        std::ofstream{folder / name} << "x";
    }
    std::ofstream{folder / "ignored.mp3"} << "x";
    const auto automatic = kh3ap::load_custom_music_library(folder);
    assert(automatic.tracks.size() == 5 && automatic.pools().tracks.size() == 5);
    assert(automatic.problems.empty());
    // The same stem in both supported formats must not silently lose a song.
    assert(automatic.tracks.at(L"my_song.ogg").file.extension() == L".ogg");
    assert(automatic.tracks.at(L"my_song.wav").file.extension() == L".wav");
    for (const auto& entry : automatic.pools().tracks)
    {
        assert(entry.field && entry.battle && entry.worlds.empty() &&
               automatic.for_table_entry(entry.entry));
    }
    std::ofstream{folder / "custom_music.json"}
        << R"({"tracks": [{"file": "my_song.ogg", "replaces": ["GoA_01", "BGM_ra_field"]},)"
        << R"( {"replaces": ["BGM_he_battle", "Form_Oblivion", "he_01"], "vanilla": true},)"
        << R"( {"vanilla": true}]})";

    const kh3ap::CustomMusicLibrary library = kh3ap::load_custom_music_library(folder);
    for (const auto& problem : library.problems)
    {
        std::wcout << L"problem: " << problem << L"\n";
    }
    // Files by name, and manifest targets, by short name or object path.
    assert(library.for_target(L"BGM_attraction"));
    assert(library.for_target(L"/Game/Sound/BGM/BGM_attraction.BGM_attraction"));
    assert(library.for_target(L"form_ultimaweapon"));
    assert(library.for_target(L"GoA_01") && library.for_target(L"BGM_ra_field"));
    assert(library.for_target(L"GoA_01")->file.extension() == L".ogg");
    assert(library.pools().tracks.size() == automatic.pools().tracks.size());
    // Vanilla entries: kept, and they win over the file of the same name.
    assert(library.keeps_vanilla(L"/Game/Sound/BGM/BGM_he_battle.BGM_he_battle"));
    assert(!library.for_target(L"BGM_he_battle"));
    assert(library.overrides(L"BGM_he_battle"));
    assert(library.keeps_vanilla(L"Form_Oblivion") && library.keeps_vanilla(L"HE_01"));
    assert(!library.overrides(L"BGM_ts_field"));
    assert(library.problems.size() == 2);
    const fs::path default_folder = folder / "mod/custom_music";
    const fs::path settings = default_folder.parent_path() / "custom_music_directory.txt";
    fs::create_directories(default_folder);
    assert(kh3ap::configured_custom_music_directory(default_folder) == default_folder);
    std::ofstream{settings} << "relative/path";
    assert(kh3ap::configured_custom_music_directory(default_folder) == default_folder);
    const auto selected = folder / fs::path{u8"My Music 音楽 🎵"};
    const auto encoded = selected.u8string();
    {
        std::ofstream stream{settings, std::ios::binary};
        stream << "\xEF\xBB\xBF";
        stream.write(reinterpret_cast<const char*>(encoded.data()),
                     static_cast<std::streamsize>(encoded.size()));
        stream << "\r\n";
    }
    assert(kh3ap::configured_custom_music_directory(default_folder) == selected);
    fs::create_directories(selected);
    for (const auto& entry : fs::directory_iterator{folder})
    {
        if (entry.is_regular_file())
        {
            fs::copy_file(entry.path(), selected / entry.path().filename());
        }
    }
    const auto relocated = kh3ap::load_custom_music_library(selected);
    assert(relocated.tracks.size() == library.tracks.size());
    assert(relocated.signature != library.signature);
    assert(relocated.for_target(L"GoA_01")->file.parent_path() == selected);
    std::ofstream{settings, std::ios::trunc};
    assert(kh3ap::configured_custom_music_directory(default_folder) == default_folder);
    assert(kh3ap::configured_custom_music_directory({}).empty());
    assert(!kh3ap::write_custom_music_targets({}, {}));
    // Reject oversized manifests before reading or parsing their contents.
    {
        std::ofstream manifest{folder / "custom_music.json", std::ios::binary | std::ios::trunc};
        manifest.seekp(kh3ap::json::limits::message_bytes);
        manifest.put(' ');
    }
    const auto oversized = kh3ap::load_custom_music_library(folder);
    assert(!oversized.for_target(L"GoA_01"));
    assert(oversized.tracks.size() == automatic.tracks.size());
    fs::remove_all(folder);
    std::cout << "Custom music library tests passed\n";
}
