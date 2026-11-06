#include "kh02_arrival_readiness.h"
#include "kh3ap/features/kh02/cutscenes/kh02_intro_presentation.h"
#include "kh3ap/features/kh02/diagnostics/kh02_runtime_inspection.h"

namespace kh3ap
{
    auto kh02_sample_arrival_readiness(RC::Unreal::UObject* context, RC::Unreal::UObject* world,
                                       Kh02IntroPresentation& intro_presentation) -> nlohmann::json
    {
        const auto view = intro_presentation.fade(context);
        bool settled = true;
        for (const auto& s : kh02_stream_states(world, L"/Game/Z"))
        {
            if (s["pending"].get<bool>() || s["requested_visible"].get<bool>() && !s["visible"].get<bool>())
            {
                settled = false;
            }
        }
        const bool ready = kh02_arrival_view_ready(settled, view["changing"].get<bool>(), view["amount"].get<float>());
        return {{"view", view}, {"settled", settled}, {"ready", ready}};
    }
} // namespace kh3ap
