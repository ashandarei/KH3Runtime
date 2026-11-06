#include "kh02_castle_scenery.h"
#include "kh02_castle_scenery_plan.h"
#include "kh3ap/features/kh02/story/kh02_story_flags.h"
#include "kh02_mapset.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/bridge/utilities.h"
#include <utility>

namespace kh3ap
{
    using namespace RC::Unreal;

    void kh02_restore_castle_scenery(UObject* script, UObject* gimmick, const Kh02StoryFlags& flags, bool backtrack,
                                     nlohmann::json& loading_proof)
    {
        constexpr int restore_mapset_request_base = 0x4b483700;
        constexpr std::size_t parameter_limit_bytes = 2'048;
        const auto commands = kh02_castle_scenery_plan(flags.value(L"gameflow_DW"), backtrack,
                                                       [&](const wchar_t* key) { return flags.value(key); });
        if (commands.empty())
        {
            return;
        }
        auto* mapset = find_loaded_runtime_asset(L"/Game/Zevels/dw/dw_02/mapset/ms_dw_02.ms_dw_02");
        int request_id = restore_mapset_request_base;
        auto applied = nlohmann::json::array();
        for (const auto command : commands)
        {
            kh02_apply_mapset(script, mapset, command.data(), request_id);
            ++request_id;
            applied.push_back(to_utf8(command));
        }
        Kh02ReflectedCall check_gears(gimmick, L"dw_02_gameflow_REvt_CheckGear", "kh02_story", parameter_limit_bytes);
        check_gears.run();
        Kh02ReflectedCall show(gimmick, L"dw_02_gameflow_REvt_showGear", "kh02_story", parameter_limit_bytes);
        show.run();
        loading_proof["restored_scenery"] = std::move(applied);
    }
} // namespace kh3ap
