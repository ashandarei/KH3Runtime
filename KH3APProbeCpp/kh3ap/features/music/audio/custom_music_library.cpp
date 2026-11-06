#include <optional>
#include "kh3ap/features/music/audio/custom_music_library.h"

#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/serialization/json_limits.h"
#include "kh3ap/features/music/shuffle/form_theme.h"

#include <algorithm>
#include <cwctype>
#include <format>
#include <fstream>
#include <system_error>

namespace kh3ap
{
    namespace
    {
        constexpr std::string_view level_list_indent = "  ";

        constexpr auto max_custom_track_volume = 2.0;
        constexpr std::wstring_view manifest_name = L"custom_music.json";

        auto lower(std::wstring text) -> std::wstring
        {
            for (wchar_t& ch : text)
            {
                ch = static_cast<wchar_t>(std::towlower(ch));
            }
            return text;
        }

        // "/Game/Sound/BGM/BGM_he_field.BGM_he_field" and "BGM_he_field"
        // name the same target.
        auto target_key(std::wstring_view target) -> std::wstring
        {
            const size_t dot = target.rfind(L'.');
            return lower(
                std::wstring{dot == std::wstring_view::npos ? target : target.substr(dot + 1)});
        }

        auto track_key(const std::filesystem::path& file) -> std::wstring
        {
            return lower(file.filename().wstring());
        }

        auto read_text(const std::filesystem::path& file) -> std::optional<std::string>
        {
            std::ifstream stream{file, std::ios::binary | std::ios::ate};
            if (!stream)
            {
                return std::nullopt;
            }
            const auto size = stream.tellg();
            if (size < 0 || size > static_cast<std::streamoff>(json::limits::message_bytes))
            {
                return std::nullopt;
            }
            std::string text(static_cast<size_t>(size), '\0');
            stream.seekg(0);
            if (!stream.read(text.data(), static_cast<std::streamsize>(text.size())))
            {
                return std::nullopt;
            }
            return text;
        }

        auto apply_manifest(CustomMusicLibrary& library, const std::filesystem::path& folder)
            -> void
        {
            const auto text = read_text(folder / manifest_name);
            if (!text)
            {
                return;
            }
            library.signature += L"|manifest:" + std::to_wstring(std::hash<std::string>{}(*text));
            for (const std::string& entry : JsonFieldReader::object_array_field(*text, "tracks"))
            {
                if (JsonFieldReader::bool_field(entry, "vanilla").value_or(false))
                {
                    const auto kept = JsonFieldReader::string_array_field(entry, "replaces");
                    if (kept.empty())
                    {
                        library.problems.push_back(L"vanilla entry without \"replaces\"");
                    }
                    for (const std::wstring& target : kept)
                    {
                        library.vanilla_targets.insert(target_key(target));
                    }
                    continue;
                }
                const auto file = JsonFieldReader::string_field(entry, "file");
                const auto found =
                    file ? library.tracks.find(track_key(*file)) : library.tracks.end();
                if (found == library.tracks.end())
                {
                    library.problems.push_back(file ? L"missing file " + *file
                                                    : std::wstring{L"entry without \"file\""});
                    continue;
                }
                CustomMusicTrack& track = found->second;
                for (const std::wstring& target :
                     JsonFieldReader::string_array_field(entry, "replaces"))
                {
                    library.targets[target_key(target)] = found->first;
                }
                track.loop_start = JsonFieldReader::number_field(entry, "loop_start");
                track.loop_end = JsonFieldReader::number_field(entry, "loop_end");
                if (const auto volume = JsonFieldReader::number_field(entry, "volume"))
                {
                    track.volume =
                        static_cast<float>(std::clamp(*volume, 0.0, max_custom_track_volume));
                }
            }
            // Keeping the game's music is the explicit choice: it wins over
            // a file for the same target.
            for (const std::wstring& target : library.vanilla_targets)
            {
                if (library.targets.erase(target) > 0)
                {
                    library.problems.push_back(L"\"" + target +
                                               L"\" has a file and a vanilla entry: vanilla wins");
                }
            }
        }
    } // namespace

    auto CustomMusicLibrary::for_target(std::wstring_view target) const -> const CustomMusicTrack*
    {
        const auto found = targets.find(target_key(target));
        if (found == targets.end())
        {
            return nullptr;
        }
        const auto track = tracks.find(found->second);
        return track != tracks.end() ? &track->second : nullptr;
    }

    auto CustomMusicLibrary::keeps_vanilla(std::wstring_view target) const -> bool
    {
        return vanilla_targets.contains(target_key(target));
    }

