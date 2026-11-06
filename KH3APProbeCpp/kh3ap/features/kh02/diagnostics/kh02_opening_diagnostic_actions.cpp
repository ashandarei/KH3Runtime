#include "kh02_diagnostic_action_helpers.h"
#include "kh3ap/features/kh02/diagnostics/kh02_capture_path.h"
#include "kh3ap/features/kh02/cutscenes/kh02_event_aqua.h"

namespace kh3ap
{
    using namespace kh02diagnostic_detail;

    auto Kh02OpeningDiagnosticActions::execute(UObject* pawn, Action action, Kh02OpeningActionServices services)
        -> std::optional<std::string>
    {
        auto& opening_scene = services.opening_scene;
        auto& event_aqua = services.event_aqua;
        opening_scene.bind_context(pawn, services.context);
        if (action == Action::Capture)
        {
            const auto file = kh02_capture_file(L"kh02-opening-scene.png");
            ReflectedCall capture(kh02_require_loaded_object(L"/Script/Engine.Default__KismetSystemLibrary"),
                                  L"ExecuteConsoleCommand");
            capture.object(L"WorldContextObject", pawn);
            capture.string(L"Command", kh02_capture_command(file));
            capture.run();
            auto snapshot = services.snapshot();
            snapshot["screenshot_file"] = to_utf8(file.wstring());
            return snapshot.dump();
        }
        if (action == Action::Inspect)
        {
            return services.snapshot().dump();
        }
        if (action == Action::EyesLod0 || action == Action::EyesAutoLod)
        {
            auto* a = event_aqua.actor();
            if (!is_uobject_valid_fast(a))
            {
                throw std::runtime_error("kh02_story_aqua_missing");
            }
            ReflectedCall force(try_read_reflected_object_property_value(a, L"MyMesh"), L"SetForcedLOD");
            force.integer(L"InNewForcedLOD", action == Action::EyesLod0 ? 1 : 0);
            force.run();
            return services.snapshot().dump();
        }
        if (action == Action::Load)
        {
            if (!opening_scene.prepare(pawn, services.context))
            {
                return std::nullopt;
            }
        }
        else if (action == Action::Unload)
        {
            if (!opening_scene.unload(pawn, services.context))
            {
                return std::nullopt;
            }
        }
        else if (action == Action::Start)
        {
            opening_scene.start(pawn, services.context);
        }
        else if (action == Action::Skip)
        {
            opening_scene.skip(pawn, services.context);
        }
        return services.snapshot().dump();
    }

} // namespace kh3ap
