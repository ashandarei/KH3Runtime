#pragma once
#include <set>
#include <string>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02NavRowPublisher
    {
      public:
        void publish(RC::Unreal::UObject* original, RC::Unreal::UObject* native);

      private:
        std::set<std::wstring> published_mirror_nav;
    };
} // namespace kh3ap
