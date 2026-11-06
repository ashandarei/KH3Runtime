#include "kh02_event_aqua_materials.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/bridge/utilities.h"

#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

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

        constexpr int inner_eye_lens_slot = 1;
        constexpr int outer_eye_lens_slot = 3;

        std::string path(UObject* object)
        {
            return is_uobject_valid_fast(object) ? to_utf8(object->GetPathName()) : "";
        }

        void adapt_eye_layer(UObject* mesh, int index, UObject* shader, const char* expected,
                             const char* expected_mirror)
        {
            ReflectedCall original(mesh, L"GetMaterial");
            original.integer(L"ElementIndex", index);
            original.run();
            auto* source = original.result();
            auto* parent = try_read_reflected_object_property_value(source, L"Parent");
            auto* adapted = source;
            if (!is_uobject_valid_fast(shader) ||
                (parent != shader && path(parent) != expected && path(parent) != expected_mirror))
            {
                throw std::runtime_error("kh02_story_eye_material_identity");
            }
            // Preserve source parameters and the native animation cache. Repeated
            // Play calls reuse the adapted instances instead of allocating more.
            if (parent != shader)
            {
                ReflectedCall create(mesh, L"CreateDynamicMaterialInstance");
                create.integer(L"ElementIndex", index);
                create.object(L"SourceMaterial", shader);
                create.run();
                adapted = create.result();
                if (!is_uobject_valid_fast(adapted))
                {
                    throw std::runtime_error("kh02_story_eye_lens_create");
                }
                ReflectedCall copy(adapted, L"K2_CopyMaterialInstanceParameters");
                copy.object(L"Source", source);
                copy.run();
            }
            auto** cached = kh02_native_material_slot(mesh, index);
            if (cached)
            {
                if (*cached != source && *cached != adapted)
                {
                    throw std::runtime_error("kh02_story_material_cache_owner");
                }
                *cached = adapted;
            }
        }

    } // namespace

    void kh02_adapt_event_aqua_eyes(UObject* actor)
    {
        auto* mesh = try_read_reflected_object_property_value(actor, L"MyMesh");
        auto* lens = find_loaded_runtime_asset(L"/Game/Shader/Material_set/SQEX_EyeLens.SQEX_EyeLens");
        if (!is_uobject_valid_fast(mesh))
        {
            throw std::runtime_error("kh02_story_eye_mesh_missing");
        }
        if (!is_uobject_valid_fast(lens))
        {
            throw std::runtime_error("kh02_story_native_eye_lens_missing");
        }
        for (int index : {inner_eye_lens_slot, outer_eye_lens_slot})
        {
            const auto expected = index == inner_eye_lens_slot
                                      ? "/Game/Zharacter/pc/p_ex002/mdl/mat0/p_ex002_E_mat1_Inst.p_ex002_E_mat1_Inst"
                                      : "/Game/Zharacter/pc/p_ex002/mdl/mat0/p_ex002_E_mat2_Inst.p_ex002_E_mat2_Inst";
            const auto expected_mirror = index == inner_eye_lens_slot
                                             ? "/Game/Zharacter/pc/p_ex002/mdl/mat0/NonStateMaterial/"
                                               "p_ex002_E_mat1_NS_Inst.p_ex002_E_mat1_NS_Inst"
                                             : "/Game/Zharacter/pc/p_ex002/mdl/mat0/NonStateMaterial/"
                                               "p_ex002_E_mat2_NS_Inst.p_ex002_E_mat2_NS_Inst";
            adapt_eye_layer(mesh, index, lens, expected, expected_mirror);
        }
    }

    void kh02_adapt_mirror_aqua_eyes(UObject* actor)
    {
        kh02_adapt_event_aqua_eyes(actor);
        constexpr int eyelashes_slot = 8;
        auto* mesh = try_read_reflected_object_property_value(actor, L"MyMesh");
        auto* eyelashes = find_loaded_runtime_asset(L"/Game/Shader/Material_set/SQEX_BaseEyelashes.SQEX_BaseEyelashes");
        constexpr auto source_eyelashes = "/Game/Zharacter/pc/p_ex002/mdl/mat0/NonStateMaterial/"
                                          "p_ex002_E_mat3_NS_Inst.p_ex002_E_mat3_NS_Inst";
        adapt_eye_layer(mesh, eyelashes_slot, eyelashes, source_eyelashes, source_eyelashes);
    }

} // namespace kh3ap
