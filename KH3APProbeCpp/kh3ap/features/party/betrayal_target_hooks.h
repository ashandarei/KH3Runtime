#pragma once

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::betrayal_target_hooks
{
    using Resolve = RC::Unreal::UObject* (*)(RC::Unreal::UObject*);
    void install(RC::Unreal::UObject* controller, Resolve resolve);
    RC::Unreal::UObject* stored_target(RC::Unreal::UObject* controller);
} // namespace kh3ap::betrayal_target_hooks
