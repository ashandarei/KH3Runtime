#include "kh02_progress_service.h"
#include "kh3ap/features/kh02/core/kh02_profile.h"
#include "kh3ap/features/kh02/treasure/kh02_chest_journal.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/features/kh02/progress/kh02_progress_policy.h"
#include "kh3ap/features/kh02/progress/kh02_gameflow_policy.h"
#include <Unreal/AActor.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/World.hpp>
#include <Windows.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;

    namespace
    {
        auto actor_world(UObject* pawn) -> UObject*
        {
            return is_uobject_valid_fast(pawn) ? static_cast<AActor*>(pawn)->GetWorld() : nullptr;
        }

        namespace chest_journal_layout
        {
            using kh02_chest_journal::bit_count;
            using kh02_chest_journal::bits_per_word;
            using kh02_chest_journal::first_imported_chest_bit;
            using kh02_chest_journal::first_owned_word;
            using kh02_chest_journal::last_owned_word;
            using kh02_chest_journal::owned_word_count;
            using kh02_chest_journal::preserved_bits_in_first_word;
            using kh02_chest_journal::preserved_native_mask;
            using kh02_chest_journal::word_count;
            constexpr uintptr_t game_instance_save_offset = 0x870;
            constexpr uintptr_t save_journal_offset = 0x13684;
        } // namespace chest_journal_layout

        struct CommandState
        {
            struct GameflowMutation
            {
                std::wstring request_id;
                std::wstring seed;
                int64_t slot{};
                int64_t epoch{};
                uint64_t revision{};
                Json expected;
                Json result;
            };

            std::optional<GameflowMutation> gameflow_mutation;

            auto command(UObject* pawn, std::wstring_view request_id, std::wstring_view action,
                         std::wstring_view requested_flag, std::optional<int> value,
                         const Kh02ProgressService::Invalidate& invalidate) -> std::string
            {
                const auto saved = reconciliation::view();
                if (!saved->ready || !saved->kh02_progress_ready)
                {
                    throw std::runtime_error("The native 0.2 save binding is not ready");
                }
                const auto progress = kh02_progress::to_json(saved->kh02_progress);
                kh02_progress::validate(progress);
                const auto requested = to_utf8(requested_flag);
                const bool resetting = action == L"reset_gameflow";
                Json result{{"status", "completed"},
                            {"reason", ""},
                            {"flag_bank", "kh02"},
                            {"flag_name", requested},
                            {"flag_value", kh02_gameflow::read(progress, requested)},
                            {"game_flag_read", true},
                            {"revision", saved->kh02_progress_revision}};
                if (gameflow_mutation && gameflow_mutation->request_id == request_id)
                {
                    const auto& pending = *gameflow_mutation;
                    const bool reset_queued = pending.result.value("kh02_reset", false) &&
                                              saved->kh02_progress_revision + 1 == pending.revision;
                    if (saved->seed != pending.seed || saved->slot != pending.slot || saved->epoch != pending.epoch ||
                        (saved->kh02_progress_revision != pending.revision && !reset_queued))
                    {
                        throw std::runtime_error("The native save binding changed during the 0.2 gameflow command");
                    }
                    if (!saved->kh02_progress_error.empty())
                    {
                        throw std::runtime_error(saved->kh02_progress_error);
                    }
                    if (!reset_queued && progress == pending.expected)
                    {
                        return pending.result.dump();
                    }
                    return Json{
                        {"status", "waiting"}, {"reason", "Waiting for 0.2 story-bank readback"}, {"flag_bank", "kh02"}}
                        .dump();
                }
                gameflow_mutation.reset();
                if (action == L"get_game_flag")
                {
                    return result.dump();
                }
                if (action != L"set_game_flag" && !resetting)
                {
                    throw std::runtime_error("Unsupported 0.2 gameflow command");
                }
                auto* world = actor_world(pawn);
                if (!is_uobject_valid_fast(world) ||
                    (world->GetPathName() != L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01" &&
                     world->GetPathName() != L"/Game/Maps/GardenOfAssemblage/umap/GoA_02.GoA_02"))
                {
                    throw std::runtime_error("Return to Garden before changing or resetting the 0.2 story");
                }
                if (!runtime_sora_control_ready(pawn))
                {
                    throw std::runtime_error(
                        "Close menus and wait for playable Garden Sora before changing the 0.2 story");
                }
                GameflowMutation pending{std::wstring(request_id),      saved->seed, saved->slot, saved->epoch,
                                         saved->kh02_progress_revision, nullptr,     result};
                if (resetting)
                {
                    if (requested != "gameflow_DW")
                    {
                        throw std::runtime_error("Use /gameflow kh02 reset to reset the complete chapter");
                    }
                    const auto reset = Json::parse(Kh02ProgressService::reset(pawn, invalidate));
                    ++pending.revision;
                    pending.result["kh02_reset"] = true;
                    pending.result["progress"] = nullptr;
                    pending.result["flag_value"] = 102;
                    pending.result["opened_chests_before"] = reset.at("opened_chests_before");
                    pending.result["opened_chests_after"] = 0;
                    pending.result["other_chest_bits_unchanged"] = true;
                }
                else
                {
                    if (!value)
                    {
                        throw std::runtime_error("A 0.2 gameflow write requires an integer");
                    }
                    pending.expected = kh02_gameflow::update(progress, requested, *value);
                    invalidate();
                    reconciliation::kh02_progress_update(*kh02_progress::from_json(pending.expected), pending.revision,
                                                         pending.seed, pending.slot);
                    pending.result["game_flag_set"] = true;
                    pending.result["flag_value"] = *value;
                }
                pending.result["revision"] = pending.revision;
                gameflow_mutation = std::move(pending);
                return Json{
                    {"status", "waiting"}, {"reason", "Waiting for 0.2 story-bank readback"}, {"flag_bank", "kh02"}}
                    .dump();
            }
        };
    } // namespace

    auto Kh02ProgressService::reset(UObject* pawn, const Invalidate& invalidate) -> std::string
    {
        auto* w = actor_world(pawn);
        if (!is_uobject_valid_fast(w) || (w->GetPathName() != L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01" &&
                                          w->GetPathName() != L"/Game/Maps/GardenOfAssemblage/umap/GoA_02.GoA_02"))
        {
            throw std::runtime_error("Return to the Garden before resetting the 0.2 playtest");
        }
        const auto saved = reconciliation::view();
        if (!saved->ready || !saved->kh02_progress_ready)
        {
            throw std::runtime_error("The native save binding is not ready for a 0.2 reset");
        }
        const auto progress = kh02_progress::to_json(saved->kh02_progress);
        auto* game_instance = try_read_reflected_object_property_value(w, L"OwningGameInstance");
        auto* game_instance_type = find_loaded_runtime_asset_class(L"/Script/TresGame.TresGameInstance");
        using kh02_native::Site;
        // Steam's wrapper adds save+0x13280 and its bit helper adds +0x404;
        // EGS inlines the same save+0x13684 journal. The bank starts at +0x870.
        // Guard surviving instruction tails, since chest tracking hooks entries.
        if (!is_uobject_a_fast(game_instance, game_instance_type) ||
            !kh02_native::verify_sites({Site::chest_journal_read, Site::chest_save_bank}))
        {
            throw std::runtime_error("The 0.2 chest-reset profile does not match this runtime");
        }
        auto* journal = reinterpret_cast<std::byte*>(game_instance) + chest_journal_layout::game_instance_save_offset +
                        chest_journal_layout::save_journal_offset;
        std::array<uint32_t, chest_journal_layout::word_count> before{}, after{};
        std::memcpy(before.data(), journal, sizeof(before));
        // The 40 imported identities own bits 4056..4095. Preserve every
        // preceding native bit, including the low 24 bits of word 126.
        const uint32_t cleared =
            before[chest_journal_layout::first_owned_word] & chest_journal_layout::preserved_native_mask;
        const uint32_t empty = 0;
        std::memcpy(journal + chest_journal_layout::first_owned_word * sizeof(uint32_t), &cleared, sizeof(cleared));
        std::memcpy(journal + chest_journal_layout::last_owned_word * sizeof(uint32_t), &empty, sizeof(empty));
        std::memcpy(after.data(), journal, sizeof(after));
        if (!std::equal(before.begin(), before.end() - chest_journal_layout::owned_word_count, after.begin()) ||
            after[chest_journal_layout::first_owned_word] != cleared || after.back())
        {
            throw std::runtime_error("The 0.2 chest reset failed its journal readback");
        }
        int opened_before = 0;
        for (int bit = 0; bit < chest_journal_layout::bits_per_word; ++bit)
        {
            opened_before += (before.back() >> bit) & 1;
            if (bit >= chest_journal_layout::preserved_bits_in_first_word)
            {
                opened_before += (before[chest_journal_layout::first_owned_word] >> bit) & 1;
            }
        }
        invalidate();
        reconciliation::kh02_progress_reset(saved->kh02_progress_revision, saved->seed, saved->slot);
        return Json{{"reset_requested", true},
                    {"revision_before", saved->kh02_progress_revision},
                    {"progress_before", progress},
                    {"checks_before", saved->completed_checks},
                    {"opened_chests_before", opened_before},
                    {"opened_chests_after", 0},
                    {"other_chest_bits_unchanged", true},
                    {"challenges_after", 0},
                    {"counts_after", 0},
                    {"events_after", 0}}
            .dump();
    }

    auto Kh02ProgressService::command(UObject* pawn, std::wstring_view request_id, std::wstring_view action,
                                      std::wstring_view flag_name, std::optional<int> value,
                                      const Invalidate& invalidate) -> std::string
    {
        static CommandState state;
        return state.command(pawn, request_id, action, flag_name, value, invalidate);
    }
} // namespace kh3ap
