#pragma once
#include <set>
#include <string>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02AreaRowPublisher
    {
      public:
        void publish(RC::Unreal::UObject* depths_areas, RC::Unreal::UObject* native_areas);

      private:
        std::set<std::wstring> published_depths_areas;
    };
} // namespace kh3ap
