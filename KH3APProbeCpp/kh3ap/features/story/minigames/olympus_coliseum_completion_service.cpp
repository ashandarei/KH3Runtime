#include "kh3ap/core/settings/pak_seed_settings.h"
#include "kh3ap/core/settings/seed_slot_options.h"
#include "kh3ap/features/story/minigames/olympus_coliseum_completion_service.h"

#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/features/story/minigames/olympus_coliseum_completion.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace RC;

namespace kh3ap
{
    namespace
    {
        auto append_cup_ids(
                std::ostringstream& json,
                const std::vector<std::wstring_view>& cup_ids) -> void
        {
            for (size_t index = 0; index < cup_ids.size(); ++index)
            {
                if (index != 0)
                {
                    json << ", ";
                }
                json << "\"" << json_escape(cup_ids[index]) << "\"";
            }
        }

        auto serialize_state(
                const RuntimeOlympusColiseumCompletionTracker& tracker,
                std::wstring_view seed_name,
                int64_t slot,
                int64_t option_mode,
                const RuntimeOlympusColiseumCup& last_cup) -> std::string
        {
            std::ostringstream json{};
            json << "{\n"
                 << "  \"schema\": \"kh3ap.runtime_olympus_coliseum_state.v1\",\n"
                 << "  \"event\": \"olympus_coliseum_cup_clear\",\n"
                 << "  \"source\": \""
                 << json_escape(runtime_olympus_coliseum_progress_save_source)
                 << "\",\n"
                 << "  \"seed_name\": \"" << json_escape(seed_name) << "\",\n"
                 << "  \"slot\": " << slot << ",\n"
                 << "  \"olympus_coliseum_pool\": " << option_mode << ",\n"
                 << "  \"last_cup\": \"" << json_escape(last_cup.cup_id) << "\",\n"
                 << "  \"completed_count\": " << tracker.completed_count() << ",\n"
                 << "  \"completed_cups\": [";
            append_cup_ids(json, tracker.completed_cup_ids());
            json << "],\n"
                 << "  \"updated_at_utc\": \"" << now_iso8601_utc() << "\"\n"
                 << "}\n";
            return json.str();
        }

        auto is_coliseum_progress_save_path(
                const std::filesystem::path& path) -> bool
        {
            std::wstring filename = path.filename().wstring();
            std::transform(
                    filename.begin(),
                    filename.end(),
                    filename.begin(),
                    [](wchar_t character) {
                        return static_cast<wchar_t>(std::towlower(character));
                    });

            constexpr std::wstring_view prefix = L"coliseummod_slot";
            constexpr std::wstring_view suffix = L".sav";
            if (!filename.starts_with(prefix) || !filename.ends_with(suffix))
            {
                return false;
            }
            const std::wstring_view digits{
                    filename.data() + prefix.size(),
                    filename.size() - prefix.size() - suffix.size()};
            return !digits.empty() &&
                   std::all_of(
                           digits.begin(),
                           digits.end(),
                           [](wchar_t character) {
                               return character >= L'0' && character <= L'9';
                           });
        }
    }

