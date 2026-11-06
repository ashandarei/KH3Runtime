#pragma once
#include "kh02_mirror_travel_policy.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <optional>

namespace kh3ap
{
    class Kh02StoryFlags;

    class Kh02MirrorTravel
    {
      public:
        auto stage(RC::Unreal::UObject* source_world, std::wstring_view caller, const std::wstring& destination,
                   const std::wstring& tag, int fade, bool epilogue_complete) -> bool;
        void commit();
        void cancel();
        auto dispatch(RC::Unreal::UObject* current_world, const Kh02StoryFlags& source_flags) -> bool;
        std::wstring arrival_tag;
        std::wstring arrival_package;

      private:
        std::optional<kh02_mirror_travel::Plan> staged;
        RC::Unreal::FWeakObjectPtr source_world;
        bool pending{};
    };
} // namespace kh3ap
