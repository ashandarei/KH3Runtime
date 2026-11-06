#include "kh02_castle_boss_streams.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <cstring>
#include <stdexcept>
#include <utility>
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/story/kh02_story_flags.h"
#include "kh3ap/features/kh02/progress/kh02_story_checkpoints.h"
#include <Unreal/Property/FArrayProperty.hpp>

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

        constexpr std::size_t native_array_header_bytes = 16;
        constexpr int minimum_host_streams = 32;
        constexpr int maximum_host_streams = 64;
        constexpr int required_boss_streams = 2;
        constexpr int encounter_gear_count = 5;

        void set_flag(UObject* o, const wchar_t* n, bool v)
        {
            if (!is_uobject_valid_fast(o) || !write_bool_function_param(o->GetPropertyByNameInChain(n), o, v))
            {
                throw std::runtime_error("kh02_story_set_flag");
            }
        }

    } // namespace

    auto kh02_prepare_castle_boss_streams(UObject* w, const Kh02StoryFlags& source_flags, Json& encounter_proof) -> bool
    {
        if (!is_uobject_valid_fast(w))
        {
            throw std::runtime_error("kh02_boss_world_expired");
        }
        const int checkpoint = source_flags.value(L"gameflow_DW");
        if (!kh02_castle_boss_streams_required(checkpoint, source_flags.value(L"gameflow_DW_GEAR_GETCOUNT")))
        {
            throw std::runtime_error("kh02_boss_requires_five_gears");
        }

        struct Array
        {
            UObject** data;
            int32_t count, capacity;
        };

        auto* property = w->GetPropertyByNameInChain(L"StreamingLevels");
        if (!property || !property->IsA<FArrayProperty>() || property->GetElementSize() != native_array_header_bytes)
        {
            throw std::runtime_error("kh02_boss_stream_layout");
        }
        auto* a = property->ContainerPtrToValuePtr<Array>(w);
        if (a->count < minimum_host_streams || a->count > maximum_host_streams || a->capacity < a->count || !a->data)
        {
            throw std::runtime_error("kh02_boss_stream_bounds");
        }
        int found = 0, ready = 0;
        for (int i = 0; i < a->count; ++i)
        {
            auto* stream = a->data[i];
            if (!is_uobject_valid_fast(stream) || stream->GetOuterPrivate() != w)
            {
                continue;
            }
            ReflectedCall get(stream, L"GetWorldAssetPackageFName");
            get.run();
            auto* name = get.param(L"ReturnValue");
            if (!name || name->GetClass().GetName() != L"NameProperty" || name->GetElementSize() != sizeof(FName))
            {
                throw std::runtime_error("kh02_boss_stream_name");
            }
            const auto package = name->ContainerPtrToValuePtr<FName>(get.parameters.data())->ToString();
            if (package != L"/Game/Zevels/dw/dw_02/umap/dw_02_camera_ms040" &&
                package != L"/Game/Zevels/dw/dw_02/umap/dw_02_enemy_09")
            {
                continue;
            }
            ++found;
            set_flag(stream, L"bShouldBeLoaded", true);
            set_flag(stream, L"bShouldBeVisible", true);
            ReflectedCall visible(stream, L"IsLevelVisible"), pending(stream, L"IsStreamingStatePending");
            visible.run();
            pending.run();
            if (visible.boolean_result() && !pending.boolean_result())
            {
                ++ready;
            }
        }
        if (found != required_boss_streams)
        {
            throw std::runtime_error("kh02_boss_streams_missing");
        }
        encounter_proof["enabled_at_counter"] = encounter_gear_count;
        return ready == required_boss_streams;
    }

} // namespace kh3ap
