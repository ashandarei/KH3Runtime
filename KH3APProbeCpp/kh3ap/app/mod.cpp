#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/app/mod.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UE4SSProgram.hpp>

namespace kh3ap
{
    Kh3ApRuntimeMod::Kh3ApRuntimeMod()
    {
        ModName = STR("KH3APProbeCpp");
        ModVersion = STR("2.0.0");
        ModDescription = STR("KH3 UE4SS runtime bridge for treasure, event, vbonus, and Moogle Shop checks.");
        ModAuthors = STR("Aesais");
    }

    Kh3ApRuntimeMod::~Kh3ApRuntimeMod() = default;

    auto Kh3ApRuntimeMod::on_unreal_init() -> void
    {
        runtime_.initialize();

        // F9 re-draws every character's colours in place. Judging a palette
        // change otherwise costs a relaunch, a save load and a walk back to
        // whichever character is being looked at.
        RC::UE4SSProgram::get_program().register_keydown_event(RC::Input::Key::F9, [this]() {
            runtime_.reroll_colors();
        });
        RC::Output::send<RC::LogLevel::Normal>(
                STR("[KH3APProbeCpp] Color hotkeys registered reroll=F9\n"));
    }

    auto Kh3ApRuntimeMod::on_update() -> void
    {
        // UE4SS invokes C++ mod updates from UE4SS-UpdateThread, not Unreal's
        // game thread. Runtime work is driven by the scanned native
        // UGameEngine::Tick hook installed by RuntimeComposition.
    }
} // namespace kh3ap
