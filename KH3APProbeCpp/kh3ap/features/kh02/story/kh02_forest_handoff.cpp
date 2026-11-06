#include "kh02_forest_handoff.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;

    namespace
    {
        constexpr std::size_t reflected_parameter_limit_bytes = 2'048;

        struct ReflectedCall : Kh02ReflectedCall
        {
            ReflectedCall(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "kh02_story", reflected_parameter_limit_bytes)
            {
            }
        };

    } // namespace

    void Kh02ForestHandoff::enqueue(UObject* world, std::wstring tag)
    {
        forest_finish_world = make_uobject_weak(world);
        forest_finish_tag = std::move(tag);
    }

    void Kh02ForestHandoff::clear()
    {
        forest_finish_world = {};
        forest_finish_tag.clear();
    }

    auto Kh02ForestHandoff::tick(UObject* pawn, UObject* world, Json& forest_story_proof) -> std::optional<std::string>
    {
        if (!forest_finish_tag.empty())
        {
            const auto tag = std::move(forest_finish_tag);
            forest_finish_tag.clear();
            try
            {
                if (forest_finish_world.Get() != world ||
                    world->GetPathName() != L"/Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest")
                {
                    throw std::runtime_error("kh02_forest_finish_world");
                }
                // The source normally animates its playable Aqua to this
                // point. Our event Aqua is separate from Sora. Use the
                // source's own Skip endpoint after a real natural ending;
                // native Skip already performs its own warp.
                ReflectedCall warp(pawn, L"WarpToTaggedPlayerStart");
                warp.name(L"InTag", tag.c_str());
                warp.boolean(L"bResetCamera", true);
                warp.boolean(L"bStateClear", true);
                warp.run();
                if (!warp.boolean_result())
                {
                    throw std::runtime_error("kh02_forest_finish_warp");
                }
                forest_story_proof["sora_end_tag"] = to_utf8(tag);
                forest_story_proof["sora_warped"] = true;
            }
            catch (const std::exception& e)
            {
                forest_finish_world = {};
                return std::string(e.what());
            }
            forest_finish_world = {};
        }

        return std::nullopt;
    }

} // namespace kh3ap