    auto CustomMusicLibrary::override_target(std::wstring_view track, std::wstring_view level) const
        -> std::wstring
    {
        for (const auto target : {level, track})
        {
            if (!target.empty() && overrides(target))
            {
                return std::wstring{target};
            }
        }
        const std::wstring level_code = lower(std::wstring{level.substr(0, level.find(L'_'))});
        const std::wstring key = target_key(track);
        for (const BgmShuffleWorld& world : bgm_shuffle_worlds())
        {
            const auto contains_track = [&key](const auto& tracks)
            {
                return std::ranges::any_of(tracks,
                                           [&key](std::wstring_view candidate)
                                           {
                                               return target_key(candidate) == key;
                                           });
            };
            const bool battle = level.empty() && contains_track(world.battles);
            const bool matches = !level.empty() ? lower(std::wstring{world.code}) == level_code
                                                : battle || contains_track(world.fields);
            if (!matches)
            {
                continue;
            }
            const std::wstring scope = battle ? L"_battles" : L"_exploration";
            for (const std::wstring& target :
                 {std::wstring{world.code} + scope, std::wstring{world.name} + scope,
                  std::wstring{world.code}, std::wstring{world.name}})
            {
                if (overrides(target))
                {
                    return target;
                }
            }
            break;
        }
        return {};
    }

    auto CustomMusicLibrary::for_table_entry(std::wstring_view entry) const
        -> const CustomMusicTrack*
    {
        if (!is_custom_bgm_track(entry))
        {
            return nullptr;
        }
        const auto track = tracks.find(std::wstring{entry.substr(bgm_custom_track_prefix.size())});
        return track != tracks.end() ? &track->second : nullptr;
    }

    auto CustomMusicLibrary::pools() const -> BgmCustomPools
    {
        BgmCustomPools pools{};
        for (const auto& [name, track] : tracks)
        {
            pools.tracks.push_back({.entry = std::wstring{bgm_custom_track_prefix} + name});
        }
        // The draw must not depend on the map's iteration order.
        std::ranges::sort(pools.tracks, {}, &BgmCustomPoolTrack::entry);
        return pools;
    }

    auto write_custom_music_targets(const std::filesystem::path& folder,
                                    const std::vector<std::wstring>& visited_levels) -> bool
    {
        if (folder.empty())
        {
            return false;
        }
        std::error_code error{};
        std::filesystem::create_directories(folder, error);
        std::wstring text =
            L"KH3 AP custom music targets\n"
            L"===========================\n"
            L"\n"
            L"Put WAV or OGG files in this folder. With Include Custom Music ON in\n"
            L"Music Rando, every file joins the CHAOS shuffle pool without a JSON file.\n"
            L"The toggle only affects CHAOS; explicit replacements work in every mode.\n"
            L"The library is scanned once at game startup. Restart KH3 after changing\n"
            L"the folder, adding/removing tracks, or editing custom_music.json.\n"
            L"\n"
            L"A file named after a target replaces\n"
            L"it (he_01.ogg, BGM_he_battle.wav) where the game normally plays it, in\n"
            L"every mode, over the music shuffle and the form themes. custom_music.json\n"
            L"can list explicit targets per file, loop points and a volume,\n"
            L"and keep the game's own music for some targets:\n"
            L"\n"
            L"  {\"tracks\": [{\"file\": \"my_song.ogg\", \"replaces\": [\"GoA_01\", "
            L"\"BGM_he_field\"],\n"
            L"               \"loop_start\": 12.5, \"loop_end\": 184.0, \"volume\": 0.8},\n"
            L"              {\"replaces\": [\"BGM_he_battle\", \"Form_Oblivion\"], \"vanilla\": "
            L"true}]}\n"
            L"\n"
            L"\"vanilla\": true (no file) keeps a track out of the music shuffle (form\n"
            L"themes still play over it; a file holds them back); on a Form_ target it\n"
            L"turns that form's theme off.\n"
            L"\n"
            L"Names are not case sensitive. This file is rewritten by the mod; edits to\n"
            L"it are lost.\n"
            L"\n"
            L"Tracks: replaced where the game plays them, whatever the shuffle\n"
            L"-----------------------------------------------------------------\n";
        for (const BgmShuffleWorld& world : bgm_shuffle_worlds())
        {
            text += std::format(L"\n{} ({})\n", world.name, world.code);
            const auto list =
                [&text](const std::vector<std::wstring_view>& tracks, std::wstring_view role)
            {
                for (size_t index = 0; index < tracks.size(); ++index)
                {
                    const std::wstring_view track = tracks[index];
                    const std::wstring_view name = track.substr(track.rfind(L'.') + 1);
                    text += std::format(L"  {:<22}{}{}\n", name, role,
                                        tracks.size() > 1 ? std::format(L" (variant {})", index + 1)
                                                          : std::wstring{});
                }
            };
            list(world.fields, L"field");
            list(world.battles, L"battle");
        }
        text += L"\n"
                L"Keyblade forms: while Sora is in that keyblade's form, with or without\n"
                L"Form Themes\n"
                L"---------------------------------------------------------------------\n"
                L"\n";
        for (const FormCustomTarget& target : form_custom_targets())
        {
            text += std::format(L"  {:<22}{}\n", target.target, target.name);
        }
        text += L"\n"
                L"Levels: that level's field music\n"
                L"--------------------------------\n"
                L"\n"
                L"Worlds, by code or name (listed above): \"he\" or \"Olympus\" covers all of\n"
                L"that world's music, \"he_battles\" its battle music, \"he_exploration\"\n"
                L"the rest (the field music of all its levels). The most specific target\n"
                L"wins: a level or a track over its world's scope, a scope over the world.\n"
                L"\n"
                L"  GoA_01                Garden of Assemblage\n";
        for (const std::wstring& level : visited_levels)
        {
            if (lower(level) != L"goa_01")
            {
                text += L"  " + level + L"\n";
            }
        }
        text += L"\nLevels appear here as you visit them, while Music Shuffle is on or\n"
                L"this folder holds at least one music file.\n";

        const std::filesystem::path file = folder / L"TARGETS.txt";
        std::ofstream stream{file, std::ios::binary | std::ios::trunc};
        if (!stream)
        {
            return false;
        }
        // UTF-8, as the names are ASCII.
        for (const wchar_t ch : text)
        {
            stream.put(static_cast<char>(ch));
        }
        return static_cast<bool>(stream);
    }

