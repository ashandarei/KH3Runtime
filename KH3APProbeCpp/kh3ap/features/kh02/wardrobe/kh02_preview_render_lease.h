#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <vector>

namespace kh3ap
{
    // Native release is explicit on the game thread. Destruction only releases weak references.
    class Kh02PreviewRenderLease
    {
      public:
        Kh02PreviewRenderLease() = default;
        Kh02PreviewRenderLease(const Kh02PreviewRenderLease&) = delete;
        auto operator=(const Kh02PreviewRenderLease&) -> Kh02PreviewRenderLease& = delete;
        void prepare(RC::Unreal::UObject* pawn, RC::Unreal::UObject* preview_material);
        void close(bool destroy_actor = true);
        auto ready_for(RC::Unreal::UObject* pawn) const -> bool;

        auto capture_actor() const -> RC::Unreal::UObject*
        {
            return actor.Get();
        }

        auto widget() const -> RC::Unreal::UObject*
        {
            return image.Get();
        }

        auto light_actors() const -> const std::vector<RC::Unreal::FWeakObjectPtr>&
        {
            return lights;
        }

      private:
        RC::Unreal::FWeakObjectPtr actor, image, target, hero, scene_manager;
        std::vector<RC::Unreal::FWeakObjectPtr> lights;
    };
} // namespace kh3ap
