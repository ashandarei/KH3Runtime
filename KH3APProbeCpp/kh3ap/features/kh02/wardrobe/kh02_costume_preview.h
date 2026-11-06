#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <memory>
#include <string>
#include <vector>

namespace kh3ap
{
    class Kh02CostumePreview
    {
      public:
        Kh02CostumePreview();
        ~Kh02CostumePreview();
        void update(RC::Unreal::UObject* pawn, const std::vector<RC::Unreal::FWeakObjectPtr>& accessories, float angle);
        void close(bool destroy_actor = true);
        auto snapshot() const -> std::string;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
