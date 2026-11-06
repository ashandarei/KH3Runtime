#include "kh02_nav_row_publisher.h"
#include "kh02_hud_reflection.h"
#include <Unreal/NameTypes.hpp>

namespace
{
    constexpr int native_name_bytes = 8;
}

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02hud_detail;

    void Kh02NavRowPublisher::publish(UObject* original, UObject* native)
    {
        Table src(original, L"/Script/TresGame.TresNaviMapData"), dst(native, L"/Script/TresGame.TresNaviMapData");
        const wchar_t* map_keys[]{L"dw02_01", L"dw02_02", L"dw02_03", L"dw02_04", L"dw03_01", L"dw03_02", L"dw03_03",
                                  L"dw03_04", L"dw03_05", L"dw03_06", L"dw04_01", L"dw04_02", L"dw04_03", L"dw04_04",
                                  L"dw05_01", L"dw05_02", L"dw05_03", L"dw05_04", L"dw06_01", L"dw08_01", L"dw08_02",
                                  L"dw08_03", L"dw08_04", L"dw09_01", L"dw09_02"};
        for (const auto* source_key : map_keys)
        {
            const std::wstring source = source_key, target = L"AP02_" + source;
            auto* row = dst.copy(target.c_str(), src.find(source.c_str()));
            auto* item = find_struct_field_by_name(dst.schema, L"ItemKeyName");
            if (!item || item->GetClass().GetName() != L"NameProperty" || item->GetElementSize() != native_name_bytes)
            {
                throw std::runtime_error("kh02_hud_map_item_layout");
            }
            *item->ContainerPtrToValuePtr<FName>(row) = FName{};
            // Original room volumes and gameflow select these source keys.
            // Publish the audited identities after checking for conflicts,
            // alongside each host's private initial key.
            if ((!source.starts_with(L"dw02_")) && published_mirror_nav.insert(source).second)
            {
                if (dst.find(source.c_str()))
                {
                    throw std::runtime_error("kh02_mirror_nav_key_occupied");
                }
                auto* alias = dst.copy(source.c_str(), row);
                *item->ContainerPtrToValuePtr<FName>(alias) = FName{};
            }
        }
    }

} // namespace kh3ap
