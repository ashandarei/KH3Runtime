#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kh3ap
{
    // Only unfinished setup retries. Events arriving during an attempt must
    // survive that attempt's completion.
    class RuntimeWorldBootstrapWork
    {
    public:
        using Clock=std::chrono::steady_clock;
        void request() { pending_=true;next_={}; }
        bool take(Clock::time_point now) {
            if(!pending_||now<next_)return false;
            pending_=false;return true;
        }
        void finish(Clock::time_point now,bool complete,Clock::duration retry) {
            if(!complete&&!pending_){pending_=true;next_=now+retry;}
        }
        bool pending() const { return pending_; }
    private:
        bool pending_{true};
        Clock::time_point next_{};
    };

    struct RuntimeWorldActorDefinition
    {
        std::wstring_view name{};
        std::wstring_view class_path{};
        int32_t latent_uuid{};
    };

    // Event and VBonus detection/persistence are native; no reward worker actors.
    inline constexpr std::array<RuntimeWorldActorDefinition, 0> runtime_required_world_actors{};

    inline constexpr int32_t runtime_goa_shop_level = 7000;
    inline constexpr int32_t runtime_proof_ui_asset_load_uuid = 0x4b483413;
    inline constexpr std::wstring_view runtime_proof_ui_class_path =
            L"/Game/Blueprints/GoA/UI/W_ProofUI.W_ProofUI_C";
    inline constexpr std::array<std::wstring_view, 1>
            runtime_world_bootstrap_class_object_names{{
                    L"W_ProofUI_C",
            }};

    // Pointer identity is used only while the matching weak UWorld reference
    // remains live. A new world resets the once-per-world native effects.
    struct RuntimeWorldBootstrapProgress
    {
        std::uintptr_t world_identity{};
        bool shop_level_set{};
        bool proof_ui_added{};

        [[nodiscard]] constexpr auto complete() const noexcept -> bool
        { return world_identity!=0&&shop_level_set&&proof_ui_added; }

        [[nodiscard]] constexpr auto observe_world(
                std::uintptr_t identity) noexcept -> bool
        {
            if (identity == 0 || identity == world_identity)
            {
                return false;
            }
            world_identity = identity;
            shop_level_set = false;
            proof_ui_added = false;
            return true;
        }

        constexpr auto reset() noexcept -> void
        {
            world_identity = 0;
            shop_level_set = false;
            proof_ui_added = false;
        }
    };
}
