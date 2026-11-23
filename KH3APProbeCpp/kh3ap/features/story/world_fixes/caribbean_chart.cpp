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
#include <Unreal/UFunction.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/FProperty.hpp>
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
        constexpr uintptr_t egs_chart_points_rva = 0x1adb610;
        constexpr uintptr_t steam_chart_points_rva = 0x175b270;
        constexpr std::array<uint8_t, 20> egs_chart_points_signature{
            0x88, 0x54, 0x24, 0x10, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54,
            0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x78};
        constexpr std::array<uint8_t, 28> steam_chart_points_signature{
            0x88, 0x54, 0x24, 0x10, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
            0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24, 0xe1, 0x48, 0x81, 0xec, 0x98, 0x00, 0x00, 0x00};
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
            if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
            {
                return 0;
            }
            const auto validated_address = [base](uintptr_t rva, const auto& signature)
            {
                const auto address = base + rva;
                return std::memcmp(reinterpret_cast<const void*>(address), signature.data(), signature.size()) == 0
                           ? address : 0;
            };
            switch (native_build::identify(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage))
            {
            case native_build::Build::Egs:
                return validated_address(egs_chart_points_rva, egs_chart_points_signature);
            case native_build::Build::Steam:
                return validated_address(steam_chart_points_rva, steam_chart_points_signature);
            default:
                return 0;
            }
        }

        constexpr wchar_t fleet_overlap[] =
            L"BndEvt__lv_TriggerVolume_FrontFinalDestination_K2Node_ActorBoundEvent_459_OnTresActorBeginOverlap__DelegateSignature";

        // The stock fleet volume includes the Island09 landing. Use the island's
        // outer island visit volume (Island09_Desert_Out) to keep its anchorage clear.
        auto at_forsaken_isle(UObject* other) -> bool
        {
            if (!is_uobject_valid_fast(other)) return false;
            try
            {
                ReflectedFunctionCall location(other, L"K2_GetActorLocation", "forsaken_fleet", 12);
                location.run();
                const auto position = location.result_vector();
                constexpr float island_x = -3273.491455078125f;
                constexpr float island_y = -284075.78125f;
                constexpr float visit_radius = 25000.f;
                const float dx = position[0] - island_x;
                const float dy = position[1] - island_y;
                return dx * dx + dy * dy < visit_radius * visit_radius;
            }
            catch (const std::runtime_error&)
            {
                return false;
            }
        }

        void route_fleet_overlap(UObject* level_script, UFunction* event, void* parameters)
        {
            static const FName name{fleet_overlap, FNAME_Add};
            if (!event || event->GetNamePrivate() != name || !parameters || !is_uobject_valid_fast(level_script))
            {
                return;
            }
            auto* graph = level_script->GetFunctionByNameInChain(L"ExecuteUbergraph_ca_02_gameflow");
            if (!is_uobject_valid_fast(graph) ||
                graph->GetPathName() != L"/Game/Levels/ca/ca_02/umap/ca_02_gameflow.ca_02_gameflow_C:ExecuteUbergraph_ca_02_gameflow" ||
                event->GetNumParms() != 1 || event->GetParmsSize() != sizeof(UObject*))
            {
                return;
            }
            auto* other_param = find_function_param_by_name(event, L"OtherActor");
            if (!other_param || other_param->GetClass().GetName() != L"ObjectProperty" ||
                other_param->GetOffset_Internal() != 0 || other_param->GetElementSize() != sizeof(UObject*))
            {
                return;
            }

            // This bound event's private entry is a single EX_Jump to the
            // stock story gates. Select its target on every overlap, including
            // subsequent entries in the same world after sailing back from sea.
            constexpr int32_t overlap_entry = 633777;
            constexpr int32_t stock_story_gates = 571783;
            constexpr int32_t graph_return = 635881;
            auto& script = graph->GetScript();
            if (script.Num() != graph_return + 3 || script[overlap_entry] != 0x06 ||
                script[graph_return] != 0x04 || script[graph_return + 1] != 0x0b || script[graph_return + 2] != 0x53)
            {
                return;
            }
            auto* target_operand = script.GetData() + overlap_entry + 1;
            int32_t current{};
            std::memcpy(&current, target_operand, sizeof(current));
            if (current != stock_story_gates && current != graph_return)
            {
                return;
            }

            auto* other = *other_param->ContainerPtrToValuePtr<UObject*>(parameters);
            const bool anchored = at_forsaken_isle(other);
            const int32_t target = anchored ? graph_return : stock_story_gates;
            if (target != current)
            {
                std::memcpy(target_operand, &target, sizeof(target));
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] Forsaken Isle fleet overlap anchored={}\n"), anchored);
#endif
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
        Hook::RegisterProcessEventPreCallback(&route_fleet_overlap);
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
