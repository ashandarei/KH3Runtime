#include <utility>
#include <optional>
#include <memory>
#include <chrono>
#include <array>
#include "kh3ap/bridge/transport/request_file_watcher.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <limits>
#include "kh3ap/features/world/portals/portal_service.h"
#include "kh3ap/features/world/portals/portal_policy.h"
#include "kh3ap/features/world/portals/final_door_policy.h"
#include "kh3ap/features/story/progression/game_flag_events.h"
#include "kh3ap/features/world/space/space_access.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include <Windows.h>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/World.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <set>
#include <sstream>
#include <cstring>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr auto portrait_mark_vertical_offset = .10f;
        constexpr auto portrait_mark_offset_intensity = .003f;
        constexpr auto portrait_backdrop_black_switch = .97f;
        constexpr auto portrait_mark_bump_offset = -.6f;
        constexpr std::array portrait_haze_color{.55f, .55f, .55f, 1.f};
        constexpr auto portrait_material_test_count = 8;
        constexpr auto portrait_material_source_test_start = 4;
        constexpr auto portrait_material_switch_cycle = 4;
        constexpr auto portrait_material_switch_on_count = 2;
        constexpr auto portrait_material_opacity_cycle = 2;
        constexpr auto twilight_comparison_parameter_bytes = 17;
        constexpr auto twilight_comparison_label_offset = 8;

        constexpr auto portrait_cycle_seconds = 8.f;
        constexpr auto portrait_face_seconds = 4.f;
        constexpr auto portrait_fade_seconds = .35f;
        constexpr auto portrait_emissive_power = 6.f;
        constexpr auto portal_parameter_tolerance = .0001f;
        constexpr auto portal_portrait_material_slot = 2;
        constexpr auto portal_asset_load_uuid_base = 0x504f5200;
        constexpr int portrait_canvas_pixels = 512;
        constexpr float portrait_canvas_extent = static_cast<float>(portrait_canvas_pixels);
        constexpr float portrait_width_fraction = .48f;
        constexpr float portrait_height_fraction = .375f;
        constexpr float portrait_tint_channel = .38f;

        constexpr auto portal_animation_interval = std::chrono::milliseconds(33);
        constexpr auto max_portal_parameter_bytes = 4096;
        struct Call
        {
            UObject* object;
            UFunction* function;
            std::vector<std::byte> data;
            Call(UObject* o, const wchar_t* name) : object(o)
            {
                if (!is_uobject_valid_fast(o))
                {
                    throw std::runtime_error("portal_object");
                }
                function = o->GetFunctionByNameInChain(name);
                if (!is_uobject_valid_fast(function) ||
                    function->GetParmsSize() > max_portal_parameter_bytes)
                {
                    throw std::runtime_error("portal_function");
                }
                data.resize(std::max<int>(1, function->GetParmsSize()));
            }
            FProperty* field(const wchar_t* name)
            {
                auto* p = find_function_param_by_name(function, name);
                if (!p || p->GetOffset_Internal() < 0 ||
                    p->GetOffset_Internal() + p->GetElementSize() > function->GetParmsSize())
                {
                    throw std::runtime_error("portal_parameter");
                }
                return p;
            }
            void index()
            {
                auto* p = field(L"ElementIndex");
                if (p->GetElementSize() != sizeof(float))
                {
                    throw std::runtime_error("portal_index");
                }
                *p->ContainerPtrToValuePtr<int>(data.data()) = portal_portrait_material_slot;
            }
            void run()
            {
                object->ProcessEvent(function, data.data());
            }
            void object_arg(const wchar_t* name, UObject* value)
            {
                if (!write_object_function_param(function, data.data(), name, value))
                {
                    throw std::runtime_error("portal_object_parameter");
                }
            }
            void integer(const wchar_t* name, int value)
            {
                auto* p = field(name);
                if (p->GetClass().GetName() != L"IntProperty" ||
                    p->GetElementSize() != sizeof(int32_t))
                {
                    throw std::runtime_error("portal_integer_parameter");
                }
                *p->ContainerPtrToValuePtr<int>(data.data()) = value;
            }
            template <size_t N> void floats(const wchar_t* name, const std::array<float, N>& values)
            {
                auto* p = field(name);
                if (!p->IsA<FStructProperty>() || p->GetElementSize() != sizeof(values))
                {
                    throw std::runtime_error("portal_float_struct");
                }
                std::memcpy(p->ContainerPtrToValuePtr<void>(data.data()), values.data(),
                            sizeof(values));
            }
        };
        UObject* object_property(UObject* o, const wchar_t* name)
        {
            auto* p = o->GetPropertyByNameInChain(name);
            if (!p || p->GetClass().GetName() != L"ObjectProperty" ||
                p->GetElementSize() != sizeof(UObject*))
            {
                return nullptr;
            }
            auto* value = *p->ContainerPtrToValuePtr<UObject*>(o);
            return is_uobject_valid_fast(value) ? value : nullptr;
        }
        std::wstring portrait_path(std::wstring_view name)
        {
            return L"/Game/UI/Icon/MenuFace/" + std::wstring{name} + L"." + std::wstring{name};
        }
        UObject* library(const wchar_t* path)
        {
            auto* o = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, path);
            if (!is_uobject_valid_fast(o))
            {
                throw std::runtime_error("portal_library");
            }
            return o;
        }
        void scalar(UObject* material, const wchar_t* name, float value)
        {
            Call c(material, L"SetScalarParameterValue");
            if (!write_name_or_string_function_param(c.field(L"ParameterName"), c.data.data(),
                                                     name) ||
                !write_float_function_param(c.field(L"Value"), c.data.data(), value))
            {
                throw std::runtime_error("portal_scalar");
            }
            c.run();
        }
        void color(UObject* material, const wchar_t* name, const std::array<float, 4>& value)
        {
            Call c(material, L"SetVectorParameterValue");
            if (!write_name_or_string_function_param(c.field(L"ParameterName"), c.data.data(),
                                                     name))
            {
                throw std::runtime_error("portal_color");
            }
            c.floats<4>(L"Value", value);
            c.run();
            Call read(material, L"K2_GetVectorParameterValue");
            if (!write_name_or_string_function_param(read.field(L"ParameterName"), read.data.data(),
                                                     name))
            {
                throw std::runtime_error("portal_color_read");
            }
            read.run();
            auto* returned = read.field(L"ReturnValue");
            if (!returned->IsA<FStructProperty>() || returned->GetElementSize() != sizeof(value))
            {
                throw std::runtime_error("portal_color_layout");
            }
            std::array<float, 4> actual{};
            std::memcpy(actual.data(), returned->ContainerPtrToValuePtr<void>(read.data.data()),
                        sizeof(actual));
            for (size_t i = 0; i < value.size(); ++i)
            {
                if (std::abs(actual[i] - value[i]) > portal_parameter_tolerance)
                {
                    throw std::runtime_error("portal_color_readback");
                }
            }
        }
        float read_scalar(UObject* material, const wchar_t* name)
        {
            Call c(material, L"K2_GetScalarParameterValue");
            if (!write_name_or_string_function_param(c.field(L"ParameterName"), c.data.data(),
                                                     name))
            {
                throw std::runtime_error("portal_scalar_read");
            }
            c.run();
            auto* p = c.field(L"ReturnValue");
            if (p->GetElementSize() != sizeof(float) || p->GetClass().GetName() != L"FloatProperty")
            {
                throw std::runtime_error("portal_scalar_read_layout");
            }
            return *p->ContainerPtrToValuePtr<float>(c.data.data());
        }
        void texture(UObject* material, const wchar_t* name, UObject* value)
        {
            Call c(material, L"SetTextureParameterValue");
            if (!write_name_or_string_function_param(c.field(L"ParameterName"), c.data.data(),
                                                     name))
            {
                throw std::runtime_error("portal_texture");
            }
            c.object_arg(L"Value", value);
            c.run();
            Call read(material, L"K2_GetTextureParameterValue");
            if (!write_name_or_string_function_param(read.field(L"ParameterName"), read.data.data(),
                                                     name))
            {
                throw std::runtime_error("portal_texture_read");
            }
            read.run();
            if (try_read_function_object_param(read.function, read.data.data(), L"ReturnValue") !=
                value)
            {
                throw std::runtime_error("portal_texture_readback");
            }
        }
        float game_time(UObject* context)
        {
            Call c(library(L"/Script/Engine.Default__GameplayStatics"), L"GetTimeSeconds");
            c.object_arg(L"WorldContextObject", context);
            c.run();
            auto* p = c.field(L"ReturnValue");
            if (p->GetClass().GetName() != L"FloatProperty" || p->GetElementSize() != sizeof(float))
            {
                throw std::runtime_error("portal_time");
            }
            return *p->ContainerPtrToValuePtr<float>(c.data.data());
        }
    } // namespace
    struct RuntimePortalService::Impl
    {
        RuntimeSpaceAccess access;
        FWeakObjectPtr final_actor;
        VBonusFlagEvents final_flags{true};
        bool final_subscribed{}, final_pending{true};
        std::shared_ptr<const reconciliation::View> final_view;
        std::optional<bool> final_enabled;
        void bind_final_door(UObject* context)
        {
            if (!final_subscribed)
            {
                std::set<uint64_t> watched;
                for (const auto& flag : final_door::worlds)
                {
                    FName name(std::wstring{flag.name}, FNAME_Add);
                    uint64_t raw{};
                    static_assert(sizeof(name) == sizeof(raw));
                    std::memcpy(&raw, &name, sizeof(raw));
                    watched.insert(raw);
                }
                FName twilight_clear(L"gameflow_WM_WORLD_TT", FNAME_Add);
                uint64_t twilight_raw{};
                std::memcpy(&twilight_raw, &twilight_clear, sizeof(twilight_raw));
                watched.insert(twilight_raw);
                final_flags.configure(std::move(watched));
                subscribe_game_flags(final_flags);
                subscribe_game_flags(final_flags, true);
                final_subscribed = true;
            }
            auto* klass = find_loaded_runtime_asset_class(
                L"/Game/Maps/GardenOfAssemblage/uasset/Actors/g_rg_symbol_01/Portal01.Portal01_C");
            if (!klass)
            {
                return;
            }
            TArray<AActor*> actors;
            UGameplayStatics::GetAllActorsOfClass(context, klass, actors);
            if (actors.Num() != 1 || !is_uobject_valid_fast(actors[0]))
            {
                return;
            }
            final_actor = make_uobject_weak(actors[0]);
            final_enabled.reset();
            final_pending = true;
        }
        void update_final_door()
        {
            const auto view = reconciliation::view();
            if (view != final_view)
            {
                final_view = view;
                final_pending = true;
            }
            if (final_flags.pending())
            {
                final_flags.take();
                final_pending = true;
            }
            if (!final_pending || travel_pending || !garden_active)
            {
                return;
            }
            auto* actor = final_actor.Get();
            auto* context = observed_pawn.Get();
            if (!is_uobject_valid_fast(actor) || !is_uobject_valid_fast(context))
            {
                return;
            }
            auto* gate = actor->GetPropertyByNameInChain(L"APFinalDoorEnabled");
            auto* reactor = object_property(actor, L"TresReactor");
            if (!gate || !gate->IsA<FBoolProperty>() || !reactor)
            {
                throw std::runtime_error("final_door_asset_contract");
            }
            auto* owner = library(L"/Script/TresGame.Default__TresGameBlueprintLibrary");
            int completed = 0;
            for (const auto& flag : final_door::worlds)
            {
                Call query(owner, flag.boolean ? L"GetGameFlagBoolean" : L"GetGameFlag");
                if (!write_name_or_string_function_param(
                        query.field(L"FlagName"), query.data.data(), std::wstring{flag.name}))
                {
                    throw std::runtime_error("final_door_flag_name");
                }
                query.run();
                auto* result = query.field(L"ReturnValue");
                if (flag.boolean)
                {
                    if (!result->IsA<FBoolProperty>())
                    {
                        throw std::runtime_error("final_door_boolean_flag");
                    }
                    completed += static_cast<FBoolProperty*>(result)->GetPropertyValueInContainer(
                        query.data.data());
                }
                else
                {
                    if (result->GetClass().GetName() != L"IntProperty" ||
                        result->GetElementSize() != sizeof(int32_t))
                    {
                        throw std::runtime_error("final_door_integer_flag");
                    }
                    bool cleared =
                        *result->ContainerPtrToValuePtr<int>(query.data.data()) >= flag.threshold;
                    if (cleared && flag.name == L"gameflow_TT")
                    {
                        Call twilight(owner, L"GreaterEqual_FlagLabel");
                        FName flag_name(L"gameflow_WM_WORLD_TT", FNAME_Add);
                        FName label(L"WM_WORLD_TT_CLEARED", FNAME_Add);
                        if (twilight.function->GetParmsSize() !=
                            twilight_comparison_parameter_bytes)
                        {
                            throw std::runtime_error("final_door_twilight_signature");
                        }
                        std::memcpy(twilight.data.data(), &flag_name, sizeof(flag_name));
                        std::memcpy(twilight.data.data() + twilight_comparison_label_offset, &label,
                                    sizeof(label));
                        twilight.run();
                        auto* out = twilight.field(L"ReturnValue");
                        if (!out->IsA<FBoolProperty>())
                        {
                            throw std::runtime_error("final_door_twilight_result");
                        }
                        cleared = static_cast<FBoolProperty*>(out)->GetPropertyValueInContainer(
                            twilight.data.data());
                    }
                    completed += cleared;
                }
            }
            Call stock(owner, L"BP_TresGetBackyardItemNum");
            stock.object_arg(L"WorldContextObject", context);
            if (!write_name_or_string_function_param(stock.field(L"InItemKeyName"),
                                                     stock.data.data(), L"KEY_ITEM17"))
            {
                throw std::runtime_error("final_door_heart_token");
            }
            stock.run();
            auto* count = stock.field(L"ReturnValue");
            if (count->GetClass().GetName() != L"IntProperty" ||
                count->GetElementSize() != sizeof(int32_t))
            {
                throw std::runtime_error("final_door_heart_count");
            }
            const int hearts = *count->ContainerPtrToValuePtr<int>(stock.data.data());
            const bool ready = access.access_ready && view->ready && view->seed == access.seed &&
                               view->slot == access.ap_slot;
            const bool enabled = final_door::enabled(ready, hearts, access.heart_piece_count,
                                                     completed, access.worlds_required);
            static_cast<FBoolProperty*>(gate)->SetPropertyValueInContainer(actor, enabled);
            if (!final_enabled || *final_enabled != enabled)
            {
                Call change(actor, L"IsEnabled");
                change.run();
                final_enabled = enabled;
            }
            Call active(reactor, L"BP_IsGimmickComponentActive");
            active.run();
            auto* actual = active.field(L"ReturnValue");
            if (!actual->IsA<FBoolProperty>() ||
                static_cast<FBoolProperty*>(actual)->GetPropertyValueInContainer(
                    active.data.data()) != enabled)
            {
                throw std::runtime_error("final_door_reactor_readback");
            }
            final_pending = false;
            std::ostringstream state;
            state << "{\"enabled\":" << (enabled ? "true" : "false")
                  << ",\"heart_pieces\":" << hearts
                  << ",\"heart_pieces_required\":" << access.heart_piece_count
                  << ",\"completed_worlds\":" << completed
                  << ",\"worlds_required\":" << access.worlds_required << "}";
            if (!access.bridge.empty())
            {
                write_text_file_atomic_verified(access.bridge / L"runtime_final_door.json",
                                                state.str());
            }
        }

        std::function<UObject*()> pawn;
        inline static Impl* instance{};
        FWeakObjectPtr observed_pawn, observed_world, engine, departing_world;
        bool travel_pending{}, garden_active{}, context_refresh_pending{true};
        struct Entry
        {
            FWeakObjectPtr actor, original, material;
            bool rooted{}, material_rooted{};
            int id{};
            bool shown{};
            int current_face{-1};
            std::optional<bool> stock_enabled;
            bool timer_stopped{};
            float mark_power{-1.f};
        };
        std::vector<Entry> entries;
        // Render a portrait once, then let the shipped portal shader apply its
        // depth, moving smoke and distortion. No character image enters the pak.
        struct Canvas
        {
            FWeakObjectPtr target;
            bool ready{};
            uint64_t created_step{};
        };
        std::map<std::wstring, Canvas> canvases;
        std::set<std::wstring> requested;
        struct Portrait
        {
            FWeakObjectPtr source;
            bool rooted{};
            uint64_t ready_step{};
        };
        std::map<std::wstring, Portrait> portraits;
        uint64_t step{};
        uint64_t consumed_input_generation{};
        bool access_refresh_pending{true}, presentation_pending{true};
        std::chrono::steady_clock::time_point next_animation{};
        bool warned{};
        gate_opener::State gates;
        std::wstring gate_seed;
        int64_t gate_epoch{-1};
        std::tuple<std::wstring, int64_t, int64_t> gate_inventory_signature;
        bool gate_inventory_synced{};
        std::shared_ptr<const reconciliation::View> gate_inventory_view;
        FWeakObjectPtr gate_inventory_pawn;
        void sync_gate_inventory(const reconciliation::View& view)
        {
            if (!view.ready || view.seed != access.seed || view.slot != access.ap_slot)
            {
                return;
            }
            const auto count = static_cast<int64_t>(view.gates.available(view.gate_receipts));
            const auto signature = std::make_tuple(view.seed, view.epoch, count);
            if (gate_inventory_synced && signature == gate_inventory_signature)
            {
                return;
            }
            auto* sora = pawn();
            const auto current_view = reconciliation::view();
            // Delivery/load publications and possession changes wake unfinished
            // synchronization. Do not repeatedly query native inventory while idle.
            if (gate_inventory_view == current_view && gate_inventory_pawn.Get() == sora)
            {
                return;
            }
            gate_inventory_view = current_view;
            gate_inventory_pawn = make_uobject_weak(sora);
            if (view.delivery)
            {
                for (const auto& grant : view.delivery->grants)
                {
                    if (grant.runtime_token == L"KEY_ITEM20")
                    {
                        return;
                    }
                }
            }
            if (!is_uobject_valid_fast(sora))
            {
                return;
            }
            auto* owner = library(L"/Script/TresGame.Default__TresGameBlueprintLibrary");
            const auto stock = [&]()
            {
                Call query(owner, L"BP_TresGetBackyardItemNum");
                query.object_arg(L"WorldContextObject", sora);
                if (!write_name_or_string_function_param(query.field(L"InItemKeyName"),
                                                         query.data.data(), L"KEY_ITEM20"))
                {
                    throw std::runtime_error("gate_inventory_token");
                }
                auto* result = query.field(L"ReturnValue");
                if (result->GetClass().GetName() != L"IntProperty" ||
                    result->GetElementSize() != sizeof(int32_t))
                {
                    throw std::runtime_error("gate_inventory_layout");
                }
                query.run();
                return *result->ContainerPtrToValuePtr<int32_t>(query.data.data());
            };
            const int actual = stock();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            RC::Output::send<RC::LogLevel::Normal>(
                L"[KH3APProbeCpp] Gate Opener inventory native={} unspent={}\n", actual, count);
#endif
            if (actual < count || actual > std::numeric_limits<uint8_t>::max())
            {
                return;
            }
            for (int remaining = actual; remaining > count; --remaining)
            {
                Call drop(owner, L"BP_TresDropBackyardItemOne");
                drop.object_arg(L"WorldContextObject", sora);
                if (!write_name_or_string_function_param(drop.field(L"InItemKeyName"),
                                                         drop.data.data(), L"KEY_ITEM20"))
                {
                    throw std::runtime_error("gate_inventory_drop_token");
                }
                drop.run();
            }
            if (stock() != count)
            {
                throw std::runtime_error("gate_inventory_readback");
            }
            gate_inventory_signature = signature;
            gate_inventory_synced = true;
        }
        std::string published_status;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        RequestFileWatcher diagnostic_requests{
            L"portal_visual_tuning.json", L"portal_visual_probe.request",
            L"portal_refresh_canvases.request", L"portal_capture_canvases.request"};
        uint64_t access_refreshes{}, presentation_passes{}, stopped_timers{}, context_validations{};
        std::string applied_visual_tuning;
        std::optional<std::chrono::steady_clock::time_point> loading_closed_at;
        std::optional<int64_t> first_present_delay_ms;
        bool prepared_while_loading{};
#endif
        Impl(RuntimeSpaceAccess::BridgeResolver b, std::function<UObject*()> p)
            : access(std::move(b)), pawn(std::move(p))
        {
            instance = this;
        }
        ~Impl()
        {
            unsubscribe_game_flags(final_flags);
            clear();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#endif
            if (instance == this)
            {
                instance = nullptr;
            }
        }
        void clear(bool restore_materials = true)
        {
            final_actor = FWeakObjectPtr{};
            final_enabled.reset();
            final_pending = true;
            for (auto& e : entries)
            {
                auto* actor = e.actor.Get();
                auto* original = e.original.Get();
                if (restore_materials && e.shown && is_uobject_valid_fast(actor) &&
                    is_uobject_valid_fast(original))
                {
                    if (auto* mesh = object_property(actor, L"MyMesh"))
                    {
                        try
                        {
                            set_material(mesh, original);
                        }
                        catch (const std::exception&)
                        {
                        }
                    }
                }
                if (e.rooted && is_uobject_valid_fast(original))
                {
                    original->ClearRootSet();
                }
                if (auto* material = e.material.Get();
                    e.material_rooted && is_uobject_valid_fast(material))
                {
                    material->ClearRootSet();
                }
            }
            for (auto& [name, canvas] : canvases)
            {
                if (auto* object = canvas.target.Get(); is_uobject_valid_fast(object))
                {
                    object->ClearRootSet();
                }
            }
            for (auto& [path, portrait] : portraits)
            {
                if (auto* object = portrait.source.Get();
                    portrait.rooted && is_uobject_valid_fast(object))
                {
                    object->ClearRootSet();
                }
            }
            const bool had_resources = !entries.empty() || !canvases.empty() || !portraits.empty();
            canvases.clear();
            entries.clear();
            requested.clear();
            portraits.clear();
            presentation_pending = true;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            loading_closed_at.reset();
            first_present_delay_ms.reset();
            prepared_while_loading = false;
#endif
            if (had_resources)
            {
                published_status.clear();
                if (!access.bridge.empty())
                {
                    write_text_file_atomic_verified(
                        access.bridge / L"runtime_portal_state.json",
                        "{\"phase\":\"travel\",\"render_targets\":0,\"portals\":[]}\n");
                }
            }
        }
        void on_map_jump()
        {
            if (!travel_pending)
            {
                departing_world = make_uobject_weak(current_world());
            }
            travel_pending = true;
            if (!entries.empty() || !canvases.empty() || !portraits.empty())
            {
                RC::Output::send<RC::LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Party portals releasing before map jump actors={} "
                        "canvases={} sources={}\n"),
                    entries.size(), canvases.size(), portraits.size());
                // These rooted MIDs/targets have a map actor as their Outer. A
                // weak handle alone cannot stop a root from retaining that world.
                // Do not call SetMaterial on actors during world teardown.
                clear(false);
            }
            observed_pawn = FWeakObjectPtr{};
            observed_world = FWeakObjectPtr{};
            garden_active = false;
            context_refresh_pending = false;
            access_refresh_pending = true;
            next_animation = {};
        }
        UObject* current_world()
        {
            auto* owner = engine.Get();
            if (!is_uobject_valid_fast(owner))
            {
                owner = UObjectGlobals::StaticFindObject<UObject*>(
                    nullptr, nullptr, L"/Engine/Transient.TresGameEngine_0");
                if (!is_uobject_valid_fast(owner))
                {
                    return nullptr;
                }
                engine = make_uobject_weak(owner);
            }
            auto* viewport = object_property(owner, L"GameViewport");
            return viewport ? object_property(viewport, L"World") : nullptr;
        }
        bool loading_closed()
        {
            Call c(library(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                   L"TresUI_IsClosedLoadingScreen");
            auto* result = c.field(L"ReturnValue");
            if (!result->IsA<FBoolProperty>() || c.function->GetParmsSize() != 1)
            {
                throw std::runtime_error("portal_loading_layout");
            }
            c.run();
            return static_cast<FBoolProperty*>(result)->GetPropertyValueInContainer(c.data.data());
        }
        static int id(UObject* o)
        {
            if (!is_uobject_valid_fast(o))
            {
                return -1;
            }
            auto* p = o->GetPropertyByNameInChain(L"PortalID");
            return p && p->GetClass().GetName() == L"IntProperty" &&
                           p->GetElementSize() == sizeof(int32_t)
                       ? *p->ContainerPtrToValuePtr<int>(o)
                       : -1;
        }
        bool party_portrait(int portal) const
        {
            return access.party_portals &&
                   !(portal == portals::dark_world_portal_id && access.kh02_enabled);
        }
        bool owned(int portal) const
        {
            if (portal == portals::dark_world_portal_id && access.kh02_enabled)
            {
                return access.access_ready && access.items.contains(L"Oblivion");
            }
            const auto* def = portals::by_id(portal);
            return access.access_ready && def &&
                   (gates.opened(portal) ||
                    portals::allowed(*def,
                                     [this](auto item)
                                     {
                                         return access.items.contains(std::wstring{item});
                                     }));
        }
        bool gate_locked(int portal)
        {
            // A Gate Opener cannot replace the KH0.2 chapter's Oblivion access.
            if (portal == portals::dark_world_portal_id && access.kh02_enabled)
            {
                return false;
            }
            if (!access.access_ready || !portals::by_id(portal) || gates.opened(portal))
            {
                return false;
            }
            for (const auto& entry : entries)
            {
                if (entry.id == portal)
                {
                    auto* actor = entry.actor.Get();
                    if (!is_uobject_valid_fast(actor))
                    {
                        return false;
                    }
                    Call query(actor, L"IsEnabled");
                    auto* result = query.field(L"OutEnabled");
                    if (!result->IsA<FBoolProperty>())
                    {
                        return false;
                    }
                    query.run();
                    return !static_cast<FBoolProperty*>(result)->GetPropertyValueInContainer(
                        query.data.data());
                }
            }
            if (access.party_portals)
            {
                return !owned(portal);
            }
            for (const auto& world : gate_opener::worlds)
            {
                if (world.portal == portal)
                {
                    return !access.items.contains(std::wstring{world.keyblade});
                }
            }
            return false;
        }
        static void mode_thunk(UObject* object, FFrame& stack, void* result)
        {
            auto* frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
            if (frame->Code)
            {
                ++frame->Code;
            }
            if (result)
            {
                *static_cast<uint8_t*>(result) =
                    instance &&
                    (instance->access.party_portals || instance->gates.opened(id(object)) ||
                     (id(object) == portals::dark_world_portal_id &&
                      instance->access.kh02_enabled));
            }
        }
        static void enabled_thunk(UObject* o, FFrame& stack, void* result)
        {
            auto* frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
            if (frame->Code)
            {
                ++frame->Code;
            }
            if (result)
            {
                *static_cast<uint8_t*>(result) = instance && instance->owned(id(o));
            }
        }
        static void portraits_thunk(UObject* o, FFrame& stack, void* result)
        {
            auto* frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
            if (frame->Code)
            {
                ++frame->Code;
            }
            const bool show = instance && instance->party_portrait(id(o)) &&
                              instance->access.access_ready && !instance->owned(id(o));
            if (result)
            {
                *static_cast<uint8_t*>(result) = show;
            }
            // The stock paint event may have created a new mesh-slot MID. Reassert
            // the cached portrait in response to that event, not by polling slots.
            if (show)
            {
                instance->presentation_pending = true;
                instance->context_refresh_pending = true;
            }
        }
        static void kh02_route_thunk(UObject* object, FFrame& stack, void* result)
        {
            auto* frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
            if (frame->Code)
            {
                ++frame->Code;
            }
            if (result)
            {
                *static_cast<uint8_t*>(result) =
                    instance && id(object) == portals::dark_world_portal_id &&
                    instance->access.access_ready && instance->access.kh02_enabled;
            }
        }
        static void dark_world_thunk(UObject* object, FFrame& stack, void* result)
        {
            auto* frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
            if (frame->Code)
            {
                ++frame->Code;
            }
            const bool allowed = instance && id(object) == portals::dark_world_portal_id &&
                                 instance->access.access_ready &&
                                 (!instance->access.party_portals ||
                                  instance->gates.opened(portals::dark_world_portal_id) ||
                                  instance->access.items.contains(L"Party Member: Aqua"));
            if (result)
            {
                *static_cast<uint8_t*>(result) = allowed;
            }
        }
        void bind(UClass* klass)
        {
            for (auto name :
                 {L"APPartyPortalMode", L"APPartyPortalEnabled", L"APPartyPortalPortraits",
                  L"APKH02RouteActive", L"APDarkWorldAvailable"})
            {
                auto* cdo = klass->GetClassDefaultObject();
                auto* fn =
                    is_uobject_valid_fast(cdo) ? cdo->GetFunctionByNameInChain(name) : nullptr;
                auto* ret = is_uobject_valid_fast(fn) ? fn->GetReturnProperty() : nullptr;
                if (!ret || !ret->IsA<FBoolProperty>() || fn->GetNumParms() != 1 ||
                    fn->GetParmsSize() != 1 || fn->GetReturnValueOffset() != 0)
                {
                    throw std::runtime_error("portal_query_signature");
                }
                fn->SetFuncPtr(
                    std::wstring_view{name} == L"APKH02RouteActive"        ? kh02_route_thunk
                    : std::wstring_view{name} == L"APDarkWorldAvailable"   ? dark_world_thunk
                    : std::wstring_view{name} == L"APPartyPortalMode"      ? mode_thunk
                    : std::wstring_view{name} == L"APPartyPortalPortraits" ? portraits_thunk
                                                                           : enabled_thunk);
                fn->GetFunctionFlags() |= FUNC_Native;
            }
        }
        UObject* load(UObject* context, const std::wstring& path)
        {
            auto& value = portraits[path];
            if (auto* source = value.source.Get(); is_uobject_valid_fast(source))
            {
                return step >= value.ready_step ? source : nullptr;
            }
            if (auto* source = find_loaded_runtime_asset(path))
            {
                // A soft load alone does not retain the texture. Keep it alive
                // while the deferred Canvas draw uses its render resource, and
                // allow that resource to finish initialization before the bake.
                value.rooted = !source->HasAnyInternalFlags(EInternalObjectFlags::RootSet);
                if (value.rooted)
                {
                    source->SetRootSet();
                }
                value.source = make_uobject_weak(source);
                if (value.source.Get() != source)
                {
                    throw std::runtime_error("portal_portrait_weak");
                }
                value.ready_step = step + 1;
                return nullptr;
            }
            if (!requested.contains(path) &&
                request_runtime_asset_load(context, path, nullptr,
                                           portal_asset_load_uuid_base +
                                               static_cast<int>(requested.size())))
            {
                requested.insert(path);
            }
            return nullptr;
        }
        void set_material(UObject* mesh, UObject* mat)
        {
            Call set(mesh, L"SetMaterial");
            set.index();
            if (!write_object_function_param(set.function, set.data.data(), L"Material", mat))
            {
                throw std::runtime_error("portal_material");
            }
            set.run();
        }
        UObject* canvas(UObject* context, UObject* portrait, std::wstring_view name,
                        float face_center)
        {
            auto& value = canvases[std::wstring{name}];
            auto* target = value.target.Get();
            if (value.ready && is_uobject_valid_fast(target))
            {
                return target;
            }
            auto* rendering = library(L"/Script/Engine.Default__KismetRenderingLibrary");
            if (!is_uobject_valid_fast(target))
            {
                value = {};
                Call create(rendering, L"CreateRenderTarget2D");
                create.object_arg(L"WorldContextObject", context);
                create.integer(L"Width", portrait_canvas_pixels);
                create.integer(L"Height", portrait_canvas_pixels);
                create.run();
                target = try_read_function_object_param(create.function, create.data.data(),
                                                        L"ReturnValue");
                if (!is_uobject_valid_fast(target))
                {
                    throw std::runtime_error("portal_render_target");
                }
                target->SetRootSet();
                value.target = make_uobject_weak(target);
                if (value.target.Get() != target)
                {
                    throw std::runtime_error("portal_canvas_weak");
                }
                value.created_step = step;
                // Resource initialization schedules a deferred clear. Paint on a
                // later refresh, after that clear, so it cannot erase the icon.
                return nullptr;
            }
            if (step <= value.created_step)
            {
                return nullptr;
            }
            // Copy RGBA directly with the Canvas texture shader. Its opaque blend
            // preserves the sampled alpha; ordinary Translucent writes RGB only.
            // An opaque material draw instead writes scene-color alpha, not the
            // portrait mask, and must not be used for this cached icon texture.
            Call clear_target(rendering, L"ClearRenderTarget2D");
            clear_target.object_arg(L"WorldContextObject", context);
            clear_target.object_arg(L"TextureRenderTarget", target);
            clear_target.floats<4>(L"ClearColor", {0.f, 0.f, 0.f, 0.f});
            clear_target.run();
            Call begin(rendering, L"BeginDrawCanvasToRenderTarget");
            begin.object_arg(L"WorldContextObject", context);
            begin.object_arg(L"TextureRenderTarget", target);
            begin.run();
            auto* drawing_canvas =
                try_read_function_object_param(begin.function, begin.data.data(), L"Canvas");
            if (!is_uobject_valid_fast(drawing_canvas))
            {
                throw std::runtime_error("portal_drawing_canvas");
            }
            Call draw(drawing_canvas, L"K2_DrawTexture");
            draw.object_arg(L"RenderTexture", portrait);
            // This is the stock mark's own UV space; the shader places that mark
            // inside the opening. Do not pre-offset it into the portal mesh's UVs.
            draw.floats<2>(L"ScreenPosition",
                           {(0.5f - face_center * portrait_width_fraction) * portrait_canvas_extent,
                            (.5f - .5f * portrait_height_fraction) * portrait_canvas_extent});
            draw.floats<2>(L"ScreenSize", {portrait_width_fraction * portrait_canvas_extent,
                                           portrait_height_fraction * portrait_canvas_extent});
            draw.floats<2>(L"CoordinatePosition", {0.f, 0.f});
            draw.floats<2>(L"CoordinateSize", {1.f, 1.f});
            draw.floats<4>(L"RenderColor", {portrait_tint_channel, portrait_tint_channel,
                                            portrait_tint_channel, 1.f});
            auto* blend = draw.field(L"BlendMode");
            if (blend->GetElementSize() != 1)
            {
                throw std::runtime_error("portal_canvas_blend");
            }
            *blend->ContainerPtrToValuePtr<uint8_t>(draw.data.data()) =
                0; // EBlendMode::BLEND_Opaque
            draw.run();
            Call end(rendering, L"EndDrawCanvasToRenderTarget");
            end.object_arg(L"WorldContextObject", context);
            auto* source = begin.field(L"Context");
            auto* destination = end.field(L"Context");
            if (!source->IsA<FStructProperty>() || !destination->IsA<FStructProperty>() ||
                source->GetElementSize() != destination->GetElementSize())
            {
                throw std::runtime_error("portal_canvas_context");
            }
            std::memcpy(destination->ContainerPtrToValuePtr<void>(end.data.data()),
                        source->ContainerPtrToValuePtr<void>(begin.data.data()),
                        source->GetElementSize());
            end.run();
            value.ready = true;
            return target;
        }
        void prepare(UObject* context, const Entry& e)
        {
            if (!party_portrait(e.id) || !access.access_ready || owned(e.id))
            {
                return;
            }
            const auto* def = portals::by_id(e.id);
            if (!def)
            {
                return;
            }
            if (auto* source = load(context, portrait_path(def->portrait_a)))
            {
                canvas(context, source, def->portrait_a, def->face_a_x);
            }
            if (!def->portrait_b.empty())
            {
                if (auto* source = load(context, portrait_path(def->portrait_b)))
                {
                    canvas(context, source, def->portrait_b, def->face_b_x);
                }
            }
        }
        bool prepared(const Entry& e) const
        {
            if (!party_portrait(e.id) || !access.access_ready || owned(e.id))
            {
                return true;
            }
            const auto* def = portals::by_id(e.id);
            if (!def)
            {
                return true;
            }
            auto ready = [this](std::wstring_view name)
            {
                const auto value = canvases.find(std::wstring{name});
                return value != canvases.end() && value->second.ready &&
                       is_uobject_valid_fast(value->second.target.Get());
            };
            return ready(def->portrait_a) && (def->portrait_b.empty() || ready(def->portrait_b));
        }
        void animate(Entry& e, float time)
        {
            if (!e.shown)
            {
                return;
            }
            auto* mid = e.material.Get();
            const auto* def = portals::by_id(e.id);
            if (!is_uobject_valid_fast(mid) || !def)
            {
                return;
            }
            if (def->second.empty() && e.current_face == 0)
            {
                return;
            }
            const float cycle = std::fmod(std::max(0.f, time), portrait_cycle_seconds),
                        progress = std::fmod(cycle, portrait_face_seconds);
            const int face = def->second.empty() ? 0 : cycle >= portrait_face_seconds ? 1 : 0;
            if (e.current_face != face)
            {
                // Swap while fully dark, even if a slow frame crossed the boundary.
                scalar(mid, L"Mark_emmsivePower", 0.f);
                e.mark_power = 0.f;
                const auto& value =
                    canvases.at(std::wstring{face ? def->portrait_b : def->portrait_a});
                auto* target = value.target.Get();
                if (!is_uobject_valid_fast(target))
                {
                    throw std::runtime_error("portal_canvas_expired");
                }
                texture(mid, L"Mark_Texture", target);
                e.current_face = face;
            }
            auto smooth = [](float x)
            {
                x = std::clamp(x, 0.f, 1.f);
                return x * x * (3.f - 2.f * x);
            };
            const float fade =
                def->second.empty()
                    ? 1.f
                    : smooth(progress / portrait_fade_seconds) *
                          smooth((portrait_face_seconds - progress) / portrait_fade_seconds);
            const float power = portrait_emissive_power * fade;
            if (std::abs(power - e.mark_power) > portal_parameter_tolerance)
            {
                scalar(mid, L"Mark_emmsivePower", power);
                e.mark_power = power;
            }
        }
        void apply_access(Entry& e)
        {
            if ((!access.party_portals && !gates.opened(e.id) &&
                 !(e.id == portals::dark_world_portal_id && access.kh02_enabled)) ||
                !access.access_ready)
            {
                return;
            }
            auto* actor = e.actor.Get();
            if (!is_uobject_valid_fast(actor))
            {
                return;
            }
            const bool enabled = owned(e.id);
            if (!e.stock_enabled || *e.stock_enabled != enabled)
            {
                // Restore the stock slot before its Blueprint paints the open icon.
                if (e.shown)
                {
                    if (auto* mesh = object_property(actor, L"MyMesh"))
                    {
                        if (auto* original = e.original.Get(); is_uobject_valid_fast(original))
                        {
                            set_material(mesh, original);
                        }
                    }
                }
                e.shown = false;
                e.current_face = -1;
                e.mark_power = -1.f;
                Call change(actor, enabled ? L"EnablePortal" : L"DisablePortal");
                change.run();
                e.stock_enabled = enabled;
            }
            if (!e.timer_stopped)
            {
                auto* timer = actor->GetPropertyByNameInChain(L"TEnabledTimer");
                Call stop(library(L"/Script/Engine.Default__KismetSystemLibrary"),
                          L"K2_ClearAndInvalidateTimerHandle");
                auto* handle = stop.field(L"Handle");
                if (!timer || !timer->IsA<FStructProperty>() || !handle->IsA<FStructProperty>() ||
                    timer->GetElementSize() != handle->GetElementSize())
                {
                    throw std::runtime_error("portal_timer_layout");
                }
                stop.object_arg(L"WorldContextObject", actor);
                std::memcpy(handle->ContainerPtrToValuePtr<void>(stop.data.data()),
                            timer->ContainerPtrToValuePtr<void>(actor), timer->GetElementSize());
                stop.run();
                std::memcpy(timer->ContainerPtrToValuePtr<void>(actor),
                            handle->ContainerPtrToValuePtr<void>(stop.data.data()),
                            timer->GetElementSize());
                e.timer_stopped = true;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                ++stopped_timers;
#endif
            }
        }
        void render(UObject* context, Entry& e)
        {
            auto* actor = e.actor.Get();
            if (!is_uobject_valid_fast(actor))
            {
                return;
            }
            auto* mesh = object_property(actor, L"MyMesh");
            if (!mesh)
            {
                return;
            }
            const bool show = party_portrait(e.id) && access.access_ready && !owned(e.id);
            if (!show)
            {
                if (e.shown)
                {
                    if (auto* old = e.original.Get(); is_uobject_valid_fast(old))
                    {
                        set_material(mesh, old);
                    }
                }
                e.shown = false;
                return;
            }
            const auto* def = portals::by_id(e.id);
            if (!def)
            {
                return;
            }
            auto* a = load(context, portrait_path(def->portrait_a));
            auto* b = def->portrait_b.empty() ? a : load(context, portrait_path(def->portrait_b));
            if (!a || !b)
            {
                return;
            }
            auto* image_a = canvas(context, a, def->portrait_a, def->face_a_x);
            auto* image_b = def->portrait_b.empty()
                                ? image_a
                                : canvas(context, b, def->portrait_b, def->face_b_x);
            if (!image_a || !image_b)
            {
                return;
            }
            auto* mid = e.material.Get();
            if (!is_uobject_valid_fast(mid))
            {
                Call get(mesh, L"GetMaterial");
                get.index();
                get.run();
                auto* original =
                    try_read_function_object_param(get.function, get.data.data(), L"ReturnValue");
                if (!is_uobject_valid_fast(original))
                {
                    return;
                }
                if (!e.original.Get())
                {
                    e.original = make_uobject_weak(original);
                    e.rooted = !original->HasAnyInternalFlags(EInternalObjectFlags::RootSet);
                    if (e.rooted)
                    {
                        original->SetRootSet();
                    }
                }
                // Inherit the real stock material and its world-specific settings.
                // Only the mark texture changes; the portal itself keeps its shader.
                // A MID cannot be another MID's parent, and the mesh factory would
                // return/mutate the existing MID. Create a separate instance from
                // its constant parent and copy the stock runtime overrides once.
                const bool dynamic =
                    original->GetClassPrivate()->GetName() == L"MaterialInstanceDynamic";
                auto* stock_parent = dynamic ? object_property(original, L"Parent") : original;
                if (!is_uobject_valid_fast(stock_parent))
                {
                    throw std::runtime_error("portal_stock_parent");
                }
                Call make(library(L"/Script/Engine.Default__KismetMaterialLibrary"),
                          L"CreateDynamicMaterialInstance");
                make.object_arg(L"WorldContextObject", context);
                make.object_arg(L"Parent", stock_parent);
                make.run();
                mid =
                    try_read_function_object_param(make.function, make.data.data(), L"ReturnValue");
                if (!is_uobject_valid_fast(mid) || mid == original ||
                    object_property(mid, L"Parent") != stock_parent)
                {
                    throw std::runtime_error("portal_stock_parent_readback");
                }
                e.material_rooted = !mid->HasAnyInternalFlags(EInternalObjectFlags::RootSet);
                if (e.material_rooted)
                {
                    mid->SetRootSet();
                }
                e.material = make_uobject_weak(mid);
                if (e.material.Get() != mid)
                {
                    throw std::runtime_error("portal_material_weak");
                }
                if (dynamic)
                {
                    Call copy(mid, L"CopyInterpParameters");
                    copy.object_arg(L"Source", original);
                    copy.run();
                }
                scalar(mid, L"Mark_OffsetX_1", 0.f);
                scalar(mid, L"Mark_OffsetY_2", portrait_mark_vertical_offset);
                scalar(mid, L"Mark_OffsetIntensity", portrait_mark_offset_intensity);
                // Locked portals normally force the backdrop black. Retain a dim
                // portion of the stock haze around/over the recessed portrait.
                scalar(mid, L"Switch_Black", portrait_backdrop_black_switch);
                scalar(mid, L"Mark_BumpOffset", portrait_mark_bump_offset);
                // Keep the stock moving haze but neutralize its blue/red palette
                // so it does not cast a blue tint over the character portraits.
                color(mid, L"Color_Bluie", portrait_haze_color);
                color(mid, L"Color_Red", portrait_haze_color);
                e.current_face = -1;
            }
            // Called only for bootstrap, access changes or a stock paint event.
            Call get(mesh, L"GetMaterial");
            get.index();
            get.run();
            if (try_read_function_object_param(get.function, get.data.data(), L"ReturnValue") !=
                mid)
            {
                set_material(mesh, mid);
            }
            e.shown = true;
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        void visual_probe(UObject* context)
        {
            const auto request = access.bridge / L"portal_visual_probe.request";
            if (!std::filesystem::exists(request))
            {
                return;
            }
            const auto output = access.bridge / L"portal_material_probes";
            std::filesystem::create_directories(output);
            std::ostringstream snapshot;
            snapshot << "{\"portals\":[";
            bool first = true;
            Entry* example = nullptr;
            for (auto& e : entries)
            {
                auto* actor = e.actor.Get();
                if (!is_uobject_valid_fast(actor))
                {
                    continue;
                }
                auto* mesh = object_property(actor, L"MyMesh");
                if (!mesh)
                {
                    continue;
                }
                Call actual(mesh, L"GetMaterial");
                actual.index();
                actual.run();
                auto* painted = try_read_function_object_param(actual.function, actual.data.data(),
                                                               L"ReturnValue");
                auto* mid = e.shown ? e.material.Get() : painted;
                if (!is_uobject_valid_fast(mid))
                {
                    continue;
                }
                if (!example && e.shown)
                {
                    example = &e;
                }
                if (!first)
                {
                    snapshot << ',';
                }
                first = false;
                auto* parent = object_property(mid, L"Parent");
                Call enabled(actor, L"IsEnabled");
                enabled.run();
                auto* answer = enabled.field(L"OutEnabled");
                if (!answer->IsA<FBoolProperty>())
                {
                    throw std::runtime_error("portal_enabled_probe_layout");
                }
                auto* timer = actor->GetPropertyByNameInChain(L"TEnabledTimer");
                Call active(library(L"/Script/Engine.Default__KismetSystemLibrary"),
                            L"K2_IsTimerActiveHandle");
                auto* handle = active.field(L"Handle");
                if (!timer || !timer->IsA<FStructProperty>() || !handle->IsA<FStructProperty>() ||
                    timer->GetElementSize() != handle->GetElementSize())
                {
                    throw std::runtime_error("portal_timer_probe_layout");
                }
                active.object_arg(L"WorldContextObject", actor);
                std::memcpy(handle->ContainerPtrToValuePtr<void>(active.data.data()),
                            timer->ContainerPtrToValuePtr<void>(actor), timer->GetElementSize());
                active.run();
                auto* timer_answer = active.field(L"ReturnValue");
                if (!timer_answer->IsA<FBoolProperty>())
                {
                    throw std::runtime_error("portal_timer_probe_result");
                }
                Call mark(mid, L"K2_GetTextureParameterValue");
                write_name_or_string_function_param(mark.field(L"ParameterName"), mark.data.data(),
                                                    L"Mark_Texture");
                mark.run();
                auto* icon =
                    try_read_function_object_param(mark.function, mark.data.data(), L"ReturnValue");
                snapshot << "{\"id\":" << e.id
                         << ",\"assigned\":" << (painted == mid ? "true" : "false")
                         << ",\"portrait_shown\":" << (e.shown ? "true" : "false")
                         << ",\"enabled\":"
                         << (static_cast<FBoolProperty*>(answer)->GetPropertyValueInContainer(
                                 enabled.data.data())
                                 ? "true"
                                 : "false")
                         << ",\"unlock_timer_active\":"
                         << (static_cast<FBoolProperty*>(timer_answer)
                                     ->GetPropertyValueInContainer(active.data.data())
                                 ? "true"
                                 : "false")
                         << ",\"parent\":\"" << json_escape(parent ? parent->GetFullName() : L"")
                         << "\",\"mark\":\"" << json_escape(icon ? icon->GetFullName() : L"")
                         << "\",\"scalars\":{";
                bool comma = false;
                for (auto name : {L"Switch_Mark", L"Switch_Black", L"Mark_Opacity",
                                  L"Mark_emmsivePower", L"Mark_BumpOffset", L"Mark_OffsetY_2"})
                {
                    if (comma)
                    {
                        snapshot << ',';
                    }
                    comma = true;
                    snapshot << '"' << json_escape(name) << "\":" << read_scalar(mid, name);
                }
                snapshot << "},\"colors\":{";
                comma = false;
                for (auto name : {L"Color_Bluie", L"Color_Red"})
                {
                    Call read(mid, L"K2_GetVectorParameterValue");
                    write_name_or_string_function_param(read.field(L"ParameterName"),
                                                        read.data.data(), name);
                    read.run();
                    auto* returned = read.field(L"ReturnValue");
                    if (!returned->IsA<FStructProperty>() ||
                        returned->GetElementSize() != sizeof(std::array<float, 4>))
                    {
                        throw std::runtime_error("portal_probe_color_layout");
                    }
                    std::array<float, 4> value{};
                    std::memcpy(value.data(),
                                returned->ContainerPtrToValuePtr<void>(read.data.data()),
                                sizeof(value));
                    if (comma)
                    {
                        snapshot << ',';
                    }
                    comma = true;
                    snapshot << '"' << json_escape(name) << "\":[" << value[0] << ',' << value[1]
                             << ',' << value[2] << ',' << value[3] << ']';
                }
                snapshot << "}}";
            }
            snapshot << "]}";
            write_text_file_atomic_verified(output / L"snapshot.json", snapshot.str());
            if (read_text_file(request).value_or("") == "snapshot")
            {
                std::filesystem::remove(request);
                return;
            }
            if (!example)
            {
                return;
            }
            auto* current = example->material.Get();
            auto* stock_parent = object_property(current, L"Parent");
            const auto* def = portals::by_id(example->id);
            auto* source = find_loaded_runtime_asset(portrait_path(def->portrait_a));
            const auto target = canvases.at(std::wstring{def->portrait_a}).target;
            auto* rendering = library(L"/Script/Engine.Default__KismetRenderingLibrary");
            for (int test = 0; test < portrait_material_test_count; ++test)
            {
                Call make(library(L"/Script/Engine.Default__KismetMaterialLibrary"),
                          L"CreateDynamicMaterialInstance");
                make.object_arg(L"WorldContextObject", context);
                make.object_arg(L"Parent", stock_parent);
                make.run();
                auto* mid =
                    try_read_function_object_param(make.function, make.data.data(), L"ReturnValue");
                if (!is_uobject_valid_fast(mid))
                {
                    throw std::runtime_error("portal_probe_mid");
                }
                mid->SetRootSet();
                const auto weak_mid = make_uobject_weak(mid);
                Call copy(mid, L"CopyInterpParameters");
                copy.object_arg(L"Source", current);
                copy.run();
                texture(mid, L"Mark_Texture",
                        test >= portrait_material_source_test_start ? source : target.Get());
                scalar(mid, L"Mark_emmsivePower", portrait_emissive_power);
                scalar(mid, L"Switch_Mark",
                       test % portrait_material_switch_cycle < portrait_material_switch_on_count
                           ? 1.f
                           : 0.f);
                scalar(mid, L"Mark_Opacity", test % portrait_material_opacity_cycle ? 1.f : 0.f);
                Call create(rendering, L"CreateRenderTarget2D");
                create.object_arg(L"WorldContextObject", context);
                create.integer(L"Width", portrait_canvas_pixels);
                create.integer(L"Height", portrait_canvas_pixels);
                create.run();
                auto* image = try_read_function_object_param(create.function, create.data.data(),
                                                             L"ReturnValue");
                if (!is_uobject_valid_fast(image))
                {
                    throw std::runtime_error("portal_probe_target");
                }
                image->SetRootSet();
                const auto weak_target = make_uobject_weak(image);
                Call draw(rendering, L"DrawMaterialToRenderTarget");
                draw.object_arg(L"WorldContextObject", context);
                draw.object_arg(L"TextureRenderTarget", image);
                draw.object_arg(L"Material", mid);
                draw.run();
                Call save(rendering, L"ExportRenderTarget");
                save.object_arg(L"WorldContextObject", context);
                save.object_arg(L"TextureRenderTarget", image);
                const auto name = L"case_" + std::to_wstring(test) + L".hdr";
                if (!write_name_or_string_function_param(save.field(L"FilePath"), save.data.data(),
                                                         output.wstring()) ||
                    !write_name_or_string_function_param(save.field(L"FileName"), save.data.data(),
                                                         name))
                {
                    throw std::runtime_error("portal_probe_export");
                }
                save.run();
                if (auto* object = weak_mid.Get(); is_uobject_valid_fast(object))
                {
                    object->ClearRootSet();
                }
                if (auto* object = weak_target.Get(); is_uobject_valid_fast(object))
                {
                    object->ClearRootSet();
                }
            }
            std::filesystem::remove(request);
        }
        void visual_tuning()
        {
            // Bounded local material experiments reuse the verified active bridge.
            // This is excluded together with the other native agent handlers.
            const auto text = read_text_file(access.bridge / L"portal_visual_tuning.json");
            if (!text || *text == applied_visual_tuning)
            {
                return;
            }
            for (auto& e : entries)
            {
                if (e.shown)
                {
                    if (auto* mid = e.material.Get(); is_uobject_valid_fast(mid))
                    {
                        for (auto name :
                             {L"Switch_Mark", L"Mark_Opacity", L"Switch_Black", L"Mark_OffsetY_2"})
                        {
                            const auto key = json_escape(name);
                            if (auto value = JsonFieldReader::number_field(*text, key);
                                value && std::isfinite(*value) && *value >= -1.0 && *value <= 1.0)
                            {
                                scalar(mid, name, static_cast<float>(*value));
                            }
                        }
                    }
                }
            }
            applied_visual_tuning = *text;
        }
#endif
        void refresh_access()
        {
            const auto generation = pipe::input_generation();
            if (generation != consumed_input_generation)
            {
                consumed_input_generation = generation;
                access_refresh_pending = true;
            }
            if (!access_refresh_pending)
            {
                return;
            }
            access_refresh_pending = false;
            final_pending = true;
            const auto before_allowed = access.allowed;
            const auto before_seed = access.seed;
            const bool before_ready = access.access_ready, before_party = access.party_portals;
            const bool before_kh02 = access.kh02_enabled,
                       before_oblivion = access.items.contains(L"Oblivion");
            access.refresh(
                [](auto&, auto&, auto, bool)
                {
                });
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            ++access_refreshes;
#endif
            if (before_allowed != access.allowed || before_seed != access.seed ||
                before_ready != access.access_ready || before_party != access.party_portals ||
                before_kh02 != access.kh02_enabled ||
                before_oblivion != access.items.contains(L"Oblivion"))
            {
                presentation_pending = true;
                context_refresh_pending = true;
                next_animation = {};
            }
        }
        void refresh_context()
        {
            // Consume an actual lifecycle/paint/receipt event once. Never keep a
            // pending check armed until travel happens to finish.
            context_refresh_pending = false;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            ++context_validations;
#endif
            auto* world = current_world();
            // A ready event from the departing world's fade cannot recreate roots.
            // The new world's player/map-ready event will arm a fresh check.
            if (travel_pending)
            {
                if (!is_uobject_valid_fast(world) || world == departing_world.Get())
                {
                    return;
                }
                travel_pending = false;
                departing_world = FWeakObjectPtr{};
            }
            auto* context = pawn();
            if (!is_uobject_valid_fast(context) || !is_uobject_valid_fast(world) ||
                (world->GetFullName().find(L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.") ==
                     std::wstring::npos &&
                 world->GetFullName().find(L"/Game/Maps/GardenOfAssemblage/umap/GoA_02.") ==
                     std::wstring::npos) ||
                static_cast<AActor*>(context)->GetWorld() != world)
            {
                if (!entries.empty() || !canvases.empty() || !portraits.empty())
                {
                    clear(false);
                }
                observed_pawn = FWeakObjectPtr{};
                observed_world = FWeakObjectPtr{};
                garden_active = false;
                return;
            }
            const bool changed = observed_pawn.Get() != context || observed_world.Get() != world;
            if (changed)
            {
                clear(false);
                observed_pawn = make_uobject_weak(context);
                observed_world = make_uobject_weak(world);
                access_refresh_pending = true;
                refresh_access();
            }
            garden_active = true;
            if (!is_uobject_valid_fast(final_actor.Get()))
            {
                bind_final_door(context);
            }
            final_pending = true;
            if (entries.empty())
            {
                for (auto* path : {L"/Game/Maps/GardenOfAssemblage/uasset/Textures/Portals/"
                                   L"Portal_Base.Portal_Base_C"})
                {
                    auto* klass = find_loaded_runtime_asset_class(path);
                    if (!klass)
                    {
                        continue;
                    }
                    bind(klass);
                    TArray<AActor*> actors;
                    UGameplayStatics::GetAllActorsOfClass(context, klass, actors);
                    for (auto* actor : actors)
                    {
                        if (is_uobject_valid_fast(actor) && portals::by_id(id(actor)))
                        {
                            Entry e;
                            e.actor = make_uobject_weak(actor);
                            e.id = id(actor);
                            entries.push_back(e);
                        }
                    }
                }
                RC::Output::send<RC::LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Party portals bound actors={} party={} "
                        "travel_guards=lifecycle_events\n"),
                    entries.size(), access.party_portals);
            }
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        void process_canvas_requests(UObject* context)
        {
            visual_tuning();
            visual_probe(context);
            const auto refresh_request = access.bridge / L"portal_refresh_canvases.request";
            if (std::filesystem::exists(refresh_request))
            {
                for (auto& [name, value] : canvases)
                {
                    if (auto* target = value.target.Get(); is_uobject_valid_fast(target))
                    {
                        target->ClearRootSet();
                    }
                }
                canvases.clear();
                for (auto& e : entries)
                {
                    e.current_face = -1;
                    e.shown = false;
                }
                presentation_pending = true;
                context_refresh_pending = true;
                std::filesystem::remove(refresh_request);
            }
            // Explicit local diagnostic request: inspect the offscreen icon
            // canvases, without a viewport capture or computer input.
            const auto capture_request = access.bridge / L"portal_capture_canvases.request";
            if (!canvases.empty() && std::filesystem::exists(capture_request))
            {
                const auto output = access.bridge / L"portal_canvases";
                std::filesystem::create_directories(output);
                for (auto& [name, value] : canvases)
                {
                    if (auto* target = value.target.Get();
                        value.ready && is_uobject_valid_fast(target))
                    {
                        Call export_image(
                            library(L"/Script/Engine.Default__KismetRenderingLibrary"),
                            L"ExportRenderTarget");
                        export_image.object_arg(L"WorldContextObject", context);
                        export_image.object_arg(L"TextureRenderTarget", target);
                        if (!write_name_or_string_function_param(export_image.field(L"FilePath"),
                                                                 export_image.data.data(),
                                                                 output.wstring()) ||
                            !write_name_or_string_function_param(export_image.field(L"FileName"),
                                                                 export_image.data.data(),
                                                                 name + L".hdr"))
                        {
                            throw std::runtime_error("portal_canvas_export");
                        }
                        export_image.run();
                    }
                }
                std::filesystem::remove(capture_request);
            }
        }
#endif
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        void record_first_present(std::chrono::steady_clock::time_point now)
        {
            if (!first_present_delay_ms && access.party_portals && access.access_ready &&
                !entries.empty() &&
                std::all_of(entries.begin(), entries.end(),
                            [this](const auto& e)
                            {
                                return owned(e.id) || e.shown;
                            }))
            {
                first_present_delay_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - *loading_closed_at)
                        .count();
                RC::Output::send<RC::LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Party portraits visible delay_ms={} "
                        "prepared_while_loading={}\n"),
                    *first_present_delay_ms, prepared_while_loading);
            }
        }
