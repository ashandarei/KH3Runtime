#pragma once
#include "kh02_compatibility_reader.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <Unreal/FFrame.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/NameTypes.hpp>

namespace kh3ap::kh02compat
{
    using namespace RC::Unreal;

    // Only valid during a guarded native thunk. Every reflected local is checked
    // against its owning function or receiver before its memory is dereferenced.
    class EngineOperands
    {
        FFrame_50_AndBelow* frame;
        uint8_t*& cursor;
        uint8_t* script_end;
        BytecodeReader bytes;

      public:
        struct Latent
        {
            int32_t linkage;
            int32_t uuid;
            FName execution;
            UObject* callback;
        };

        static_assert(sizeof(Latent) == vm::native_latent_bytes);

        EngineOperands(FFrame_50_AndBelow& frame, uint8_t*& cursor, uint8_t* limit)
            : frame(&frame), cursor(cursor), script_end(limit), bytes(cursor, limit)
        {
        }

        void end_parameters()
        {
            bytes.expect(opcode::end_parameters, "kh02_compat_end_operand");
        }

        auto name(const char* error) -> FName
        {
            struct LoadedNameLiteral
            {
                uint32_t comparison_index;
                uint32_t display_index;
                uint32_t number;
            };

            static_assert(sizeof(LoadedNameLiteral) == vm::name_literal_bytes);
            bytes.require(vm::name_operand_bytes, error);
            bytes.expect(opcode::name_constant, error);
            const auto name_literal = bytes.value<LoadedNameLiteral>(error);
            const std::array<uint32_t, 2> packed_name{name_literal.comparison_index, name_literal.number};
            FName result;
            static_assert(sizeof(result) == sizeof(packed_name));
            std::memcpy(&result, packed_name.data(), sizeof(result));
            return result;
        }

        auto latent_self() -> Latent
        {
            bytes.require(vm::latent_prefix_bytes, "kh02_compat_movie_latent_operand");
            bytes.expect(opcode::self, "kh02_compat_movie_latent_operand");
            bytes.expect(opcode::struct_constant, "kh02_compat_movie_latent_operand");
            auto* schema = bytes.value<UScriptStruct*>("kh02_compat_movie_latent_schema");
            const auto serialized_size = bytes.value<int32_t>("kh02_compat_movie_latent_schema");
            if (!is_uobject_valid_fast(schema) || schema->GetPathName() != L"/Script/Engine.LatentActionInfo" ||
                serialized_size != vm::serialized_latent_bytes)
            {
                throw std::runtime_error("kh02_compat_movie_latent_schema");
            }
            bytes.require(vm::scalar_operand_bytes, "kh02_compat_movie_latent_linkage");
            if (*cursor != opcode::integer_constant && *cursor != opcode::skip_offset)
            {
                throw std::runtime_error("kh02_compat_movie_latent_linkage");
            }
            ++cursor;
            Latent result{};
            result.linkage = bytes.value<int32_t>("kh02_compat_movie_latent_linkage");
            result.uuid = integer();
            bytes.require(vm::latent_name_and_tail_bytes, "kh02_compat_movie_latent_name");
            result.execution = name("kh02_compat_movie_latent_name");
            bytes.expect(opcode::self, "kh02_compat_movie_latent_callback");
            bytes.expect(opcode::end_struct_constant, "kh02_compat_movie_latent_callback");
            if (!is_uobject_valid_fast(frame->Object))
            {
                throw std::runtime_error("kh02_compat_movie_latent_callback");
            }
            result.callback = frame->Object;
            return result;
        }

        auto integer() -> int32_t
        {
            return bytes.integer("kh02_compat_int_operand");
        }

        auto local_integer() -> int32_t
        {
            constexpr int integer_property_bytes = sizeof(int32_t);
            if (script_end - cursor < vm::variable_with_following_token_bytes || *cursor++ != opcode::local_variable ||
                !frame->Locals)
            {
                throw std::runtime_error("kh02_compat_local_int_operand");
            }
            FProperty* local_property;
            std::memcpy(&local_property, cursor, sizeof(local_property));
            cursor += vm::reference_bytes;
            // In this verified UE4.17 build properties are UObjects;
            // UE4SS exposes them through its version-neutral FProperty.
            auto* property_object = reinterpret_cast<UObject*>(local_property);
            if (!is_uobject_valid_fast(property_object) || property_object->GetOuterPrivate() != frame->Node ||
                local_property->GetClass().GetName() != L"IntProperty" ||
                local_property->GetElementSize() != integer_property_bytes ||
                local_property->GetOffset_Internal() < 0 ||
                local_property->GetOffset_Internal() + integer_property_bytes > frame->Node->GetPropertiesSize())
            {
                throw std::runtime_error("kh02_compat_local_int_layout");
            }
            return *local_property->ContainerPtrToValuePtr<int32_t>(frame->Locals);
        }

