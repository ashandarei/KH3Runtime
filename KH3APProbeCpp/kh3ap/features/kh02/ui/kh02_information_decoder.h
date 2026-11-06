#pragma once
#include <Unreal/FFrame.hpp>
#include <optional>
#include <string>

namespace RC::Unreal
{
    class UObject;
    class UScriptStruct;
} // namespace RC::Unreal

namespace kh3ap
{
    // Borrowed UObject/property/value pointers are used only by the immediate enqueue.
    struct Kh02InformationMessage
    {
        RC::Unreal::UObject* world;
        RC::Unreal::UScriptStruct* schema;
        void* text;
        std::wstring caller;
        bool auto_close;
        uint8_t* next_code;
    };

    auto kh02_decode_information(RC::Unreal::FFrame& stack) -> std::optional<Kh02InformationMessage>;
} // namespace kh3ap
