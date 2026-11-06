#pragma once
#include "kh02_call_parameters.h"
#include <array>
#include <cstddef>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

namespace RC::Unreal
{
    class UObject;
    class UFunction;
    class FProperty;
} // namespace RC::Unreal

namespace kh3ap
{
    auto kh02_require_loaded_object(const wchar_t* path) -> RC::Unreal::UObject*;

    auto kh02_native_material_slot(RC::Unreal::UObject* mesh, int index) -> RC::Unreal::UObject**;

    // One synchronous, game-thread call. The owner and reflected properties are
    // used only during this stack lifetime; callers retain weak pointers between
    // frames. Borrowed FString parameters keep their backing strings here.
    class Kh02ReflectedCall
    {
      public:
        Kh02ReflectedCall(RC::Unreal::UObject* owner, const wchar_t* method, const char* context,
                          size_t max_parameter_bytes);
        Kh02ReflectedCall(const wchar_t* owner_path, const wchar_t* method, const char* context,
                          size_t max_parameter_bytes);
        Kh02ReflectedCall(const Kh02ReflectedCall&) = delete;
        auto operator=(const Kh02ReflectedCall&) -> Kh02ReflectedCall& = delete;

        auto param(const wchar_t* name) -> RC::Unreal::FProperty*;
        void object(const wchar_t* name, RC::Unreal::UObject* value);
        void boolean(const wchar_t* name, bool value);
        void integer(const wchar_t* name, int value);
        void name(const wchar_t* name, const wchar_t* value);
        void number(const wchar_t* name, float value);
        void string(const wchar_t* name, const std::wstring& value);
        void numbers(const wchar_t* name, std::initializer_list<float> values);
        void vector(const wchar_t* name, float x, float y, float z);
        void vector2(const wchar_t* name, float x, float y);
        void spawn_transform(float x = 0, float y = 0, float z = 0);
        void run();
        auto result() -> RC::Unreal::UObject*;
        auto boolean_result(const wchar_t* name = L"ReturnValue") -> bool;
        auto result_vector() -> std::array<float, 3>;

        RC::Unreal::UObject* owner;
        RC::Unreal::UFunction* function;
        Kh02CallParameters parameters;

      private:
        [[noreturn]] void fail(const char* operation, const wchar_t* parameter = L"") const;
        std::string context_;
        std::map<std::wstring, std::wstring> strings_;
    };
} // namespace kh3ap
