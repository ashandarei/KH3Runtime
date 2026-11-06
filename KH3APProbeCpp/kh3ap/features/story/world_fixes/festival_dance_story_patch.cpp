#include "kh3ap/features/story/world_fixes/festival_dance_story_patch.h"

#include "kh3ap/core/objects/uobject_validity.h"

#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;

    auto runtime_festival_dance_story_object_kind_name(
            RuntimeFestivalDanceStoryObjectKind object_kind) noexcept -> std::wstring_view
    {
        switch (object_kind)
        {
        case RuntimeFestivalDanceStoryObjectKind::ClassDefaultObject:
            return STR("class_default_object");
        case RuntimeFestivalDanceStoryObjectKind::LiveInstance:
            return STR("live_instance");
        case RuntimeFestivalDanceStoryObjectKind::NotTarget:
        default:
            return STR("not_target");
        }
    }

    auto runtime_festival_dance_story_property_write_status_name(
            RuntimeFestivalDanceStoryPropertyWriteStatus status) noexcept -> std::wstring_view
    {
        switch (status)
        {
        case RuntimeFestivalDanceStoryPropertyWriteStatus::MissingProperty:
            return STR("missing_property");
        case RuntimeFestivalDanceStoryPropertyWriteStatus::WrongPropertyType:
            return STR("wrong_property_type");
        case RuntimeFestivalDanceStoryPropertyWriteStatus::WrongPropertySize:
            return STR("wrong_property_size");
        case RuntimeFestivalDanceStoryPropertyWriteStatus::MissingValuePointer:
            return STR("missing_value_pointer");
        case RuntimeFestivalDanceStoryPropertyWriteStatus::UnexpectedValue:
            return STR("unexpected_value");
        case RuntimeFestivalDanceStoryPropertyWriteStatus::AlreadySet:
            return STR("already_set");
        case RuntimeFestivalDanceStoryPropertyWriteStatus::Patched:
            return STR("patched");
        case RuntimeFestivalDanceStoryPropertyWriteStatus::VerificationFailed:
            return STR("verification_failed");
        case RuntimeFestivalDanceStoryPropertyWriteStatus::NotAttempted:
        default:
            return STR("not_attempted");
        }
    }

    auto try_patch_runtime_festival_dance_story_object(UObject* object)
            -> RuntimeFestivalDanceStoryObjectPatchResult
    {
        RuntimeFestivalDanceStoryObjectPatchResult result{};
        for (size_t index = 0; index < runtime_festival_dance_story_property_specs.size(); ++index)
        {
            result.writes[index].property_name =
                    runtime_festival_dance_story_property_specs[index].property_name;
            result.writes[index].requested_value =
                    runtime_festival_dance_story_property_specs[index].fast_value;
        }

        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return result;
        }

        result.object_name = object->GetFullName();
        UClass* const object_class = object->GetClassPrivate();
        if (!object_class)
        {
            return result;
        }
        result.object_kind = classify_runtime_festival_dance_story_object(
                object_class->GetName(),
                result.object_name);
        if (result.object_kind == RuntimeFestivalDanceStoryObjectKind::NotTarget)
        {
            return result;
        }

        std::array<int32_t*, runtime_festival_dance_story_property_specs.size()> value_pointers{};
        bool validation_succeeded = true;
        for (size_t index = 0; index < runtime_festival_dance_story_property_specs.size(); ++index)
        {
            const RuntimeFestivalDanceStoryPropertySpec& spec =
                    runtime_festival_dance_story_property_specs[index];
            RuntimeFestivalDanceStoryPropertyWriteResult& write = result.writes[index];
            FProperty* const property = object->GetPropertyByNameInChain(spec.property_name.data());
            if (!property)
            {
                write.status = RuntimeFestivalDanceStoryPropertyWriteStatus::MissingProperty;
                validation_succeeded = false;
                continue;
            }
            if (property->GetClass().GetName() != STR("IntProperty"))
            {
                write.status = RuntimeFestivalDanceStoryPropertyWriteStatus::WrongPropertyType;
                validation_succeeded = false;
                continue;
            }
            if (property->GetElementSize() != static_cast<int32_t>(sizeof(int32_t)))
            {
                write.status = RuntimeFestivalDanceStoryPropertyWriteStatus::WrongPropertySize;
                validation_succeeded = false;
                continue;
            }

            int32_t* const value = property->ContainerPtrToValuePtr<int32_t>(object);
            if (!value)
            {
                write.status = RuntimeFestivalDanceStoryPropertyWriteStatus::MissingValuePointer;
                validation_succeeded = false;
                continue;
            }

            value_pointers[index] = value;
            write.original_value = *value;
            if (*value != spec.vanilla_value && *value != spec.fast_value)
            {
                write.status = RuntimeFestivalDanceStoryPropertyWriteStatus::UnexpectedValue;
                validation_succeeded = false;
            }
        }

        // Validate every reflected field before changing any of them. This
        // prevents a partial patch if another game build or mod changes the
        // story level-script layout or its expected defaults.
        if (!validation_succeeded)
        {
            return result;
        }

        result.success = true;
        for (size_t index = 0; index < runtime_festival_dance_story_property_specs.size(); ++index)
        {
            const int32_t requested_value =
                    runtime_festival_dance_story_property_specs[index].fast_value;
            int32_t* const value = value_pointers[index];
            RuntimeFestivalDanceStoryPropertyWriteResult& write = result.writes[index];
            if (*value == requested_value)
            {
                write.status = RuntimeFestivalDanceStoryPropertyWriteStatus::AlreadySet;
                continue;
            }

            *value = requested_value;
            if (*value == requested_value)
            {
                write.status = RuntimeFestivalDanceStoryPropertyWriteStatus::Patched;
                result.changed = true;
            }
            else
            {
                write.status = RuntimeFestivalDanceStoryPropertyWriteStatus::VerificationFailed;
                result.success = false;
            }
        }
        return result;
    }

    auto try_patch_runtime_festival_dance_story_timing_object(UObject* object)
            -> RuntimeFestivalDanceStoryTimingPatchResult
    {
        RuntimeFestivalDanceStoryTimingPatchResult result{};
        for (size_t index = 0;
             index < runtime_festival_dance_story_timing_property_specs.size();
             ++index)
        {
            result.writes[index].property_name =
                    runtime_festival_dance_story_timing_property_specs[index].property_name;
            result.writes[index].requested_value =
                    runtime_festival_dance_story_timing_property_specs[index].fast_value;
        }

        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return result;
        }

        result.object_name = object->GetFullName();
        UClass* const object_class = object->GetClassPrivate();
        if (!object_class ||
            !is_runtime_festival_dance_story_manager_live_object(
                    object_class->GetName(),
                    result.object_name))
        {
            return result;
        }
        result.is_target = true;

        std::array<float*, runtime_festival_dance_story_timing_property_specs.size()>
                value_pointers{};
        bool validation_succeeded = true;
        for (size_t index = 0;
             index < runtime_festival_dance_story_timing_property_specs.size();
             ++index)
        {
            const RuntimeFestivalDanceStoryTimingPropertySpec& spec =
                    runtime_festival_dance_story_timing_property_specs[index];
            RuntimeFestivalDanceStoryTimingPropertyWriteResult& write = result.writes[index];
            FProperty* const property = object->GetPropertyByNameInChain(spec.property_name.data());
            if (!property)
            {
                write.status = RuntimeFestivalDanceStoryPropertyWriteStatus::MissingProperty;
                validation_succeeded = false;
                continue;
            }
            if (property->GetClass().GetName() != STR("FloatProperty"))
            {
                write.status = RuntimeFestivalDanceStoryPropertyWriteStatus::WrongPropertyType;
                validation_succeeded = false;
                continue;
            }
            if (property->GetElementSize() != static_cast<int32_t>(sizeof(float)))
            {
                write.status = RuntimeFestivalDanceStoryPropertyWriteStatus::WrongPropertySize;
                validation_succeeded = false;
                continue;
            }

            float* const value = property->ContainerPtrToValuePtr<float>(object);
            if (!value)
            {
                write.status = RuntimeFestivalDanceStoryPropertyWriteStatus::MissingValuePointer;
                validation_succeeded = false;
                continue;
            }

            value_pointers[index] = value;
            write.original_value = *value;
            if (*value != spec.vanilla_value && *value != spec.fast_value)
            {
                write.status = RuntimeFestivalDanceStoryPropertyWriteStatus::UnexpectedValue;
                validation_succeeded = false;
            }
        }

        // These values are read by the native festival-dance manager. Validate
        // both before writing either so an unexpected game build or another mod
        // cannot leave the story Sunwheel sequence partially retimed.
        if (!validation_succeeded)
        {
            return result;
        }

        result.success = true;
        for (size_t index = 0;
             index < runtime_festival_dance_story_timing_property_specs.size();
             ++index)
        {
            const float requested_value =
                    runtime_festival_dance_story_timing_property_specs[index].fast_value;
            float* const value = value_pointers[index];
            RuntimeFestivalDanceStoryTimingPropertyWriteResult& write = result.writes[index];
            if (*value == requested_value)
            {
                write.status = RuntimeFestivalDanceStoryPropertyWriteStatus::AlreadySet;
                continue;
            }

            *value = requested_value;
            if (*value == requested_value)
            {
                write.status = RuntimeFestivalDanceStoryPropertyWriteStatus::Patched;
                result.changed = true;
            }
            else
            {
                write.status = RuntimeFestivalDanceStoryPropertyWriteStatus::VerificationFailed;
                result.success = false;
            }
        }
        return result;
    }
}
