#include "kh02_mission_counter.h"
#include "kh02_hud_reflection.h"
#include "kh3ap/core/objects/asset_load_helpers.h"

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02hud_detail;

    void Kh02MissionCounter::prepare(UObject* counter, UObject* hud)
    {
        Table counters(counter, L"/Script/TresGame.TresUIMissionCounterTable");
        Table own_hud(hud, L"/Script/TresGame.TresUIMissionCounterTable");
        auto* row = counters.copy(L"AP02_Gears", own_hud.find(L"AP02_Gears"));
        auto* maximum_field = find_struct_field_by_name(counters.schema, L"MaxValue");
        if (!maximum_field || maximum_field->GetClass().GetName() != L"IntProperty" ||
            maximum_field->GetElementSize() != sizeof(int32_t))
        {
            throw std::runtime_error("kh02_hud_counter_max");
        }
        *maximum_field->ContainerPtrToValuePtr<int>(row) = maximum;
    }

    void Kh02MissionCounter::set(int value, int limit, bool visible, bool ready)
    {
        if (!kh02_mission_counter_valid(value, limit))
        {
            throw std::runtime_error("kh02_counter_bounds");
        }
        count = value;
        maximum = limit;
        presentation.request(visible);
        if (ready)
        {
            auto* table = find_loaded_runtime_asset(L"/Game/UI/Data/MissionCounterData.MissionCounterData");
            Table rows(table, L"/Script/TresGame.TresUIMissionCounterTable");
            auto* row = rows.find(L"AP02_Gears");
            auto* field = find_struct_field_by_name(rows.schema, L"MaxValue");
            if (!row || !field || field->GetClass().GetName() != L"IntProperty" ||
                field->GetElementSize() != sizeof(int32_t))
            {
                throw std::runtime_error("kh02_hud_counter_row_lost");
            }
            *field->ContainerPtrToValuePtr<int>(row) = limit;
        }
        if (!visible && presentation.visible)
        {
            Call close(library, L"TresUI_CloseMissionCounter");
            close.run();
            presentation.suspend();
        }
    }

    void Kh02MissionCounter::tick()
    {
        if (presentation.dirty)
        {
            if (!presentation.visible)
            {
                Call open(library, L"TresUI_OpenMissionCounter");
                open.integer(L"InitializeCount", count);
                open.name(L"DataTableKeyName", L"AP02_Gears");
                open.boolean(L"IsOverMaxCount", false);
                open.run();
            }
            else
            {
                Call set(library, L"TresUI_SetMissionCounter");
                set.integer(L"count", count);
                set.run();
            }
            rendered = count;
            presentation.presented();
            ++counter_updates;
        }
    }

    void Kh02MissionCounter::close()
    {
        if (presentation.visible)
        {
            Call close(library, L"TresUI_CloseMissionCounter");
            close.run();
        }
        invalidate_presentation();
    }

    void Kh02MissionCounter::invalidate_presentation()
    {
        // A character handoff can replace/close the native UI without a new
        // source count event. Retain the source's visibility request and count.
        presentation.suspend();
        rendered = -1;
    }

    void Kh02MissionCounter::reset_world()
    {
        presentation = {};
        rendered = -1;
    }

} // namespace kh3ap
