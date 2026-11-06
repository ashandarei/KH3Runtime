#include "kh02_movie_source_continuations.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/World.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr std::size_t continuation_parameter_limit_bytes = 512;
        constexpr std::string_view gear_movie_prefix = "Rebellis02/gear_";

        struct ContinuationCall : Kh02ReflectedCall
        {
            ContinuationCall(UObject* target, const wchar_t* function)
                : Kh02ReflectedCall(target, function, "movie_bridge", continuation_parameter_limit_bytes)
            {
            }
        };
    } // namespace

    void Kh02MovieSourceContinuations::bind(UObject* target, bool gear_event)
    {
        script = make_uobject_weak(target);
        gear_finish = gear_event;
    }

    void Kh02MovieSourceContinuations::release_source()
    {
        script = {};
    }

    void Kh02MovieSourceContinuations::reset_waits()
    {
        ready_linkage = -1;
        finish_linkage = -1;
    }

    void Kh02MovieSourceContinuations::reset_request()
    {
        reset_waits();
        ready_function.clear();
        finish_function.clear();
        ready_sent = false;
        completed_waits = 0;
        terminal_finish_waits = 0;
    }

    void Kh02MovieSourceContinuations::retire()
    {
        release_source();
        ready_function.clear();
        finish_function.clear();
        gear_finish = false;
        ready_sent = false;
    }

    void Kh02MovieSourceContinuations::dispatch_ready()
    {
        auto* target = script.Get();
        if (!is_uobject_valid_fast(target))
        {
            throw std::runtime_error("movie_bridge_ready_target");
        }
        // Set before dispatch: the source can synchronously re-enter the bridge.
        ready_sent = true;
        ContinuationCall callback(target, ready_function.c_str());
        callback.integer(L"EntryPoint", ready_linkage);
        callback.run();
    }

    void Kh02MovieSourceContinuations::wait(UObject* caller, int linkage, const std::wstring& function,
                                            bool story_movie)
    {
        // The four original gear branches deliberately serialize linkage -1 and
        // terminate execution immediately afterward. Their Matinee requests Resume
        // at the whiteout event; there is no latent continuation to manufacture.
        const auto name = is_uobject_valid_fast(caller) ? caller->GetClassPrivate()->GetName() : L"";
        const bool story =
            story_movie && name.ends_with(L"_C") && function == L"ExecuteUbergraph_" + name.substr(0, name.size() - 2);
        if (script.Get() != caller || linkage < -1 || (!story && function != L"ExecuteUbergraph_dw_02_camera_ms030"))
        {
            throw std::runtime_error("movie_bridge_wait_identity");
        }
        ready_linkage = linkage;
        ready_function = function;
    }

    void Kh02MovieSourceContinuations::wait_finish(UObject* caller, int linkage, const std::wstring& function,
                                                   bool story_movie, bool completed, UObject* pawn, UObject* world)
    {
        const auto name = is_uobject_valid_fast(caller) ? caller->GetClassPrivate()->GetName() : L"";
        auto* fn = is_uobject_valid_fast(caller) ? caller->GetFunctionByNameInChain(function.c_str()) : nullptr;
        const bool gear = !story_movie && function == L"ExecuteUbergraph_dw_02_camera_ms030" &&
                          is_uobject_valid_fast(caller) &&
                          caller->GetPathName().starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_camera_ms030.");
        if ((!story_movie && !gear) || script.Get() != caller || !name.ends_with(L"_C") ||
            function != L"ExecuteUbergraph_" + name.substr(0, name.size() - 2) || !is_uobject_valid_fast(fn) ||
            linkage < (gear ? -1 : 0) || linkage >= fn->GetScript().Num())
        {
            throw std::runtime_error("movie_bridge_finish_wait_identity:linkage=" + std::to_string(linkage) +
                                     ":function=" + to_utf8(function) + ":caller=" +
                                     (is_uobject_valid_fast(caller) ? to_utf8(caller->GetPathName()) : "invalid"));
        }
        if (gear && linkage == -1)
        {
            // The original gear branch ends here, just like its preparation wait.
            // Its separate Gear*_FinishRemote event is dispatched only at real EOF.
            ++terminal_finish_waits;
            return;
        }
        if (completed)
        {
            auto* context = pawn;
            if (!is_uobject_valid_fast(context) || !is_uobject_valid_fast(world) ||
                static_cast<AActor*>(context)->GetWorld() != world || static_cast<AActor*>(caller)->GetWorld() != world)
            {
                throw std::runtime_error("movie_bridge_completed_wait_context");
            }
            ++completed_waits;
            ContinuationCall callback(caller, function.c_str());
            callback.integer(L"EntryPoint", linkage);
            callback.run();
            return;
        }
        finish_linkage = linkage;
        finish_function = function;
    }

    void Kh02MovieSourceContinuations::Completion::dispatch(std::string_view movie,
                                                            const std::function<bool()>& same_generation) const
    {
        if (gear_event)
        {
            if (!is_uobject_valid_fast(script.Get()))
            {
                throw std::runtime_error("movie_bridge_gear_finish_target");
            }
            const auto event =
                L"Gear" + std::wstring(1, wchar_t(movie[gear_movie_prefix.size()] - 'a' + 'A')) + L"_FinishRemote";
            ContinuationCall callback(script.Get(), event.c_str());
            if (callback.function->GetParmsSize())
            {
                throw std::runtime_error("movie_bridge_gear_finish_signature");
            }
            callback.run();
            if (!same_generation())
            {
                return;
            }
        }
        if (linkage >= 0)
        {
            if (!is_uobject_valid_fast(script.Get()))
            {
                throw std::runtime_error("movie_bridge_finish_target");
            }
            ContinuationCall callback(script.Get(), function.c_str());
            callback.integer(L"EntryPoint", linkage);
            callback.run();
        }
    }
} // namespace kh3ap
