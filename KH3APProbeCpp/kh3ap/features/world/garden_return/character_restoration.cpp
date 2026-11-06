#include "character_restoration.h"
#include "runtime_context.h"
#include "kh3ap/features/player/player_ids.h"
#include "kh3ap/features/player/combat/aqua_style_unlock.h"
#include "kh3ap/features/world/garden_return/garden_return_policy.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/core/events/world_context_events.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <utility>

namespace kh3ap::garden_return_detail
{
    namespace
    {
        constexpr int sora_player_id = player_id_value(PlayerId::Sora);
        constexpr int aqua_player_id = player_id_value(PlayerId::Aqua);
        constexpr auto character_restore_initial_delay = std::chrono::seconds(1);
        constexpr auto character_restore_retry_delay = std::chrono::seconds(5);
    } // namespace

    CharacterRestoration::CharacterRestoration(CharacterRestorationConfig config) : config_(std::move(config)) {}

    void CharacterRestoration::select_aqua(bool selected)
    {
        state_.aqua_selected = selected;
    }

    void CharacterRestoration::request(bool selected)
    {
        state_.aqua_selected = selected;
        state_.pending = true;
    }

    void CharacterRestoration::defer_until(Clock::time_point deadline)
    {
        state_.next_attempt = deadline;
    }

    const CharacterRestorationSnapshot& CharacterRestoration::snapshot() const
    {
        return state_;
    }

    void CharacterRestoration::tick(Clock::time_point now)
    {
        const auto context_revision = world_context_revision.load(std::memory_order_acquire);
        if (state_.context_revision != context_revision)
        {
            state_.context_revision = context_revision;
            state_.pending = true;
            ++state_.events;
        }
        if (!state_.aqua_selected || !state_.pending)
        {
            return;
        }
        ++state_.checks;
        const bool in_kh02 = camp_is_kh02_world(world());
        auto* pawn = player();
        const int current_character = player_id(pawn);
        if (in_kh02)
        {
            if (current_character == aqua_player_id)
            {
                // Map/controller producers wake this service again when
                // possession changes. A quiet session needs no lookup.
                state_.pending = false;
                aqua_style_unlock::invalidate();
                return;
            }
            if (current_character != sora_player_id || config_.navigation_pending() || config_.combat_swap_busy() ||
                config_.interaction_pending() || now < state_.next_attempt)
            {
                return;
            }
            if (!Kh02Story::challenges_available() || !loading_closed() || save_open() || config_.camp_ready() ||
                Kh02Story::challenges_open())
            {
                return;
            }
            auto* instance = object_field(viewport(), L"GameInstance");
            if (runtime_sora_control_ready(pawn) && instance && field<int32_t>(instance, 0x8d0) == 0)
            {
                state_.next_attempt = now + character_restore_retry_delay;
                if (config_.change_player(pawn, aqua_player_id))
                {
                    config_.set_phase(L"restoring_aqua_after_travel");
                }
            }
            return;
        }
        // Ordinary KH3 maps must return to Sora before AP grants resume.
        // Keep the loading guard: an intermediate world is not an arrival.
        if (!loading_closed())
        {
            return;
        }
        if (current_character == sora_player_id)
        {
            state_.aqua_selected = false;
            state_.pending = false;
        }
        else if (current_character == aqua_player_id && !config_.navigation_pending() && !config_.camp_ready() &&
                 now >= state_.next_attempt)
        {
            state_.next_attempt = now + character_restore_retry_delay;
            if (config_.change_player(pawn, sora_player_id))
            {
                config_.set_phase(L"restoring_sora_outside_kh02");
            }
        }
    }

    bool CharacterRestoration::finish_arrival(RC::Unreal::UObject* pawn)
    {
        if (!state_.aqua_selected)
        {
            return true;
        }
        if (!is_uobject_valid_fast(pawn) || pawn != player() || !camp_is_kh02_world(world()) ||
            config_.interaction_pending())
        {
            return false;
        }
        if (player_id(pawn) == aqua_player_id)
        {
            return true;
        }
        const auto now = Clock::now();
        if (player_id(pawn) == sora_player_id && now >= state_.next_attempt)
        {
            state_.next_attempt = now + character_restore_initial_delay;
            if (config_.change_player(pawn, aqua_player_id))
            {
                config_.set_phase(L"restoring_aqua_under_loading_cover");
            }
        }
        return false;
    }
} // namespace kh3ap::garden_return_detail
