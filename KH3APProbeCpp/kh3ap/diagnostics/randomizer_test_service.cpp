#include "kh3ap/bridge/transport/request_file_watcher.h"
#include <memory>
#include <optional>
#include <utility>
#include "kh3ap/core/settings/seed_gameplay_options.h"
#include "kh3ap/diagnostics/randomizer_test_service.h"
#include "kh3ap/core/settings/goa_settings_service.h"
#include "kh3ap/features/music/shuffle/bgm_shuffle_service.h"
#include "kh3ap/features/music/shuffle/bgm_shuffle_table.h"
#include "kh3ap/features/colors/color_service.h"
#include "kh3ap/features/colors/color_material_ops.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/music/events/sead_bgm.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <deque>
#include <sstream>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr auto max_randomizer_test_request_lifetime_ms = 120000;
        constexpr auto max_test_material_slots = 32;
        constexpr auto randomizer_test_lease_duration = std::chrono::seconds(90);
        constexpr auto randomizer_test_update_interval = std::chrono::milliseconds(100);
        constexpr auto randomizer_restart_timeout = std::chrono::seconds(10);
        constexpr auto randomizer_recovery_lease_duration = std::chrono::seconds(10);
        constexpr auto randomizer_request_history_capacity = 128;
        using Clock = std::chrono::steady_clock;
        constexpr auto request_name = L"runtime_randomizer_test_request.json";
        constexpr auto result_name = L"runtime_randomizer_test_result.json";
        constexpr auto events_name = L"runtime_randomizer_test_events.jsonl";
        auto unix_ms() -> int64_t
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        }

        // Reads existing materials only; inspection never creates a MID.
        auto material_snapshot(UObject* pawn) -> std::string
        {
            std::ostringstream out;
            out << '[';
            bool comma = false;
            if (is_uobject_valid_fast(pawn))
            {
                for (UObject* mesh : collect_mesh_components(pawn, resolve_mesh_component_class()))
                {
                    for (int slot = 0; slot < std::min(read_num_materials(mesh).value_or(0),
                                                       max_test_material_slots);
                         ++slot)
                    {
                        UObject* const material = read_material_at(mesh, slot);
                        if (!is_uobject_valid_fast(material))
                        {
                            continue;
                        }
                        if (comma)
                        {
                            out << ',';
                        }
                        comma = true;
                        out << "{\"mesh\":\"" << json_escape(mesh->GetName())
                            << "\",\"slot\":" << slot << ",\"material\":\""
                            << json_escape(material->GetPathName()) << "\",\"class\":\""
                            << json_escape(material->GetClassPrivate()->GetName())
                            << "\",\"vectors\":{";
                        bool vector_comma = false;
                        // K2_GetVectorParameterValue belongs to MID, not its
                        // constant-material parent. Off mode restores MICs.
                        auto names =
                            material->GetClassPrivate()->GetName() == L"MaterialInstanceDynamic"
                                ? material_parameter_names(material)
                                : std::vector<std::wstring>{};
                        std::sort(names.begin(), names.end());
                        for (const auto& name : names)
                        {
                            if (const auto value = read_vector_parameter_from_mid(material, name))
                            {
                                if (vector_comma)
                                {
                                    out << ',';
                                }
                                vector_comma = true;
                                out << '"' << json_escape(name) << "\":[";
                                for (size_t i = 0; i < value->size(); ++i)
                                {
                                    if (i)
                                    {
                                        out << ',';
                                    }
                                    out << (*value)[i];
                                }
                                out << ']';
                            }
                        }
                        out << "}}";
                    }
                }
            }
            out << ']';
            return out.str();
        }
    } // namespace

    struct RuntimeRandomizerTestService::Impl
    {
        RuntimeRandomizerTestConfig config;
        RequestFileWatcher requests{request_name};
        std::optional<std::filesystem::path> dir;
        std::wstring id, command, session, requested_session, fixture_level;
        std::deque<std::wstring> consumed;
        RuntimeGoaSettingsStatus original{};
        FWeakObjectPtr fixture_controller{};
        FWeakObjectPtr volume_instance{};
        std::optional<sead_bgm::VolumeSettings> original_volume{};
        std::vector<std::pair<FWeakObjectPtr, uint8_t>> held;
        Clock::time_point next_poll{}, lease{};
        bool restart_pending{};
        Clock::time_point restart_deadline{};
        int64_t expires{};

        explicit Impl(RuntimeRandomizerTestConfig value) : config(std::move(value))
        {
        }

        void stop_fixture()
        {
            restart_pending = false;
            UObject* const controller = fixture_controller.Get();
            UObject* const pawn = config.sora();
            if (is_uobject_valid_fast(controller) && is_uobject_valid_fast(pawn) &&
                sead_bgm::current_level_name(pawn) == fixture_level)
            {
                (void)sead_bgm::test_request_stop(controller);
            }
            for (const auto& [weak, behaviour] : held)
            {
                if (UObject* const slot = weak.Get(); is_uobject_valid_fast(slot))
                {
                    (void)sead_bgm::set_store_behaviour(slot, behaviour);
                }
            }
            held.clear();
            fixture_controller = nullptr;
        }

        bool restore()
        {
            stop_fixture();
            if (original_volume)
            {
                if (!sead_bgm::test_volume_settings(volume_instance.Get(), *original_volume))
                {
                    return false;
                }
                original_volume.reset();
                volume_instance = {};
            }
            if (session.empty())
            {
                return true;
            }
            const bool music = config.settings.set_music_shuffle_mode(original.music_shuffle_mode);
            const bool color =
                config.settings.set_color_randomization_mode(original.color_randomization_mode);
            const bool form = config.settings.set_form_themes_enabled(original.form_themes_enabled);
            const bool custom =
                config.settings.set_include_custom_music(original.include_custom_music);
            const bool song = config.settings.set_song_display(original.song_display);
            if (music && color && form && custom && song)
            {
                session.clear();
            }
            return music && color && form && custom && song;
        }

        void report(std::wstring_view status, std::wstring_view reason)
        {
            if (!dir)
            {
                return;
            }
            UObject* const pawn = config.sora();
            UObject* material_pawn = character_swap::reward_context();
            if (!material_pawn)
            {
                material_pawn = pawn;
            }
            const auto settings = config.settings.status();
            std::ostringstream out;
            out << std::boolalpha << "{\"schema\":\"kh3ap.randomizer_test.v1\",\"request_id\":\""
                << json_escape(id) << "\",\"command\":\"" << json_escape(command)
                << "\",\"session_id\":\"" << json_escape(requested_session)
                << "\",\"process_id\":" << GetCurrentProcessId()
                << ",\"expires_unix_ms\":" << expires << ",\"status\":\"" << json_escape(status)
                << "\",\"reason\":\"" << json_escape(reason) << "\",\"at\":\"" << now_iso8601_utc()
                << "\",\"session_active\":" << !session.empty()
                << ",\"gameplay\":" << config.gameplay() << ",\"pawn\":\""
                << json_escape(is_uobject_valid_fast(pawn) ? pawn->GetFullName() : L"")
                << "\",\"material_pawn\":\""
                << json_escape(is_uobject_valid_fast(material_pawn) ? material_pawn->GetFullName() : L"")
                << "\",\"settings\":{\"music\":" << settings.music_shuffle_mode
                << ",\"color\":" << settings.color_randomization_mode
                << ",\"forms\":" << settings.form_themes_enabled
                << ",\"include_custom_music\":" << settings.include_custom_music
                << ",\"song_display\":" << settings.song_display
                << "},\"music\":" << config.music.test_snapshot()
                << ",\"now_playing\":" << (config.now_playing ? config.now_playing() : "{}")
                << ",\"materials\":" << material_snapshot(material_pawn)
                << ",\"gummi\":" << config.colors.test_gummi_snapshot() << "}\n";
            write_text_file_atomic_verified(*dir / result_name, out.str());
            append_text_file(*dir / events_name, out.str());
        }

        void apply_settings(const std::string& text)
        {
            const auto music = JsonFieldReader::int64_field(text, "music");
            const auto color = JsonFieldReader::int64_field(text, "color");
            const auto forms = JsonFieldReader::bool_field(text, "forms");
            const auto custom = JsonFieldReader::bool_field(text, "include_custom_music");
            const auto song = JsonFieldReader::bool_field(text, "song_display");
            if (!config.gameplay() ||
                (music && (*music < 0 || *music > max_seed_music_shuffle_mode)) ||
                (color && (*color < 0 || *color > seed_color_mode_wild)))
            {
                throw std::runtime_error("invalid_settings_or_gameplay");
            }
            if ((music && !config.settings.set_music_shuffle_mode(static_cast<int32_t>(*music))) ||
                (color &&
                 !config.settings.set_color_randomization_mode(static_cast<int32_t>(*color))) ||
                (forms && !config.settings.set_form_themes_enabled(*forms)) ||
                (custom && !config.settings.set_include_custom_music(*custom)) ||
                (song && !config.settings.set_song_display(*song)))
            {
                throw std::runtime_error("settings_set_failed");
            }
        }
        void apply_play_group(const std::string& text)
        {
            UObject* const pawn = config.sora();
            if (!config.gameplay() || !is_uobject_valid_fast(pawn) || fixture_controller.Get())
            {
                throw std::runtime_error("group_fixture_requires_playable_sora");
            }
            const auto managers = sead_bgm::find_live_objects(L"SQEXSEADBGMManager");
            UObject* const manager = managers.empty() ? nullptr : managers.front();
            UObject* const boss = sead_bgm::controller_for_slot(manager, L"Boss");
            const auto state = sead_bgm::slot_state(boss);
            if (!is_uobject_valid_fast(boss) || !state ||
                (*state != sead_bgm::slot_state_stop && *state != sead_bgm::slot_state_finished &&
                 *state != sead_bgm::slot_state_invalid &&
                 *state != sead_bgm::slot_state_auto_play))
            {
                throw std::runtime_error("boss_slot_busy");
            }
            fixture_controller = make_uobject_weak(boss);
            if (!fixture_controller.Get())
            {
                throw std::runtime_error("boss_weak_reference_failed");
            }
            fixture_level = sead_bgm::current_level_name(pawn).value_or(L"");
            for (const auto& slot : sead_bgm::slot_controllers(manager))
            {
                if (slot.controller != boss &&
                    sead_bgm::slot_state(slot.controller) == sead_bgm::slot_state_playing)
                {
                    if (const auto defaults = sead_bgm::default_options(slot.controller))
                    {
                        const auto weak = make_uobject_weak(slot.controller);
                        if (!weak.Get())
                        {
                            throw std::runtime_error("held_slot_weak_reference_failed");
                        }
                        held.emplace_back(weak, defaults->store_behaviour);
                        (void)sead_bgm::set_store_behaviour(slot.controller,
                                                            sead_bgm::store_behaviour_auto_resume);
                    }
                }
            }
            // A finite native cue exercises SEAD's natural Finished path;
            // it uses the same owned, idle Boss fixture and restoration.
            const bool short_cue = JsonFieldReader::bool_field(text, "short_cue").value_or(false);
            const auto track = short_cue
                                   ? std::wstring_view{L"/Game/Sound/BGM/"
                                                       L"BGM_cook_jojo_first.BGM_cook_jojo_first"}
                                   : bgm_shuffle_groups().back().tracks.front();
            if (!sead_bgm::test_request_path(boss, track) || !sead_bgm::play(boss))
            {
                throw std::runtime_error("group_play_dispatch_failed");
            }
        }
        void apply_volume(const std::string& text)
        {
            const auto master = JsonFieldReader::int64_field(text, "master");
            const auto bgm = JsonFieldReader::int64_field(text, "bgm");
            if (!config.gameplay() || !master || !bgm || *master < 0 ||
                *master > sead_bgm::maximum_volume_setting || *bgm < 0 ||
                *bgm > sead_bgm::maximum_volume_setting)
            {
                throw std::runtime_error("volume_values_or_gameplay_invalid");
            }
            if (!original_volume)
            {
                const auto instances = sead_bgm::find_live_objects(L"TresGameInstance");
                if (instances.size() != 1)
                {
                    throw std::runtime_error("volume_instance_ambiguous");
                }
                volume_instance = make_uobject_weak(instances.front());
                original_volume = sead_bgm::read_volume_settings(volume_instance.Get());
            }
            if (!original_volume || !sead_bgm::test_volume_settings(volume_instance.Get(),
                                                                    {static_cast<uint8_t>(*master),
                                                                     static_cast<uint8_t>(*bgm)}))
            {
                throw std::runtime_error("volume_dispatch_failed");
            }
        }
        void apply_restart_group()
        {
            UObject* controller = fixture_controller.Get();
            if (!config.gameplay() || !is_uobject_valid_fast(controller) ||
                sead_bgm::current_level_name(config.sora()) != fixture_level)
            {
                throw std::runtime_error("restart_requires_owned_group_fixture");
            }
            if (!sead_bgm::stop(controller))
            {
                throw std::runtime_error("group_stop_dispatch_failed");
            }
            // Stop fades asynchronously. Starting again in this same frame
            // lets its deferred completion stop the new fixture instead.
            restart_pending = true;
            restart_deadline = Clock::now() + randomizer_restart_timeout;
            report(L"accepted", L"waiting_for_native_group_stop");
            return;
        }
        void accept()
        {
            dir = config.bridge_dir();
            if (!dir || !path_points_to_regular_file(*dir / request_name))
            {
                return;
            }
            const auto text = read_text_file(*dir / request_name);
            if (!text)
            {
                return;
            }
            try_remove_file(*dir / request_name);
            id = JsonFieldReader::string_field(*text, "request_id").value_or(L"");
            if (id.empty() || std::ranges::find(consumed, id) != consumed.end())
            {
                return;
            }
            consumed.push_back(id);
            if (consumed.size() > randomizer_request_history_capacity)
            {
                consumed.pop_front();
            }
            command = JsonFieldReader::string_field(*text, "command").value_or(L"");
            requested_session = JsonFieldReader::string_field(*text, "session_id").value_or(L"");
            expires = JsonFieldReader::int64_field(*text, "expires_unix_ms").value_or(0);
            if (JsonFieldReader::string_field(*text, "schema") != L"kh3ap.randomizer_test.v1" ||
                JsonFieldReader::int64_field(*text, "process_id") != GetCurrentProcessId() ||
                expires <= unix_ms() ||
                expires > unix_ms() + max_randomizer_test_request_lifetime_ms)
            {
                throw std::runtime_error("invalid_request_identity_or_expiry");
            }
            if (command == L"inspect")
            {
                report(L"completed", L"randomizer_readback");
                return;
            }
            if (command == L"begin")
            {
                if (!session.empty() || requested_session.empty() || !config.gameplay())
                {
                    throw std::runtime_error("requires_gameplay_and_idle_session");
                }
                original = config.settings.status();
                if (!original.persistence_ready)
                {
                    throw std::runtime_error("settings_not_ready");
                }
                session = requested_session;
            }
            else if (session.empty() || session != requested_session)
            {
                throw std::runtime_error("session_mismatch");
            }
            lease = Clock::now() + randomizer_test_lease_duration;
            if (command == L"restore")
            {
                if (!restore())
                {
                    throw std::runtime_error("settings_restore_failed");
                }
            }
            else if (command == L"settings")
            {
                apply_settings(*text);
            }
            else if (command == L"play_group")
            {
                apply_play_group(*text);
            }
            else if (command == L"volume")
            {
                apply_volume(*text);
            }
            else if (command == L"restart_group")
            {
                apply_restart_group();
                return;
            }
            else if (command == L"stop_group")
            {
                stop_fixture();
            }
            else if (command != L"begin" && command != L"keepalive" && command != L"restore")
            {
                throw std::runtime_error("unknown_command");
            }
            report(L"completed", L"randomizer_readback");
        }

        void tick()
        {
            const auto now = Clock::now();
            if (now < next_poll)
            {
                return;
            }
            next_poll = now + randomizer_test_update_interval;
            if (restart_pending)
            {
                auto* controller = fixture_controller.Get();
                auto* pawn = config.sora();
                const auto state = sead_bgm::slot_state(controller);
                if (!is_uobject_valid_fast(controller) || !is_uobject_valid_fast(pawn) ||
                    sead_bgm::current_level_name(pawn) != fixture_level || now >= restart_deadline)
                {
                    restart_pending = false;
                    report(L"error", L"group_restart_stop_not_observed");
                }
                else if (state == 0 || state == sead_bgm::slot_state_finished)
                {
                    restart_pending = false;
                    const bool played =
                        sead_bgm::test_request_path(controller,
                                                    bgm_shuffle_groups().back().tracks.front()) &&
                        sead_bgm::play(controller);
                    report(played ? L"completed" : L"error",
                           played ? L"group_restart_dispatched" : L"group_restart_dispatch_failed");
                }
                return;
            }
            if (!session.empty() && now >= lease)
            {
                const bool restored = restore();
                report(restored ? L"completed" : L"error",
                       restored ? L"session_expired_restored" : L"settings_restore_failed");
                lease = now + randomizer_recovery_lease_duration;
            }
            try
            {
                if (requests.update(config.bridge_dir(), true))
                {
                    accept();
                }
            }
            catch (const std::exception& error)
            {
                const std::string reason = error.what();
                report(L"error", std::wstring(reason.begin(), reason.end()));
            }
        }
    };
    RuntimeRandomizerTestService::RuntimeRandomizerTestService(RuntimeRandomizerTestConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }
    RuntimeRandomizerTestService::~RuntimeRandomizerTestService() = default;
    void RuntimeRandomizerTestService::tick(bool initialized)
    {
        if (initialized)
        {
            impl_->tick();
        }
    }
} // namespace kh3ap
