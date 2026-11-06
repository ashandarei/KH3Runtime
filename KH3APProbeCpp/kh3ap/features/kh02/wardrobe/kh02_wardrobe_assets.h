#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <set>
#include <string>
#include <vector>

namespace kh3ap
{
    class Kh02WardrobeAssets
    {
      public:
        void release();
        void retain(RC::Unreal::UObject* object);
        auto load(RC::Unreal::UObject* context, const wchar_t* path) -> RC::Unreal::UObject*;

      private:
        std::vector<RC::Unreal::FWeakObjectPtr> roots;
        std::set<std::wstring> requested;
    };
} // namespace kh3ap
