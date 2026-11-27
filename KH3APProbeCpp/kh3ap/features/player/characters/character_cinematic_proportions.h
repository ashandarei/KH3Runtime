#pragma once
#include <memory>
#include <vector>
namespace RC::Unreal
{
    class UObject;
}
namespace kh3ap::character_cinematic_proportions
{
    struct Plan;
    using Lease = std::shared_ptr<Plan>;
    Lease prepare(RC::Unreal::UObject* pawn, const std::vector<RC::Unreal::UObject*>& animations);
    void publish(std::vector<Lease> plans);
    void publish_reaction(Lease plan);
} // namespace kh3ap::character_cinematic_proportions