    auto read_custom_music_visited_levels(const std::filesystem::path& folder)
        -> std::vector<std::wstring>
    {
        std::vector<std::wstring> levels{};
        if (folder.empty())
        {
            return levels;
        }
        const auto text = read_text(folder / L"TARGETS.txt");
        const size_t section = text ? text->find("Levels: ") : std::string::npos;
        if (section == std::string::npos)
        {
            return levels;
        }
        size_t line_start = text->find('\n', section);
        while (line_start != std::string::npos)
        {
            const size_t line_end = text->find('\n', line_start + 1);
            const std::string line = text->substr(line_start + 1, line_end - line_start - 1);
            line_start = line_end;
            // Level lines are indented; the header rule and notes are not.
            if (!line.starts_with(level_list_indent))
            {
                continue;
            }
            const size_t name_end = line.find(' ', level_list_indent.size());
            const std::string name =
                line.substr(level_list_indent.size(), name_end == std::string::npos
                                                          ? std::string::npos
                                                          : name_end - level_list_indent.size());
            if (!name.empty() && lower(std::wstring{name.begin(), name.end()}) != L"goa_01")
            {
                levels.emplace_back(name.begin(), name.end());
            }
        }
        return levels;
    }

    auto configured_custom_music_directory(const std::filesystem::path& default_folder)
        -> std::filesystem::path
    {
        if (default_folder.empty())
        {
            return {};
        }
        auto text = read_text(default_folder.parent_path() / "custom_music_directory.txt");
        if (!text)
        {
            return default_folder;
        }
        if (text->starts_with("\xEF\xBB\xBF"))
        {
            text->erase(0, std::string_view{"\xEF\xBB\xBF"}.size());
        }
        while (!text->empty() && (text->back() == '\n' || text->back() == '\r'))
        {
            text->pop_back();
        }
        if (text->empty() || text->find('\0') != std::string::npos)
        {
            return default_folder;
        }
        try
        {
            const auto selected = std::filesystem::path{std::u8string{text->begin(), text->end()}};
            return selected.is_absolute() ? selected : default_folder;
        }
        catch (const std::filesystem::filesystem_error&)
        {
            return default_folder;
        }
    }

    auto load_custom_music_library(const std::filesystem::path& folder) -> CustomMusicLibrary
    {
        CustomMusicLibrary library{};
        if (folder.empty())
        {
            return library;
        }
        // Identical file names and sizes in a new folder still need new paths.
        library.signature = folder.wstring();
        std::error_code error{};
        std::vector<std::filesystem::path> files{};
        for (const auto& entry : std::filesystem::directory_iterator{folder, error})
        {
            const std::wstring extension = lower(entry.path().extension().wstring());
            if (entry.is_regular_file(error) && (extension == L".wav" || extension == L".ogg"))
            {
                files.push_back(entry.path());
            }
        }
        std::ranges::sort(files);
        for (const auto& file : files)
        {
            const std::wstring name = track_key(file);
            library.tracks[name].file = file;
            // A file named after a target replaces it.
            library.targets[lower(file.stem().wstring())] = name;
            library.signature += std::format(L"|{}:{}", file.filename().wstring(),
                                             std::filesystem::file_size(file, error));
        }
        apply_manifest(library, folder);
        return library;
    }
} // namespace kh3ap
