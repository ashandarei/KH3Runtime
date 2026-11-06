#include "kh3ap/core/settings/pak_seed_settings.h"
#include "kh3ap/core/settings/seed_slot_options.h"
#include "kh3ap/features/story/minigames/classic_kingdom_completion_service.h"

#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/features/story/minigames/classic_kingdom_completion.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>

#include <array>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        struct RuntimeClassicKingdomHookEntry
        {
            std::wstring function_path{};
            size_t game_index{};
        };

        struct RegisteredClassicKingdomHook
        {
            std::wstring function_path{};
            std::pair<int, int> ids{};
        };

        auto append_game_ids(
                std::ostringstream& json,
                const std::vector<std::wstring_view>& game_ids) -> void
        {
            for (size_t index = 0; index < game_ids.size(); ++index)
            {
                if (index != 0)
                {
                    json << ", ";
                }
                json << "\"" << json_escape(game_ids[index]) << "\"";
            }
        }

        auto serialize_state(
                const RuntimeClassicKingdomCompletionTracker& tracker,
                std::wstring_view seed_name,
                int64_t slot,
                int64_t option_mode,
                const RuntimeClassicKingdomGame& last_game) -> std::string
        {
            std::ostringstream json{};
            json << "{\n"
                 << "  \"schema\": \"kh3ap.runtime_classic_kingdom_state.v3\",\n"
                 << "  \"event\": \"classic_kingdom_high_score\",\n"
                 << "  \"source\": \"" << json_escape(last_game.score_hook_path)
                 << "\",\n"
                 << "  \"seed_name\": \"" << json_escape(seed_name) << "\",\n"
                 << "  \"slot\": " << slot << ",\n"
                 << "  \"classic_kingdom_checks\": " << option_mode << ",\n"
                 << "  \"last_game\": \"" << json_escape(last_game.game_id)
                 << "\",\n"
                 << "  \"high_score_count\": " << tracker.high_score_count() << ",\n"
                 << "  \"all_games_high_score\": "
                 << (tracker.all_high_scores_complete() ? "true" : "false")
                 << ",\n"
                 << "  \"high_score_games\": [";
            append_game_ids(json, tracker.high_score_game_ids());
            json << "],\n"
                 << "  \"updated_at_utc\": \"" << now_iso8601_utc() << "\"\n"
                 << "}\n";
            return json.str();
        }
    }

    struct RuntimeClassicKingdomCompletionService::Impl
    {
        explicit Impl(RuntimeClassicKingdomCompletionConfig value)
            : config(std::move(value))
        {
            for (size_t index = 0; index < runtime_classic_kingdom_games.size(); ++index)
            {
                score_hook_entries[index] = RuntimeClassicKingdomHookEntry{
                        std::wstring{runtime_classic_kingdom_games[index].score_hook_path},
                        index,
                };
            }
        }

        ~Impl()
        {
            uninstall();
        }

        auto reset_seed_scope(
                const std::filesystem::path& bridge_dir,
                std::wstring seed_name,
                int64_t slot,
                int64_t option_mode) -> void
        {
            tracker.reset();
            current_bridge_dir = bridge_dir;
            current_seed_name = std::move(seed_name);
            current_slot = slot;
            current_option_mode = option_mode;
            state_dirty = false;

            const auto state_text = read_text_file(
                    bridge_dir /
                    std::filesystem::path{runtime_classic_kingdom_state_filename});
            if (!state_text.has_value() ||
                JsonFieldReader::string_field(*state_text, "schema").value_or(L"") !=
                        L"kh3ap.runtime_classic_kingdom_state.v3" ||
                JsonFieldReader::string_field(*state_text, "seed_name").value_or(L"") !=
                        current_seed_name ||
                JsonFieldReader::int64_field(*state_text, "slot").value_or(-1) !=
                        current_slot ||
                JsonFieldReader::int64_field(
                        *state_text,
                        runtime_classic_kingdom_slot_option)
                                .value_or(-1) != current_option_mode)
            {
                return;
            }

            for (const std::wstring& game_id :
                 JsonFieldReader::string_array_field(*state_text, "high_score_games"))
            {
                tracker.mark_high_score(game_id);
            }
        }

        auto ensure_seed_scope() -> bool
        {
            const auto bridge_dir = config.bridge_dir_resolver
                    ? config.bridge_dir_resolver()
                    : std::nullopt;
            if (!bridge_dir.has_value())
            {
                return false;
            }

            // Seed options are parsed once from the installed pak.
            const auto& options = installed_seed_slot_data();
            if (!options.valid()) return false;
            const auto identity = options.object_field("kh3_seed");
            const auto seed_name = identity.string_field("seed_name");
            const auto slot = identity.int64_field("slot");
            const int64_t option_mode = options.int64_field(runtime_classic_kingdom_slot_option).value_or(0);
            if (!seed_name.has_value() || seed_name->empty() ||
                !slot.has_value() || *slot < 1)
            {
                return false;
            }

            if (current_bridge_dir != bridge_dir ||
                current_seed_name != *seed_name ||
                current_slot != *slot ||
                current_option_mode != option_mode)
            {
                reset_seed_scope(*bridge_dir, *seed_name, *slot, option_mode);
            }
            else
            {
                current_option_mode = option_mode;
            }
            return true;
        }

        auto persist_high_score(size_t game_index) -> void
        {
            if (game_index >= runtime_classic_kingdom_games.size() ||
                !ensure_seed_scope() ||
                !current_bridge_dir.has_value() ||
                current_option_mode < 1)
            {
                return;
            }

            const bool newly_marked = tracker.mark_high_score(game_index);
            state_dirty = state_dirty || newly_marked;
            if (!state_dirty)
            {
                return;
            }

            const RuntimeClassicKingdomGame& game =
                    runtime_classic_kingdom_games[game_index];
            const bool wrote = write_text_file_atomic_verified(
                    *current_bridge_dir /
                            std::filesystem::path{
                                    runtime_classic_kingdom_state_filename},
                    serialize_state(
                            tracker,
                            current_seed_name,
                            current_slot,
                            current_option_mode,
                            game));
            state_dirty = !wrote;

            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] ClassicKingdom event=high_score game={} "
                        "id={} high_scores={}/{} all={} "
                        "sidecar_written={} option_mode={} "
                        "mode=event_driven no_object_scans=true\n"),
                    game.display_name,
                    game.game_id,
                    static_cast<uint64_t>(tracker.high_score_count()),
                    static_cast<uint64_t>(runtime_classic_kingdom_games.size()),
                    tracker.all_high_scores_complete(),
                    wrote,
                    current_option_mode);
        }

        auto on_score_event(RuntimeClassicKingdomHookEntry* entry) -> void
        {
            if (!entry || entry->game_index >= runtime_classic_kingdom_games.size())
            {
                return;
            }

            const RuntimeClassicKingdomGame& game =
                    runtime_classic_kingdom_games[entry->game_index];
            if (config.runtime_grant_transition_started)
            {
                // Arm the runtime-grant gate before writing the check sidecar.
                // The AP client can react to that write immediately, while KH3
                // is still leaving the Classic Kingdom results flow.
                config.runtime_grant_transition_started(
                        game.game_id,
                        game.score_hook_path);
            }

            std::lock_guard lock{state_mutex};
            persist_high_score(entry->game_index);
        }

        auto register_hook(RuntimeClassicKingdomHookEntry& entry) -> void
        {
            try
            {
                const auto ids = UObjectGlobals::RegisterHook(
                        entry.function_path,
                        [](
                                UnrealScriptFunctionCallableContext& /*context*/,
                                void* /*custom_data*/) {},
                        [this](
                                UnrealScriptFunctionCallableContext& /*context*/,
                                void* custom_data) {
                            try
                            {
                                // A successful return is KH3's authoritative
                                // per-game high-score update notification.
                                on_score_event(
                                        static_cast<RuntimeClassicKingdomHookEntry*>(
                                                custom_data));
                            }
                            catch (...)
                            {
                            }
                        },
                        &entry);
                registered_hooks.push_back(
                        RegisteredClassicKingdomHook{entry.function_path, ids});
            }
            catch (...)
            {
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] ClassicKingdom hook registration "
                            "failed function={}\n"),
                        entry.function_path);
            }
        }

        auto install_once() -> void
        {
            if (!config.enabled || installed)
            {
                return;
            }
            installed = true;
            registered_hooks.reserve(runtime_classic_kingdom_games.size());
            for (RuntimeClassicKingdomHookEntry& entry : score_hook_entries)
            {
                register_hook(entry);
            }
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] ClassicKingdom hooks installed={}/{} "
                        "mode=event_driven no_object_scans=true\n"),
                    static_cast<uint64_t>(registered_hooks.size()),
                    static_cast<uint64_t>(runtime_classic_kingdom_games.size()));
        }

        auto uninstall() -> void
        {
            if (!installed)
            {
                return;
            }
            for (const RegisteredClassicKingdomHook& hook : registered_hooks)
            {
                try
                {
                    UObjectGlobals::UnregisterHook(hook.function_path, hook.ids);
                }
                catch (...)
                {
                }
            }
            registered_hooks.clear();
            installed = false;
        }

        RuntimeClassicKingdomCompletionConfig config;
        RuntimeClassicKingdomCompletionTracker tracker{};
        std::array<
                RuntimeClassicKingdomHookEntry,
                runtime_classic_kingdom_games.size()>
                score_hook_entries{};
        std::vector<RegisteredClassicKingdomHook> registered_hooks{};
        std::optional<std::filesystem::path> current_bridge_dir{};
        std::wstring current_seed_name{};
        int64_t current_slot{};
        int64_t current_option_mode{};
        std::mutex state_mutex{};
        bool state_dirty{};
        bool installed{};
    };

    RuntimeClassicKingdomCompletionService::
            RuntimeClassicKingdomCompletionService(
                    RuntimeClassicKingdomCompletionConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeClassicKingdomCompletionService::~RuntimeClassicKingdomCompletionService() =
            default;
    RuntimeClassicKingdomCompletionService::
            RuntimeClassicKingdomCompletionService(
                    RuntimeClassicKingdomCompletionService&&) noexcept = default;
    auto RuntimeClassicKingdomCompletionService::operator=(
            RuntimeClassicKingdomCompletionService&&) noexcept
            -> RuntimeClassicKingdomCompletionService& = default;

    auto RuntimeClassicKingdomCompletionService::install_once() -> void
    {
        impl_->install_once();
    }

    auto RuntimeClassicKingdomCompletionService::uninstall() -> void
    {
        impl_->uninstall();
    }
}
