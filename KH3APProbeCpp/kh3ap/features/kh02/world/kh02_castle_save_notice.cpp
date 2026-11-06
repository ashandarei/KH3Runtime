#include "kh02_castle_save_notice.h"
#include <Unreal/UObjectGlobals.hpp>

#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
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

        std::string path(UObject* object)
        {
            return is_uobject_valid_fast(object) ? to_utf8(object->GetPathName()) : "";
        }

    } // namespace

    auto kh02_disable_castle_save_notice(UObject* world, UObject* script, Json& loading_proof) -> UObject*
    {
        std::vector<UObject*> saves;
        UObjectGlobals::FindAllOf(L"g_ex_SavePoint_C", saves);
        UObject* save = nullptr;
        for (auto* candidate : saves)
        {
            if (is_uobject_valid_fast(candidate) && candidate->GetName() == L"g_ex_SavePoint_C_1" &&
                kh02_actor_world(candidate) == world)
            {
                if (save)
                {
                    throw std::runtime_error("kh02_entry_savepoint_ambiguous");
                }
                save = candidate;
            }
        }
        if (!is_uobject_valid_fast(save))
        {
            throw std::runtime_error("kh02_entry_savepoint_missing");
        }
        // This source trigger opens a normal message, independently of the
        // OpenGameHelp adapter. Disable only its overlap before releasing the
        // loading screen; leave the actual save-point actor operational.
        // Retry can leave the previous level alive until GC. Its full path is
        // identical, so resolve inside the newly bound source script's level.
        std::vector<UObject*> notices;
        UObjectGlobals::FindAllOf(L"lv_EventTriggerBox_C", notices);
        UObject* notice = nullptr;
        for (auto* candidate : notices)
        {
            if (is_uobject_valid_fast(candidate) && candidate->GetOuterPrivate() == script->GetOuterPrivate() &&
                candidate->GetName() == L"lv_EventTriggerBox_SavePointMes")
            {
                if (notice)
                {
                    throw std::runtime_error("kh02_entry_save_notice_trigger_ambiguous");
                }
                notice = candidate;
            }
        }
        if (!is_uobject_valid_fast(notice) || kh02_actor_world(notice) != world ||
            notice->GetName() != L"lv_EventTriggerBox_SavePointMes" ||
            !notice->GetPathName().starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_gameflow."))
        {
            throw std::runtime_error("kh02_entry_save_notice_trigger_missing");
        }
        ReflectedCall collision(notice, L"SetActorEnableCollision");
        collision.boolean(L"bNewActorEnableCollision", false);
        collision.run();
        ReflectedCall enabled(notice, L"GetActorEnableCollision");
        enabled.run();
        if (enabled.boolean_result())
        {
            throw std::runtime_error("kh02_entry_save_notice_trigger_still_enabled");
        }
        loading_proof["save_notice_trigger"] = path(notice);

        return save;
    }

} // namespace kh3ap