        auto number() -> float
        {
            return bytes.number("kh02_compat_float_operand");
        }

        auto variable_number() -> float
        {
            constexpr int float_property_bytes = sizeof(float);
            if (cursor < script_end && *cursor == opcode::float_constant)
            {
                return number();
            }
            if (script_end - cursor < vm::variable_with_following_token_bytes || *cursor++ != opcode::local_variable ||
                !frame->Locals)
            {
                throw std::runtime_error("kh02_compat_local_float_operand");
            }
            FProperty* property;
            std::memcpy(&property, cursor, sizeof(property));
            cursor += vm::reference_bytes;
            if (!is_uobject_valid_fast(reinterpret_cast<UObject*>(property)) ||
                reinterpret_cast<UObject*>(property)->GetOuterPrivate() != frame->Node ||
                property->GetClass().GetName() != L"FloatProperty" ||
                property->GetElementSize() != float_property_bytes || property->GetOffset_Internal() < 0 ||
                property->GetOffset_Internal() + float_property_bytes > frame->Node->GetPropertiesSize())
            {
                throw std::runtime_error("kh02_compat_local_float_layout");
            }
            return *property->ContainerPtrToValuePtr<float>(frame->Locals);
        }

        auto literal_string() -> std::wstring
        {
            return bytes.literal_string();
        }

        auto object() -> UObject*
        {
            if (cursor >= script_end)
            {
                throw std::runtime_error("kh02_compat_object_operand");
            }
            if (*cursor == opcode::no_object)
            {
                ++cursor;
                return nullptr;
            }
            if (script_end - cursor < vm::reference_operand_bytes || *cursor++ != opcode::object_constant)
            {
                throw std::runtime_error("kh02_compat_object_operand");
            }
            UObject* value;
            std::memcpy(&value, cursor, sizeof(value));
            cursor += vm::reference_bytes;
            return value;
        }

        auto local_object() -> UObject*
        {
            constexpr int object_property_bytes = sizeof(UObject*);
            if (cursor < script_end && *cursor == opcode::no_object)
            {
                ++cursor;
                return nullptr;
            }
            if (script_end - cursor < vm::variable_with_following_token_bytes ||
                (*cursor != opcode::local_variable && *cursor != opcode::instance_variable))
            {
                throw std::runtime_error("kh02_compat_local_object_operand");
            }
            const bool instance_property = *cursor++ == opcode::instance_variable;
            FProperty* local_property;
            std::memcpy(&local_property, cursor, sizeof(local_property));
            cursor += vm::reference_bytes;
            auto* property_object = reinterpret_cast<UObject*>(local_property);
            if (!is_uobject_valid_fast(property_object) || local_property->GetClass().GetName() != L"ObjectProperty" ||
                local_property->GetElementSize() != object_property_bytes || local_property->GetOffset_Internal() < 0)
            {
                throw std::runtime_error("kh02_compat_local_object_layout");
            }
            void* container = nullptr;
            if (instance_property)
            {
                if (!is_uobject_valid_fast(frame->Object) ||
                    frame->Object->GetPropertyByNameInChain(property_object->GetName().c_str()) != local_property ||
                    local_property->GetOffset_Internal() + object_property_bytes >
                        frame->Object->GetClassPrivate()->GetPropertiesSize())
                {
                    throw std::runtime_error("kh02_compat_instance_object_layout");
                }
                container = frame->Object;
            }
            else
            {
                if (!frame->Locals || property_object->GetOuterPrivate() != frame->Node ||
                    local_property->GetOffset_Internal() + object_property_bytes > frame->Node->GetPropertiesSize())
                {
                    throw std::runtime_error("kh02_compat_local_object_layout");
                }
                container = frame->Locals;
            }
            auto* value = *local_property->ContainerPtrToValuePtr<UObject*>(container);
            if (value && !is_uobject_valid_fast(value))
            {
                throw std::runtime_error("kh02_compat_stale_target");
            }
            return value;
        }
    };
} // namespace kh3ap::kh02compat