    struct RuntimeOlympusColiseumCompletionService::Impl
    {
        explicit Impl(RuntimeOlympusColiseumCompletionConfig value)
            : config(std::move(value))
        {
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
            last_cup_index.reset();
            state_dirty = false;

            const auto state_text = read_text_file(
                    bridge_dir /
                    std::filesystem::path{
                            runtime_olympus_coliseum_state_filename});
            if (!state_text.has_value() ||
                JsonFieldReader::string_field(*state_text, "schema")
                                .value_or(L"") !=
                        L"kh3ap.runtime_olympus_coliseum_state.v1" ||
                JsonFieldReader::string_field(*state_text, "seed_name")
                                .value_or(L"") != current_seed_name ||
                JsonFieldReader::int64_field(*state_text, "slot")
                                .value_or(-1) != current_slot ||
                JsonFieldReader::int64_field(
                        *state_text,
                        runtime_olympus_coliseum_slot_option)
                                .value_or(-1) != current_option_mode)
            {
                return;
            }

            for (const std::wstring& cup_id :
                 JsonFieldReader::string_array_field(
                         *state_text,
                         "completed_cups"))
            {
                tracker.mark_completed(cup_id);
            }

            const std::wstring last_cup_id =
                    JsonFieldReader::string_field(*state_text, "last_cup")
                            .value_or(L"");
            for (size_t index = 0;
                 index < runtime_olympus_coliseum_cups.size();
                 ++index)
            {
                if (runtime_olympus_coliseum_cups[index].cup_id ==
                    last_cup_id)
                {
                    last_cup_index = index;
                    break;
                }
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

            const auto& options = installed_seed_slot_data();
            if (!options.valid()) return false;
            const auto identity = options.object_field("kh3_seed");
            const auto seed_name = identity.string_field("seed_name");
            const auto slot = identity.int64_field("slot");
            const int64_t option_mode = options.int64_field(runtime_olympus_coliseum_slot_option).value_or(0);
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
            return true;
        }

        auto record_clears(
                const std::vector<size_t>& completed_indices,
                const std::filesystem::path& save_path) -> void
        {
            std::lock_guard lock{state_mutex};
            if (!ensure_seed_scope() ||
                !current_bridge_dir.has_value() ||
                !runtime_olympus_coliseum_option_is_active(
                        current_option_mode))
            {
                return;
            }

            std::vector<size_t> newly_completed{};
            for (const size_t cup_index : completed_indices)
            {
                if (cup_index < runtime_olympus_coliseum_cups.size() &&
                    tracker.mark_completed(cup_index))
                {
                    newly_completed.push_back(cup_index);
                    last_cup_index = cup_index;
                }
            }
            state_dirty = state_dirty || !newly_completed.empty();
            if (!state_dirty || !last_cup_index.has_value())
            {
                return;
            }

            const RuntimeOlympusColiseumCup& last_cup =
                    runtime_olympus_coliseum_cups[*last_cup_index];
            if (!newly_completed.empty() &&
                config.runtime_grant_transition_started)
            {
                // The close signal occurs after the mod has serialized its
                // authoritative result, but before its results/map transition
                // necessarily returns control to Sora.
                config.runtime_grant_transition_started(
                        last_cup.cup_id,
                        runtime_olympus_coliseum_progress_save_source);
            }

            const bool wrote = write_text_file_atomic_verified(
                    *current_bridge_dir /
                            std::filesystem::path{
                                    runtime_olympus_coliseum_state_filename},
                    serialize_state(
                            tracker,
                            current_seed_name,
                            current_slot,
                            current_option_mode,
                            last_cup));
            state_dirty = !wrote;

            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] OlympusColiseum event=cup_clear "
                        "newly_completed={} last_cup={} id={} completed={}/{} "
                        "sidecar_written={} option_mode={} source_file={} "
                        "mode=save_close no_uobject_hooks=true\n"),
                    static_cast<uint64_t>(newly_completed.size()),
                    last_cup.display_name,
                    last_cup.cup_id,
                    static_cast<uint64_t>(tracker.completed_count()),
                    static_cast<uint64_t>(runtime_olympus_coliseum_cups.size()),
                    wrote,
                    current_option_mode,
                    save_path.filename().wstring());
        }

        auto process_progress_save(
                const std::filesystem::path& save_path) -> void
        {
            if (!config.enabled ||
                !is_coliseum_progress_save_path(save_path))
            {
                return;
            }
            const auto save_bytes = read_text_file(save_path);
            constexpr size_t maximum_supported_save_size = 8 * 1024 * 1024;
            if (!save_bytes.has_value() ||
                save_bytes->size() > maximum_supported_save_size)
            {
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] OlympusColiseum progress save "
                            "read failed file={}\n"),
                        save_path.filename().wstring());
                return;
            }
            record_clears(
                    runtime_olympus_coliseum_completed_cup_indices(
                            *save_bytes),
                    save_path);
        }

        RuntimeOlympusColiseumCompletionConfig config;
        RuntimeOlympusColiseumCompletionTracker tracker{};
        std::optional<std::filesystem::path> current_bridge_dir{};
        std::wstring current_seed_name{};
        int64_t current_slot{};
        int64_t current_option_mode{};
        std::optional<size_t> last_cup_index{};
        std::mutex state_mutex{};
        bool state_dirty{};
    };

    RuntimeOlympusColiseumCompletionService::
            RuntimeOlympusColiseumCompletionService(
                    RuntimeOlympusColiseumCompletionConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeOlympusColiseumCompletionService::~RuntimeOlympusColiseumCompletionService() =
            default;
    RuntimeOlympusColiseumCompletionService::
            RuntimeOlympusColiseumCompletionService(
                    RuntimeOlympusColiseumCompletionService&&) noexcept = default;
    auto RuntimeOlympusColiseumCompletionService::operator=(
            RuntimeOlympusColiseumCompletionService&&) noexcept
            -> RuntimeOlympusColiseumCompletionService& = default;

    auto RuntimeOlympusColiseumCompletionService::process_progress_save(
            const std::filesystem::path& save_path) -> void
    {
        impl_->process_progress_save(save_path);
    }
}
