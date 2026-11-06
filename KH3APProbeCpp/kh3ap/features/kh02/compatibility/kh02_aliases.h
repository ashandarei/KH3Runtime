#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/FFrame.hpp>
#include <cstddef>
#include <vector>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // These descriptors remain rooted after map departure because live source
    // imports refer to them. This differs deliberately from temporary entry roots.
    class Kh02NativeAliases
    {
      public:
        using Thunk = void (*)(RC::Unreal::UObject*, RC::Unreal::FFrame&, void*);
        void add(const wchar_t* owner, const wchar_t* name, const wchar_t* prototype, Thunk thunk = nullptr);
        void track_descriptor(RC::Unreal::UObject* object);
        auto empty() const -> bool;
        auto size() const -> std::size_t;

      private:
        std::vector<RC::Unreal::FWeakObjectPtr> descriptors;
    };
} // namespace kh3ap
