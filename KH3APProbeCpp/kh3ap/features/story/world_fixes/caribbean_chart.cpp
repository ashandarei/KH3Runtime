#include "kh3ap/features/story/world_fixes/caribbean_chart.h"

#include "kh3ap/app/config.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/core/reflection/reflected_function_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/grants/delivery/item_call_helpers.h"

#include "kh3ap/app/composition/detour.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/UObject.hpp>
#include <Windows.h>
#include <array>
#include <cstring>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        // TresUIP_SpecialShip::SetNauticalChartPoint runs when the chart opens.
        constexpr uintptr_t set_chart_points_rva = 0x1adb610;
        constexpr std::array<uint8_t, 20> set_chart_points_signature{
            0x88, 0x54, 0x24, 0x10, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54,
            0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x78};
        constexpr int32_t explore_the_seas = 2400; // CA_ISLAND_EVENT4
        constexpr GfxProbeFunctionPaths gfx{
            L"/Script/ScaleformUI.GFxObject:GetMemberValue",
            L"/Script/ScaleformUI.GFxObject:Invoke",
            L"/Script/ScaleformUI.GFxObject:SetMemberValue"};

        // Native chart projection of lv_TriggerVolume_before_shiprace_03 is
        // (935.5, 662.9). Move only Exile Island's red circle, relative to
        // its chart point at (956, 609). Travel coordinates and selection
        // remain owned by the stock chart.
        constexpr float race_marker_x = -20.5f;
        constexpr float race_marker_y = 53.9f;

        auto chart_points_address() -> uintptr_t
        {
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(GetModuleHandleW(nullptr));
            if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew >= 0x1000)
            {
                return 0;
            }
            const auto base = reinterpret_cast<uintptr_t>(dos);
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
                !native_build::egs.matches(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage))
            {
                return 0;
            }
            const auto address = base + set_chart_points_rva;
            return std::memcmp(reinterpret_cast<const void*>(address), set_chart_points_signature.data(),
                               set_chart_points_signature.size()) == 0 ? address : 0;
        }

        auto child(UObject* parent, std::wstring_view name) -> UObject*
        {
            if (!is_uobject_valid_fast(parent))
            {
                return nullptr;
            }
            return try_gfx_get_member_value_object(gfx, parent, name);
        }
    }

    struct RuntimeCaribbeanChart::Impl
    {
        using SetChartPoints = void (*)(UObject*, bool);
        inline static Impl* instance{};
        RuntimeState& runtime_state;
        std::unique_ptr<PLH::x64Detour> hook;
        uint64_t original{};

        explicit Impl(RuntimeState& runtime_state) : runtime_state(runtime_state) {}

        ~Impl()
        {
            if (hook)
            {
                hook->unHook();
                instance = nullptr;
            }
        }

        auto draw_race_marker(UObject* owner) -> void
        {
            int32_t story{};
            if (!is_uobject_valid_fast(owner) ||
                !try_call_get_game_flag(L"gameflow_CA", story, runtime_state,
                                        settings::s_get_game_flag_path,
                                        settings::s_tres_game_blueprint_library_default_object_path, false) ||
                story != explore_the_seas)
            {
                return;
            }

            auto* nautical_chart = try_read_reflected_object_property_value(owner, L"m_pGFxNauticalChart");
            auto* chart = child(nautical_chart, L"chart");
            auto* marker = child(child(chart, L"markerContainer"), L"marker6");
            auto* circle = child(marker, L"iconNext");
            if (!is_uobject_valid_fast(circle))
            {
                return;
            }
            try
            {
                ReflectedFunctionCall position(circle, L"SetPosition", "caribbean_marker_position", 2 * sizeof(float));
                position.number(L"X", race_marker_x);
                position.number(L"Y", race_marker_y);
                ReflectedFunctionCall play(circle, L"GotoAndPlay", "caribbean_marker_animation", sizeof(FString));
                play.string(L"FrameString", L"loop");
                ReflectedFunctionCall show(marker, L"SetVisible", "caribbean_marker_visibility", sizeof(bool));
                show.boolean(L"bVisible", true);
                play.run();
                position.run();
                show.run();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] Caribbean nautical chart race marker applied\n"));
#endif
            }
            catch (const std::runtime_error&)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Caribbean nautical chart marker layout rejected\n"));
            }
        }

        static auto set_points(UObject* owner, bool opening) -> void
        {
            auto& self = *instance;
            reinterpret_cast<SetChartPoints>(self.original)(owner, opening);
            self.draw_race_marker(owner);
        }
    };

    RuntimeCaribbeanChart::RuntimeCaribbeanChart(RuntimeState& runtime_state)
        : impl_(std::make_unique<Impl>(runtime_state))
    {
    }

    RuntimeCaribbeanChart::~RuntimeCaribbeanChart() = default;

    auto RuntimeCaribbeanChart::install_once() -> void
    {
        const auto address = chart_points_address();
        if (!address || Impl::instance)
        {
            return;
        }
        auto& self = *impl_;
        auto hook = std::make_unique<PLH::x64Detour>(address, reinterpret_cast<uintptr_t>(&Impl::set_points),
                                                    &self.original);
        Impl::instance = &self;
        if (hook->hook())
        {
            self.hook = std::move(hook);
        }
        else
        {
            Impl::instance = nullptr;
            RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Caribbean nautical chart hook failed\n"));
        }
    }
}
