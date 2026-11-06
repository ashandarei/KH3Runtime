#include "kh02_help_continuations.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/TArray.hpp>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr std::size_t help_queue_limit = 16;
        constexpr std::size_t reflected_parameter_limit_bytes = 2'048;
        constexpr std::size_t linkage_parameter_bytes = sizeof(int32_t);
    } // namespace

    void Kh02HelpContinuations::enqueue(UObject* callback, UObject* world, FName execution, int linkage,
                                        Kh02MapSession::Generation generation)
    {
        if (pending.size() >= help_queue_limit)
        {
            throw std::runtime_error("kh02_help_queue_limit");
        }
        pending.push_back({make_uobject_weak(callback), make_uobject_weak(world), execution, linkage, generation});
    }

    void Kh02HelpContinuations::tick(UObject* world, const Kh02MapSession& session)
    {
        if (!is_uobject_valid_fast(world))
        {
            return;
        }
        auto continuations = std::move(pending);
        pending.clear();
        for (const auto& continuation : continuations)
        {
            auto* target = continuation.callback.Get();
            if (continuation.world.Get() != world ||
                !session.accepts_continuation(continuation.generation, continuation.world.Get()) ||
                !is_uobject_valid_fast(target) || kh02_actor_world(target) != world)
            {
                continue;
            }
            Kh02ReflectedCall resume(target, continuation.execution.ToString().c_str(), "kh02_story",
                                     reflected_parameter_limit_bytes);
            if (resume.function->GetParmsSize() != linkage_parameter_bytes || continuation.linkage < 0 ||
                continuation.linkage >= resume.function->GetScript().Num())
            {
                throw std::runtime_error("kh02_help_continuation_layout");
            }
            std::memcpy(resume.parameters.data(), &continuation.linkage, linkage_parameter_bytes);
            resume.run();
            ++continuation_count;
        }
    }

    void Kh02HelpContinuations::clear()
    {
        pending.clear();
    }

    auto Kh02HelpContinuations::completed() const -> uint64_t
    {
        return continuation_count;
    }
} // namespace kh3ap
