#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

namespace RC::Unreal
{
    class UObject;
    class UScriptStruct;
} // namespace RC::Unreal

namespace kh3ap
{
    class Kh02MapSession;

    // Owns copied TresLocText values until display or retirement. Borrowed
    // objects are used synchronously; world/schema identities remain weak.
    class Kh02InformationQueue
    {
      public:
        Kh02InformationQueue();
        ~Kh02InformationQueue();
        auto full() const -> bool;
        auto pending() const -> size_t;
        auto events() const -> const nlohmann::json&;
        void enqueue(RC::Unreal::UObject* world, uint64_t generation, RC::Unreal::UScriptStruct* schema,
                     const void* text, std::wstring caller, bool auto_close);
        void tick(RC::Unreal::UObject* world, const Kh02MapSession& session, bool can_show);
        void clear();

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
