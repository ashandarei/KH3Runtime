#include "kh02_area_row_publisher.h"
#include "kh02_hud_reflection.h"

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02hud_detail;

    void Kh02AreaRowPublisher::publish(UObject* depths_areas, UObject* native_areas)
    {
        // The authored NavMapVolumes select these area-table keys.
        // Copy their source rows so ordinary overlaps can switch between
        // their authored map sections with their own scales and origins.
        Table source_area_rows(depths_areas, L"/Script/TresGame.TresAreaNameDataTable");
        Table native_area_rows(native_areas, L"/Script/TresGame.TresAreaNameDataTable");
        for (const auto* area_key : {L"dw_08_An_01", L"dw_08_An_02", L"dw_08_An_03", L"dw_08_An_04",
                                    L"dw_09_An_01", L"dw_09_An_02"})
        {
            if (published_depths_areas.contains(area_key))
            {
                continue;
            }
            if (native_area_rows.find(area_key))
            {
                throw std::runtime_error("kh02_depths_area_key_occupied");
            }
            native_area_rows.copy(area_key, source_area_rows.find(area_key));
            published_depths_areas.insert(area_key);
        }
    }

} // namespace kh3ap