#endif
        std::ostringstream portal_status_prefix()
        {
            std::ostringstream status;
            status << "{\"party_portals\":" << (access.party_portals ? "true" : "false")
                   << ",\"ready\":" << (access.access_ready ? "true" : "false")
                   << ",\"presentation\":\"stock_portal_mark\",\"render_targets\":"
                   << canvases.size() << ",\"portals\":[";
            bool first = true;
            for (auto& e : entries)
            {
                if (!first)
                {
                    status << ',';
                }
                first = false;
                status << "{\"id\":" << e.id << ",\"owned\":" << (owned(e.id) ? "true" : "false")
                       << ",\"portrait_shown\":" << (e.shown ? "true" : "false")
                       << ",\"unlock_timer_stopped\":" << (e.timer_stopped ? "true" : "false")
                       << '}';
            }
            return status;
        }
        void publish_portal_status(std::ostringstream& status)
        {
            status << "]";
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            status << ",\"prepared_while_loading\":" << (prepared_while_loading ? "true" : "false")
                   << ",\"first_present_delay_ms\":";
            if (first_present_delay_ms)
            {
                status << *first_present_delay_ms;
            }
            else
            {
                status << "null";
            }
            status << ",\"access_refreshes\":" << access_refreshes
                   << ",\"presentation_passes\":" << presentation_passes
                   << ",\"stopped_timers\":" << stopped_timers
                   << ",\"context_validations\":" << context_validations;
#endif
            status << "}";
            if (!access.bridge.empty() && status.str() != published_status &&
                write_text_file_atomic_verified(access.bridge / L"runtime_portal_state.json",
                                                status.str()))
            {
                published_status = status.str();
            }
        }
        void tick(std::chrono::steady_clock::time_point now)
        {
            refresh_access();
            const auto view = reconciliation::view();
            if (gate_seed != view->seed || gate_epoch != view->epoch ||
                gates.spent != view->gates.spent)
            {
                gate_seed = view->seed;
                gate_epoch = view->epoch;
                gates = view->seed == access.seed && view->slot == access.ap_slot
                            ? view->gates
                            : gate_opener::State{};
                presentation_pending = true;
                context_refresh_pending = true;
            }
            try
            {
                sync_gate_inventory(*view);
                if (context_refresh_pending)
                {
                    refresh_context();
                }
                update_final_door();
                if (travel_pending || !garden_active || entries.empty() || now < next_animation)
                {
                    return;
                }
                next_animation = now + portal_animation_interval;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                const std::optional<std::filesystem::path> diagnostic_directory =
                    access.bridge.empty() ? std::nullopt : std::optional{access.bridge};
                const bool diagnostic_event = diagnostic_requests.update(diagnostic_directory);
#endif
                const bool animate_portraits = std::any_of(
                    entries.begin(), entries.end(),
                    [this](const auto& e)
                    {
                        const auto* def = portals::by_id(e.id);
                        return e.shown && (e.current_face < 0 || (def && !def->second.empty()));
                    });
                if (!presentation_pending && !animate_portraits
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                    && !diagnostic_event
#endif
                )
                    return;
                // Resolve the cached weak pawn only when doing real presentation,
                // animation or requested diagnostic work; no world/pawn discovery.
                auto* context = observed_pawn.Get();
                if (!is_uobject_valid_fast(context))
                {
                    clear(false);
                    garden_active = false;
                    observed_pawn = FWeakObjectPtr{};
                    observed_world = FWeakObjectPtr{};
                    return;
                }
                ++step;
                // Prepare only for the new pawn's verified viewport world. Keep
                // the asset/resource stages a frame apart, but do that work while
                // Garden is loading instead of adding seconds after arrival.
                const bool update_presentation = presentation_pending;
                if (update_presentation)
                {
                    for (auto& e : entries)
                    {
                        prepare(context, e);
                    }
                }
                if (update_presentation && !loading_closed())
                {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                    prepared_while_loading |=
                        !canvases.empty() && std::all_of(entries.begin(), entries.end(),
                                                         [this](const auto& e)
                                                         {
                                                             return prepared(e);
                                                         });
#endif
                    return;
                }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                if (!loading_closed_at)
                {
                    loading_closed_at = now;
                }
#endif
                if (update_presentation)
                {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                    ++presentation_passes;
#endif
                    bool complete = access.access_ready;
                    for (auto& e : entries)
                    {
                        apply_access(e);
                        render(context, e);
                        complete = complete && prepared(e) &&
                                   (owned(e.id) || !party_portrait(e.id) || e.shown);
                    }
                    presentation_pending = !complete;
                }
                // A presentation pass can have just made a solo portrait visible.
                if (animate_portraits || update_presentation)
                {
                    const float time = game_time(context);
                    for (auto& e : entries)
                    {
                        animate(e, time);
                    }
                }
                if (!update_presentation
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                    && !diagnostic_event
#endif
                )
                    return;
                auto status = portal_status_prefix();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                record_first_present(now);
                if (diagnostic_event)
                {
                    process_canvas_requests(context);
                }
#endif
                publish_portal_status(status);
            }
            catch (const std::exception& ex)
            {
                if (!warned)
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Portal service: {}\n"),
                        std::wstring{ex.what(), ex.what() + std::strlen(ex.what())});
                }
                warned = true;
            }
        }
    };
    RuntimePortalService::RuntimePortalService(RuntimeSpaceAccess::BridgeResolver bridge,
                                               std::function<UObject*()> pawn)
        : impl_(std::make_unique<Impl>(std::move(bridge), std::move(pawn)))
    {
    }
    RuntimePortalService::~RuntimePortalService() = default;
    void RuntimePortalService::on_map_jump()
    {
        if (Impl::instance)
        {
            Impl::instance->on_map_jump();
        }
    }
    void RuntimePortalService::on_map_jump_canceled()
    {
        if (Impl::instance)
        {
            Impl::instance->travel_pending = false;
            Impl::instance->departing_world = FWeakObjectPtr{};
            notify_context_ready();
        }
    }
    void RuntimePortalService::notify_party_member_received()
    {
        if (Impl::instance)
        {
            Impl::instance->access_refresh_pending = true;
        }
    }
    void RuntimePortalService::notify_context_ready()
    {
        if (Impl::instance)
        {
            Impl::instance->context_refresh_pending = true;
            Impl::instance->access_refresh_pending = true;
            Impl::instance->presentation_pending = true;
            Impl::instance->next_animation = {};
        }
    }
    void RuntimePortalService::tick(std::chrono::steady_clock::time_point now)
    {
        impl_->tick(now);
    }
    auto RuntimePortalService::gate_opener_menu() -> std::wstring
    {
        auto* service = Impl::instance;
        const auto view = reconciliation::view();
        if (!service || !view->ready || !service->access.access_ready ||
            view->seed != service->access.seed || view->slot != service->access.ap_slot)
        {
            return L"0\tWaiting for this seed's inventory.";
        }
        std::wstring result = std::to_wstring(view->gates.available(view->gate_receipts)) +
                              L"\tSelect a locked world to open it";
        try
        {
            for (const auto& world : gate_opener::worlds)
            {
                const bool locked =
                    !view->gates.opened(world.portal) && service->gate_locked(world.portal);
                result += L"\n" + std::to_wstring(world.portal) + L"\t" + std::wstring{world.name} +
                          (locked ? L"\t1\t" : L"\t0\t") + gate_opener::texture(world);
            }
        }
        catch (...)
        {
            return L"0\tWorld access is not ready. Reopen this page after loading.";
        }
        return result;
    }
    auto RuntimePortalService::use_gate_opener(int portal) -> bool
    {
        auto* service = Impl::instance;
        const auto view = reconciliation::view();
        if (!service || !view->ready || view->seed != service->access.seed ||
            view->slot != service->access.ap_slot || !view->gates.available(view->gate_receipts) ||
            view->gates.opened(portal))
        {
            return false;
        }
        try
        {
            if (!service->gate_locked(portal))
            {
                return false;
            }
        }
        catch (...)
        {
            return false;
        }
        reconciliation::open_gate(portal, *view);
        return true;
    }
} // namespace kh3ap
