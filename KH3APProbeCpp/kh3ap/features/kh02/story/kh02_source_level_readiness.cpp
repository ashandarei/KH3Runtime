#include "kh02_source_level_readiness.h"
#include "kh02_map_session.h"
#include "kh3ap/features/kh02/diagnostics/kh02_runtime_inspection.h"
#include <Unreal/Property/FArrayProperty.hpp>

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

        constexpr std::size_t castle_source_stream_count = 4;
        constexpr std::size_t castle_gameflow_stream_index = 0;
        constexpr std::size_t castle_gimmick_stream_index = 1;
        constexpr std::size_t castle_opening_stream_index = 3;
        constexpr int castle_challenge_stream_count = 3;
        constexpr int minimum_mirror_stream_count = 16;
        constexpr int maximum_mirror_stream_count = 64;
        constexpr int first_mirror_source_stream = 4;
        constexpr int last_mirror_source_stream = 6;
        constexpr std::size_t native_array_header_bytes = 16;

    } // namespace

    bool kh02_castle_source_levels_ready(UObject* world, std::span<const FWeakObjectPtr> flow_streams, bool entry_retry,
                                         bool first_castle_intro, Kh02CastleLevelBindings bindings, Json& loading_proof)
    {
        if (!is_uobject_valid_fast(world) || flow_streams.size() != castle_source_stream_count)
        {
            return false;
        }
        FWeakObjectPtr story_script;
        FWeakObjectPtr gimmick_script;
        for (size_t index = 0; index < flow_streams.size(); ++index)
        {
            auto* stream = flow_streams[index].Get();
            if (!is_uobject_valid_fast(stream))
            {
                throw std::runtime_error("kh02_entry_stream_expired");
            }
            ReflectedCall loaded(stream, L"IsLevelLoaded");
            ReflectedCall visible(stream, L"IsLevelVisible");
            loaded.run();
            visible.run();
            if (!loaded.boolean_result() ||
                (!entry_retry || index != castle_opening_stream_index) && !visible.boolean_result())
            {
                return false;
            }
            auto* level = try_read_reflected_object_property_value(stream, L"LoadedLevel");
            auto* script = try_read_reflected_object_property_value(level, L"LevelScriptActor");
            if (index == castle_gameflow_stream_index)
            {
                story_script = make_uobject_weak(script);
                if (!is_uobject_valid_fast(story_script.Get()))
                {
                    loading_proof["gameflow_script_publication"] = {{"level_valid", is_uobject_valid_fast(level)},
                                                                    {"script_valid", is_uobject_valid_fast(script)}};
                }
            }
            if (index == castle_gimmick_stream_index)
            {
                gimmick_script = make_uobject_weak(script);
                if (!is_uobject_valid_fast(gimmick_script.Get()))
                {
                    loading_proof["gimmick_script_publication"] = {{"level_valid", is_uobject_valid_fast(level)},
                                                                   {"script_valid", is_uobject_valid_fast(script)}};
                }
            }
        }
        auto* script = story_script.Get();
        auto* gimmick = gimmick_script.Get();
        if (!is_uobject_valid_fast(script) || !is_uobject_valid_fast(gimmick))
        {
            // OnLevelShown can arrive before LevelScriptActor is published,
            // particularly while restoring the boss streams. The existing
            // bounded entry preparation retries this incomplete publication.
            // Do not replace a live binding with a partial observation.
            loading_proof["source_scripts_pending"] = {{"gameflow", !is_uobject_valid_fast(script)},
                                                       {"gimmick", !is_uobject_valid_fast(gimmick)}};
            return false;
        }
        bindings.story_script = story_script;
        bindings.gimmick_script = gimmick_script;
        bindings.session.bind_source_script(script);
        loading_proof["source_scripts_ready"] = true;
        if (first_castle_intro)
        {
            const auto alley = kh02_stream_states(world, L"/Game/Zaps/dw/umap/dw_02/dw_02_Area_Alley");
            if (alley.size() != 1)
            {
                throw std::runtime_error("kh02_castle_intro_alley_missing");
            }
            if (!alley[0].at("loaded").get<bool>() || !alley[0].at("visible").get<bool>() ||
                alley[0].at("pending").get<bool>())
            {
                return false;
            }
            loading_proof["intro_alley_ready"] = true;
        }
        // The gameflow opens challenges through RemoteEvent during init.
        // Both listeners must be visible before that one-time event fires.
        const auto challenge_levels = kh02_stream_states(world, L"/Game/Zevels/dw/dw_02/umap/dw_02_");
        int challenges_ready = 0;
        for (const auto& level : challenge_levels)
        {
            const auto package = level.at("package").get<std::string>();
            if (package != "/Game/Zevels/dw/dw_02/umap/dw_02_challenge" &&
                package != "/Game/Zevels/dw/dw_02/umap/dw_02_camera_ms201" &&
                package != "/Game/Zevels/dw/dw_02/umap/dw_02_camera_ms200")
            {
                continue;
            }
            if (!level.at("loaded").get<bool>() || !level.at("visible").get<bool>() || level.at("pending").get<bool>())
            {
                return false;
            }
            ++challenges_ready;
        }
        if (challenges_ready != castle_challenge_stream_count)
        {
            throw std::runtime_error("kh02_castle_challenge_levels_missing");
        }
        loading_proof["challenge_levels_ready"] = challenges_ready;

        return true;
    }

    bool kh02_mirror_source_levels_ready(UObject* world, FWeakObjectPtr& within_script, Kh02MapSession& map_session)
    {
        struct SourceStreamArray
        {
            UObject** data;
            int32_t count;
            int32_t capacity;
        };

        auto* property = world->GetPropertyByNameInChain(L"StreamingLevels");
        if (!property || !property->IsA<FArrayProperty>() || property->GetElementSize() != native_array_header_bytes)
        {
            throw std::runtime_error("kh02_within_streams_layout");
        }
        auto* streams = property->ContainerPtrToValuePtr<SourceStreamArray>(world);
        if (!streams->data || streams->count < minimum_mirror_stream_count ||
            streams->count > maximum_mirror_stream_count || streams->capacity < streams->count)
        {
            throw std::runtime_error("kh02_within_streams_count");
        }
        for (int stream_index = first_mirror_source_stream; stream_index <= last_mirror_source_stream; ++stream_index)
        {
            auto* source_stream = streams->data[stream_index];
            if (!is_uobject_valid_fast(source_stream))
            {
                throw std::runtime_error("kh02_within_stream_expired");
            }
            ReflectedCall visible(source_stream, L"IsLevelVisible");
            visible.run();
            if (!visible.boolean_result())
            {
                return false;
            }
            if (stream_index == first_mirror_source_stream)
            {
                auto* level = try_read_reflected_object_property_value(source_stream, L"LoadedLevel");
                within_script = make_uobject_weak(try_read_reflected_object_property_value(level, L"LevelScriptActor"));
                map_session.bind_source_script(within_script.Get());
            }
        }

        return true;
    }

} // namespace kh3ap
