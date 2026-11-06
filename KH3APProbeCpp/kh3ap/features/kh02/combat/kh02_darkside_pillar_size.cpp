#include "kh02_darkside_pillar_size.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <cstring>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        using SizeVector = Kh02PillarSizeVector;
        using SizeValues = Kh02PillarSizeValues;
        constexpr std::size_t maximum_struct_inheritance_depth = 16;
        constexpr std::int32_t maximum_lookup_capacity = 16;
        constexpr std::uint8_t constant_distribution_operation = 1;
        constexpr std::uint8_t constant_distribution_entries = 1;
        constexpr std::int32_t size_vector_dimensions = 3;
        constexpr auto host_mesh_path = L"/Game/Effects/trescom/mdl/omd_grd_100x01_0.omd_grd_100x01_0";

        struct SizeLayout
        {
            UObject* distribution;
            std::array<float*, 4> values;

            auto read() const -> SizeValues
            {
                SizeValues result{};
                for (std::size_t index = 0; index < values.size(); ++index)
                {
                    std::memcpy(result[index].data(), values[index], sizeof(SizeVector));
                }
                return result;
            }

            void write(const SizeValues& next) const
            {
                for (std::size_t index = 0; index < values.size(); ++index)
                {
                    std::memcpy(values[index], next[index].data(), sizeof(SizeVector));
                }
            }
        };

        auto checked_field(UScriptStruct* structure, const wchar_t* name, const wchar_t* kind,
                           std::int32_t expected_size = 0) -> FProperty*
        {
            FProperty* field = nullptr;
            auto* owner = structure;
            for (std::size_t depth = 0;
                 is_uobject_a_fast(owner, UScriptStruct::StaticClass()) && depth < maximum_struct_inheritance_depth;
                 ++depth)
            {
                field = find_struct_field_by_name(owner, name);
                if (field)
                {
                    break;
                }
                owner = static_cast<UScriptStruct*>(owner->GetSuperStruct());
            }
            if (!field || field->GetClass().GetName() != kind || field->GetOffset_Internal() < 0 ||
                (expected_size && field->GetElementSize() != expected_size))
            {
                throw std::runtime_error("kh02_pillar_size_schema:" + to_utf8(name));
            }
            return field;
        }

        auto checked_structure(FProperty* field, const wchar_t* expected_name) -> UScriptStruct*
        {
            if (!field || field->GetClass().GetName() != L"StructProperty")
            {
                throw std::runtime_error("kh02_pillar_size_struct_property");
            }
            auto* structure = static_cast<FStructProperty*>(field)->GetStruct();
            if (!is_uobject_valid_fast(structure) || structure->GetName() != expected_name)
            {
                throw std::runtime_error("kh02_pillar_size_struct_type");
            }
            return structure;
        }

        auto size_layout(UObject* module) -> SizeLayout
        {
            auto* start = module->GetPropertyByNameInChain(L"StartSize");
            auto* raw_type = checked_structure(start, L"RawDistributionVector");
            auto* raw = start->ContainerPtrToValuePtr<void>(module);
            const auto scalar = [&](const wchar_t* name) -> float
            {
                return *checked_field(raw_type, name, L"FloatProperty", sizeof(float))
                            ->ContainerPtrToValuePtr<float>(raw);
            };
            if (scalar(L"MinValue") != kh02_pillar_authored_size[2] ||
                scalar(L"MaxValue") != kh02_pillar_authored_size[0])
            {
                throw std::runtime_error("kh02_pillar_size_scalar_range");
            }
            const auto vector = [&](UScriptStruct* owner_type, void* owner, const wchar_t* name) -> float*
            {
                auto* field = checked_field(owner_type, name, L"StructProperty", sizeof(SizeVector));
                auto* vector_type = checked_structure(field, L"Vector");
                const std::array<const wchar_t*, 3> axes{L"X", L"Y", L"Z"};
                for (std::size_t index = 0; index < axes.size(); ++index)
                {
                    auto* axis = checked_field(vector_type, axes[index], L"FloatProperty", sizeof(float));
                    if (axis->GetOffset_Internal() != index * sizeof(float))
                    {
                        throw std::runtime_error("kh02_pillar_size_vector_axes");
                    }
                }
                return field->ContainerPtrToValuePtr<float>(owner);
            };
            auto* distribution = *checked_field(raw_type, L"Distribution", L"ObjectProperty", sizeof(UObject*))
                                      ->ContainerPtrToValuePtr<UObject*>(raw);
            if (!is_uobject_valid_fast(distribution) || !is_uobject_valid_fast(distribution->GetClassPrivate()) ||
                distribution->GetClassPrivate()->GetPathName() != L"/Script/Engine.DistributionVectorConstant")
            {
                throw std::runtime_error("kh02_pillar_size_distribution_type");
            }
            if (distribution->GetOuterPrivate() != module ||
                distribution->GetName() != L"DistributionVectorConstantMeshStartSize")
            {
                throw std::runtime_error("kh02_pillar_size_distribution_owner");
            }
            auto* constant = distribution->GetPropertyByNameInChain(L"Constant");
            checked_structure(constant, L"Vector");
            if (constant->GetElementSize() != sizeof(SizeVector))
            {
                throw std::runtime_error("kh02_pillar_size_constant_layout");
            }
            auto* table_field = checked_field(raw_type, L"Table", L"StructProperty");
            auto* table_type = checked_structure(table_field, L"DistributionLookupTable");
            auto* table = table_field->ContainerPtrToValuePtr<void>(raw);
            const auto byte = [&](const wchar_t* name) -> std::uint8_t
            {
                return *checked_field(table_type, name, L"ByteProperty", 1)
                            ->ContainerPtrToValuePtr<std::uint8_t>(table);
            };
            if (byte(L"Op") != constant_distribution_operation ||
                byte(L"EntryCount") != constant_distribution_entries ||
                byte(L"EntryStride") != size_vector_dimensions || byte(L"SubEntryStride") != 0 ||
                byte(L"LockFlag") != 0)
            {
                throw std::runtime_error("kh02_pillar_size_constant_table");
            }
            auto* values = checked_field(table_type, L"Values", L"ArrayProperty");
            auto* inner = static_cast<FArrayProperty*>(values)->GetInner();
            if (!inner || inner->GetClass().GetName() != L"FloatProperty" || inner->GetElementSize() != sizeof(float))
            {
                throw std::runtime_error("kh02_pillar_size_table_element");
            }

            struct FloatArray
            {
                float* data;
                std::int32_t count;
                std::int32_t capacity;
            } array{};

            if (values->GetElementSize() != sizeof(array))
            {
                throw std::runtime_error("kh02_pillar_size_table_array_layout");
            }
            std::memcpy(&array, values->ContainerPtrToValuePtr<void>(table), sizeof(array));
            if (!array.data || array.count != size_vector_dimensions || array.capacity < size_vector_dimensions ||
                array.capacity > maximum_lookup_capacity)
            {
                throw std::runtime_error("kh02_pillar_size_table_bounds");
            }
            return {distribution,
                    {vector(raw_type, raw, L"MinValueVec"), vector(raw_type, raw, L"MaxValueVec"),
                     constant->ContainerPtrToValuePtr<float>(distribution), array.data}};
        }

    } // namespace

    Kh02DarksidePillarSize::~Kh02DarksidePillarSize()
    {
        reset();
    }

    auto Kh02DarksidePillarSize::ready() const -> bool
    {
        return prepared && is_uobject_valid_fast(module.Get()) && is_uobject_valid_fast(distribution.Get()) &&
               is_uobject_valid_fast(host_mesh.Get());
    }

    void Kh02DarksidePillarSize::prepare(UObject* source_size_module, UObject* native_mesh)
    {
        if (ready())
        {
            if (module.Get() != source_size_module || host_mesh.Get() != native_mesh)
            {
                throw std::runtime_error("kh02_pillar_size_identity_changed");
            }
            return;
        }
        reset();
        auto* mesh_type = is_uobject_valid_fast(native_mesh) ? native_mesh->GetClassPrivate() : nullptr;
        if (!is_uobject_valid_fast(mesh_type) || mesh_type->GetPathName() != L"/Script/Engine.StaticMesh" ||
            native_mesh->GetPathName() != host_mesh_path)
        {
            throw std::runtime_error("kh02_pillar_size_requires_host_mesh");
        }
        auto* type = is_uobject_valid_fast(source_size_module) ? source_size_module->GetClassPrivate() : nullptr;
        auto* effect = is_uobject_valid_fast(source_size_module) ? source_size_module->GetOuterPrivate() : nullptr;
        if (!is_uobject_valid_fast(type) || type->GetPathName() != L"/Script/Engine.ParticleModuleSize" ||
            source_size_module->GetName() != L"ParticleModuleSize_24" || !is_uobject_valid_fast(effect) ||
            effect->GetPathName() != L"/Game/Zffects/enemy/e_dw407/par/ve_dw407_lsr_bem0.ve_dw407_lsr_bem0")
        {
            throw std::runtime_error("kh02_pillar_size_source_module");
        }
        const auto layout = size_layout(source_size_module);
        const auto current = layout.read();
        const auto replacement = kh02_pillar_size_replacement(current);
        if (!replacement)
        {
            throw std::runtime_error("kh02_pillar_size_authored_value");
        }
        module = make_uobject_weak(source_size_module);
        distribution = make_uobject_weak(layout.distribution);
        host_mesh = make_uobject_weak(native_mesh);
        if (module.Get() != source_size_module || distribution.Get() != layout.distribution ||
            host_mesh.Get() != native_mesh)
        {
            reset();
            throw std::runtime_error("kh02_pillar_size_weak_binding");
        }
        original = current;
        applied = *replacement;
        prepared = true;
        layout.write(applied);
        if (layout.read() != applied)
        {
            reset();
            throw std::runtime_error("kh02_pillar_size_readback");
        }
    }

    void Kh02DarksidePillarSize::reset() noexcept
    {
        try
        {
            if (prepared && is_uobject_valid_fast(module.Get()) && is_uobject_valid_fast(distribution.Get()))
            {
                const auto layout = size_layout(module.Get());
                if (layout.distribution == distribution.Get() && layout.read() == applied)
                {
                    layout.write(original);
                }
            }
        }
        catch (...)
        {
        }
        module = nullptr;
        distribution = nullptr;
        host_mesh = nullptr;
        prepared = false;
    }
} // namespace kh3ap
