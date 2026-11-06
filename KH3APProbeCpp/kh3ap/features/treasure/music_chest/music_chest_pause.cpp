#include "kh3ap/features/treasure/music_chest/music_chest_pause.h"
#include "kh3ap/features/treasure/music_chest/music_chest.h"
#include "kh3ap/features/treasure/music_chest/music_chest_service.h"
#include "kh3ap/features/treasure/music_chest/music_pause_policy.h"
#include "kh3ap/features/treasure/music_chest/mom_audio.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <stdexcept>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace music_chest;
    namespace
    {
        RuntimeMusicPausePolicy pause_policy;
        RuntimeMoMAudio* handoff_audio{};
        FWeakObjectPtr paused_world;
        bool pause_game(UObject* pawn, bool value)
        {
            auto* lib = loaded(L"/Script/Engine.Default__GameplayStatics");
            Call c(lib, L"SetGamePaused");
            if (!write_object_function_param(c.fn, c.data.data(), L"WorldContextObject", pawn))
            {
                throw std::runtime_error("music_pause_context");
            }
            c.boolean(L"bPaused", value);
            c.run();
            return static_cast<FBoolProperty*>(c.field(L"ReturnValue"))
                ->GetPropertyValueInContainer(c.data.data());
        }
    } // namespace
    namespace music_chest
    {
        bool game_paused(UObject* pawn)
        {
            Call c(loaded(L"/Script/Engine.Default__GameplayStatics"), L"IsGamePaused");
            if (!write_object_function_param(c.fn, c.data.data(), L"WorldContextObject", pawn))
            {
                throw std::runtime_error("music_pause_context");
            }
            c.run();
            return static_cast<FBoolProperty*>(c.field(L"ReturnValue"))
                ->GetPropertyValueInContainer(c.data.data());
        }
        bool foreground()
        {
            DWORD pid{};
            GetWindowThreadProcessId(GetForegroundWindow(), &pid);
            return pid == GetCurrentProcessId();
        }
        void set_handoff_audio(RuntimeMoMAudio* audio)
        {
            handoff_audio = audio;
        }
        bool pause_active()
        {
            return pause_policy.active;
        }
        void update_pause(UObject* pawn, RuntimeMoMAudio& audio)
        {
            if (pause_policy.active)
            {
                if (!is_uobject_valid_fast(paused_world.Get()))
                {
                    pause_policy.finish();
                    audio.set_muted(false);
                }
                else if (is_uobject_valid_fast(pawn) && world(pawn) == paused_world.Get() &&
                         pause_policy.should_release(foreground(), Clock::now()))
                {
                    try
                    {
                        if (pause_game(pawn, false))
                        {
                            pause_policy.finish();
                            audio.set_muted(false);
                        }
                    }
                    catch (...)
                    {
                    }
                }
            }
        }
    } // namespace music_chest
    bool begin_music_chest_pause(UObject* pawn)
    {
        if (pause_policy.active || !handoff_audio || !is_uobject_valid_fast(pawn) ||
            !foreground() || game_paused(pawn))
        {
            return false;
        }
        if (!pause_game(pawn, true))
        {
            return false;
        }
        paused_world = world(pawn);
        pause_policy.begin(Clock::now());
        handoff_audio->set_muted(true);
        return true;
    }
} // namespace kh3ap
