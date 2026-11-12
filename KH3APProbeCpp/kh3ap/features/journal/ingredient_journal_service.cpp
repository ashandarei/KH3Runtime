#include <polyhook2/Detour/x64Detour.hpp>
#include <memory>
#include <optional>
#include <span>
#include "kh3ap/features/journal/ingredient_journal_service.h"
#include "kh3ap/features/journal/ingredient_journal_map_service.h"
#include "kh3ap/features/journal/ingredient_journal_catalog.h"
#include "kh3ap/features/journal/adversary_journal_catalog.h"
#include "kh3ap/features/journal/adversary_service.h"
#include "kh3ap/features/treasure/chest_persistence_service.h"
#include "kh3ap/features/world/portals/portal_service.h"
#include "kh3ap/features/world/portals/gate_opener_policy.h"
#include "kh3ap/features/journal/ingredient_journal_movie_data.h"
#include "kh3ap/features/moogle_shop/shop_movie_patch.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/features/grants/abilities/ability_snapshot_helpers.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/core/input/options_input_ownership.h"
#include "kh3ap/core/input/menu_face_button.h"
#include "kh3ap/features/ui/ui_sound_service.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/events/event_work.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/core/conditional_profile_scope.h"
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/bridge/utilities.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <chrono>
#include <array>
#include <utility>
#include <format>
#include <sstream>
#include <cstring>

using namespace RC;
using namespace RC::Unreal;
namespace kh3ap
{
    namespace
    {
        enum JournalNavigation : int
        {
            navigate_left = 1,
            navigate_right,
            navigate_up,
            navigate_down,
            navigate_previous_category,
            navigate_next_category,
            navigate_back,
            navigate_previous_page,
            navigate_next_page,
            navigate_confirm = 11,
        };
        constexpr auto window_class_name_chars = 64;
        constexpr auto ingredient_home_icon_index = 9;
        constexpr auto ingredient_overlay_food_count = 20;
        constexpr auto ingredient_overlay_vanilla_food_count = 8;
        constexpr auto ingredient_overlay_movie_priority = 181;
        constexpr auto ingredient_dictionary_catalog_kind = 2;

        constexpr auto last_journal_navigation_command = navigate_confirm;
        constexpr auto max_journal_diagnostic_children = 12;
        constexpr int maximum_journal_movie_bytes = 4 * 1024 * 1024;
        constexpr int ingredient_catalog_count = 51;
        constexpr int adversary_catalog_count = 53;
        constexpr int ingredient_movie_load_request_id = 0x4b483351;
        constexpr int journal_asset_load_request_id_base = 0x4b483360;
        constexpr int focus_ingredients_navigation_command = 10;
        constexpr int focus_adversaries_navigation_command = 12;
        constexpr int focus_gates_navigation_command = 13;
        constexpr int focus_chests_navigation_command = 14;
        constexpr int chest_catalog_kind = 3;

        constexpr auto ingredient_journal_movie_timeout = std::chrono::seconds(30);
        constexpr auto ingredient_journal_readiness_retry = std::chrono::milliseconds(100);
        constexpr auto max_journal_movie_values = 4096;
        constexpr auto max_journal_movie_imports = 4096;
        constexpr auto ingredients_home_anchor = 13;
        constexpr auto adversaries_home_anchor = 23;
        constexpr auto gates_home_anchor = 12;
        constexpr auto chests_home_anchor = 33;

        constexpr uintptr_t egs_journal_state_refresh_rva = 0x190f9c0;
        constexpr uintptr_t egs_treasure_availability_store_rva = 0x19106e0;
        constexpr uintptr_t steam_journal_state_refresh_rva = 0x15b2c20;
        constexpr uintptr_t steam_treasure_availability_store_rva = 0x15b3607;
        constexpr size_t treasure_availability_offset = 0x5659;
        constexpr uint8_t egs_journal_state_refresh_signature[]{
            0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
            0x48, 0x8d, 0xac, 0x24, 0x48, 0xff, 0xff, 0xff};
        constexpr uint8_t egs_treasure_availability_store_signature[]{
            0x45, 0x88, 0x8f, 0x59, 0x56, 0x00, 0x00};
        constexpr uint8_t steam_journal_state_refresh_signature[]{
            0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
            0x48, 0x8d, 0xac, 0x24, 0x58, 0xff, 0xff, 0xff};
        constexpr uint8_t steam_treasure_availability_store_signature[]{
            0x88, 0x97, 0x59, 0x56, 0x00, 0x00};

        constexpr auto focus_in = L"/Script/TresGame.TresUIParts:OnFocusIn";
        constexpr auto focus_out = L"/Script/TresGame.TresUIParts:OnFocusOut";
        constexpr auto anchor_in = L"/Script/TresGame.TresUIParts:OnAnchorIn";
        constexpr auto open_movie = L"/Script/ScaleformUI.GFxMoviePlayer:OpenMovie";
        constexpr auto movie_path = L"/Game/UI/Scaleform/42_mobilePortal/MobilePortal.MobilePortal";
        auto portal_context(UObject* object) -> bool
        {
            return is_uobject_valid_fast(object) &&
                   is_uobject_valid_fast(object->GetClassPrivate()) &&
                   object->GetClassPrivate()->GetName() == L"TresUIP_MobilePortal";
        }
        auto dictionary_context(UObject* object) -> bool
        {
            return is_uobject_valid_fast(object) &&
                   is_uobject_valid_fast(object->GetClassPrivate()) &&
                   (object->GetClassPrivate()->GetName() == L"TresUIP_MobileDictionary" ||
                    object->GetClassPrivate()->GetName() == L"TresUIP_MobileStory" ||
                    object->GetClassPrivate()->GetName() == L"TresUIP_MobileTreasure");
        }
        auto as_object(UObject* proxy) -> UObject*
        {
            if (!is_uobject_valid_fast(proxy) || !is_uobject_valid_fast(proxy->GetClassPrivate()))
            {
                return nullptr;
            }
            if (proxy->GetClassPrivate()->GetName() == L"GFxObject")
            {
                return proxy;
            }
            auto* object = try_read_reflected_object_property_value(proxy, L"m_ASObject");
            return is_uobject_valid_fast(object) ? object : nullptr;
        }
        auto foreground() -> bool
        {
            DWORD pid{};
            wchar_t name[window_class_name_chars]{};
            const auto window = GetForegroundWindow();
            return window && GetWindowThreadProcessId(window, &pid) &&
                   pid == GetCurrentProcessId() &&
                   GetClassNameW(window, name, static_cast<int>(std::size(name))) &&
                   std::wstring_view{name} == L"UnrealWindow";
        }
        auto adversary_collected() -> std::optional<std::wstring>
        {
            const auto defeated = read_adversary_defeats();
            if (!defeated)
            {
                return {};
            }
            std::wstring result;
            for (const auto& row : *defeated)
            {
                if (!result.empty())
                {
                    result += L',';
                }
                result += row;
            }
            return result;
        }
    } // namespace
    struct RuntimeIngredientJournalService::Impl
    {
        RuntimeIngredientJournalConfig config;
        std::pair<int, int> in_ids{}, out_ids{}, open_ids{}, anchor_ids{};
        bool registered{}, requested{}, movie_complete{}, movie_ready{}, published{},
            missing_logged{};
        FWeakObjectPtr context, root, engine, overlay_asset, overlay_movie, home_button, home_icon,
            home_mark, home_help;
        FWeakObjectPtr app_context, app_root, app_title;
        FWeakObjectPtr cursor_owner, cursor_root;
        std::optional<double> cursor_alpha;
        int last_home_anchor{-1};
        bool app_adversaries{};
        bool app_gates{};
        bool app_chests{};
        std::wstring chest_catalog;
        portals::Kh02Access kh02_access;
        std::shared_ptr<const reconciliation::View> gate_view;
        std::wstring gate_payload;
        int catalog_kind{-1};
        bool overlay_attempted{};
        bool food_requested{};
        bool overlay_visible{}, overlay_input_enabled{};
        std::wstring last_collected;
        RuntimeOptionsInputOwnership input;
        RuntimeUiSoundService ui_sound;
        double cursor_sound_serial{};
        RuntimeIngredientJournalMapService maps;
        std::chrono::steady_clock::time_point started{};
        RuntimeEventWork preload_work, page_work;
        uint64_t collected_revision{}, adversary_revision{}, chest_revision{};
        int64_t observed_epoch{-1};
        using RefreshJournalState = void (*)(uint8_t*, void*, void*, void*, void*);
        inline static uint64_t original_journal_state_refresh{};
        std::unique_ptr<PLH::x64Detour> journal_state_refresh_hook;

        static auto refresh_journal_state(uint8_t* journal_state, void* game_data,
                                          void* camp_data, void* ui_data, void* treasure_data)
            -> void
        {
            reinterpret_cast<RefreshJournalState>(original_journal_state_refresh)(
                journal_state, game_data, camp_data, ui_data, treasure_data);
            const auto binding = reconciliation::view();
            if (binding && !binding->seed.empty() && binding->chests &&
                binding->epoch == reconciliation::epoch())
            {
                // Camp rebuilds this transient flag from world-start progression.
                // The AP Chests page has its own seed catalog from the first menu.
                journal_state[treasure_availability_offset] = 1;
            }
        }

        auto install_journal_state_refresh() -> void
        {
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
                dos->e_lfanew >= 0x1000)
            {
                return;
            }
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE ||
                nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
            {
                return;
            }
            const auto build = native_build::identify(nt->FileHeader.TimeDateStamp,
                                                      nt->OptionalHeader.SizeOfImage);
            if (build == native_build::Build::Unsupported)
            {
                return;
            }
            const bool steam = build == native_build::Build::Steam;
            const auto refresh = base + (steam ? steam_journal_state_refresh_rva
                                               : egs_journal_state_refresh_rva);
            const auto store = base + (steam ? steam_treasure_availability_store_rva
                                             : egs_treasure_availability_store_rva);
            const std::span<const uint8_t> refresh_signature =
                steam ? steam_journal_state_refresh_signature : egs_journal_state_refresh_signature;
            const auto store_signature = steam
                ? std::span<const uint8_t>{steam_treasure_availability_store_signature}
                : std::span<const uint8_t>{egs_treasure_availability_store_signature};
            if (std::memcmp(reinterpret_cast<const void*>(refresh),
                            refresh_signature.data(), refresh_signature.size()) != 0 ||
                std::memcmp(reinterpret_cast<const void*>(store),
                            store_signature.data(), store_signature.size()) != 0)
            {
                Output::send<LogLevel::Warning>(
                    L"[KH3APProbeCpp] Chests menu availability signature mismatch\n");
                return;
            }
            auto hook = std::make_unique<PLH::x64Detour>(
                refresh, reinterpret_cast<uintptr_t>(&refresh_journal_state),
                &original_journal_state_refresh);
            if (!hook->hook())
            {
                Output::send<LogLevel::Warning>(
                    L"[KH3APProbeCpp] Chests menu availability hook failed\n");
                return;
            }
            journal_state_refresh_hook = std::move(hook);
            Output::send<LogLevel::Normal>(
                L"[KH3APProbeCpp] Chests menu availability hook installed storefront={}\n",
                steam ? L"steam" : L"egs");
        }

        explicit Impl(RuntimeIngredientJournalConfig value)
            : config(std::move(value)), maps(
                                            config.gfx_paths,
                                            [this]
                                            {
                                                return startup_asset_context();
                                            },
                                            [this]
                                            {
                                                return overlay_asset.Get();
                                            })
        {
        }
        ~Impl()
        {
            if (journal_state_refresh_hook && journal_state_refresh_hook->isHooked())
            {
                journal_state_refresh_hook->unHook();
            }
            if (registered)
            {
                try
                {
                    UObjectGlobals::UnregisterHook(focus_in, in_ids);
                }
                catch (...)
                {
                }
                try
                {
                    UObjectGlobals::UnregisterHook(focus_out, out_ids);
                }
                catch (...)
                {
                }
                try
                {
                    UObjectGlobals::UnregisterHook(open_movie, open_ids);
                }
                catch (...)
                {
                }
                try
                {
                    UObjectGlobals::UnregisterHook(anchor_in, anchor_ids);
                }
                catch (...)
                {
                }
            }
        }
        auto install_once() -> void
        {
            if (registered)
            {
                return;
            }
            in_ids = UObjectGlobals::RegisterHook(
                focus_in,
                [](UnrealScriptFunctionCallableContext&, void*)
                {
                },
                [this](UnrealScriptFunctionCallableContext& event, void*)
                {
                    const bool home = portal_context(event.Context);
                    const bool app = movie_ready && dictionary_context(event.Context) &&
                                     (last_home_anchor == ingredients_home_anchor ||
                                      last_home_anchor == adversaries_home_anchor ||
                                      last_home_anchor == gates_home_anchor ||
                                      last_home_anchor == chests_home_anchor);
                    if (!home && !app)
                    {
                        return;
                    }
                    // Observe the native focus event, then publish on the next tick.
                    // Never call Scaleform from inside its focus traversal.
                    auto* fn = get_frame_node(event.TheStack);
                    auto* params = get_frame_locals(event.TheStack);
                    // UIComponent is a TresASProxyTresCoreUIComponent, not a
                    // GFxObject. Resolve the proxy's owning AS object first.
                    auto* value =
                        as_object(try_read_function_object_param(fn, params, L"UIComponent"));
                    if (!is_uobject_valid_fast(value))
                    {
                        value = as_object(try_read_reflected_object_property_value(
                            event.Context, L"m_ASProxyUIComponent"));
                    }
                    if (!is_uobject_valid_fast(value))
                    {
                        return;
                    }
                    // UE4SS's assignment does not allocate a missing weak serial.
                    // KH3 UI proxies and GFx wrappers commonly start with serial 0.
                    if (app)
                    {
                        app_adversaries = last_home_anchor == adversaries_home_anchor;
                        catalog_kind = -1;
                        app_gates = last_home_anchor == gates_home_anchor;
                        app_chests = last_home_anchor == chests_home_anchor;
                        chest_catalog.clear();
                        gate_payload.clear();
                        app_context = make_uobject_weak(event.Context);
                        app_root = make_uobject_weak(value);
                        app_title = nullptr;
                        published = false;
                        missing_logged = false;
                        last_collected.clear();
                        page_work.request();
                        Output::send<LogLevel::Normal>(L"[KH3APProbeCpp] Journal native page focus "
                                                       L"captured anchor={} adversaries={}\n",
                                                       last_home_anchor, app_adversaries);
                        return;
                    }
                    app_context = nullptr;
                    app_root = nullptr;
                    app_title = nullptr;
                    context = make_uobject_weak(event.Context);
                    root = make_uobject_weak(value);
                    if (context.Get() != event.Context || root.Get() != value)
                    {
                        context = nullptr;
                        root = nullptr;
                        Output::send<LogLevel::Warning>(
                            L"[KH3APProbeCpp] Ingredient journal UI capture failed: native weak "
                            L"handles unavailable\n");
                        return;
                    }
                    published = false;
                    missing_logged = false;
                    last_collected.clear();
                    home_button = nullptr;
                    home_icon = nullptr;
                    home_mark = nullptr;
                    home_help = nullptr;
                    page_work.request();
                    Output::send<LogLevel::Normal>(L"[KH3APProbeCpp] Ingredient journal Gummiphone "
                                                   L"focus captured native_weak_handles=true\n");
                },
                nullptr);
            try
            {
                out_ids = UObjectGlobals::RegisterHook(
                    focus_out,
                    [](UnrealScriptFunctionCallableContext&, void*)
                    {
                    },
                    [this](UnrealScriptFunctionCallableContext& event, void*)
                    {
                        if (dictionary_context(event.Context) && app_context.Get() == event.Context)
                        {
                            app_context = nullptr;
                            app_root = nullptr;
                            app_title = nullptr;
                            published = false;
                            page_work.request();
                            return;
                        }
                        if (!portal_context(event.Context) || context.Get() != event.Context)
                        {
                            return;
                        }
                        context = nullptr;
                        root = nullptr;
                        published = false;
                        home_button = nullptr;
                        home_icon = nullptr;
                        home_mark = nullptr;
                        home_help = nullptr;
                        page_work.request();
                    },
                    nullptr);
                open_ids = UObjectGlobals::RegisterHook(
                    open_movie,
                    [this](UnrealScriptFunctionCallableContext& event, void*)
                    {
                        if (movie_complete)
                        {
                            return;
                        }
                        auto* asset = try_read_function_object_param(
                            get_frame_node(event.TheStack), get_frame_locals(event.TheStack),
                            L"SwfMovie");
                        if (is_uobject_valid_fast(asset) && asset->GetPathName() == movie_path)
                        {
                            patch_asset(asset);
                        }
                    },
                    [](UnrealScriptFunctionCallableContext&, void*)
                    {
                    },
                    nullptr);
                anchor_ids = UObjectGlobals::RegisterHook(
                    anchor_in,
                    [](UnrealScriptFunctionCallableContext&, void*)
                    {
                    },
                    [this](UnrealScriptFunctionCallableContext& event, void*)
                    {
                        if (portal_context(event.Context))
                        {
                            last_home_anchor = static_cast<int>(
                                read_function_int_param(get_frame_node(event.TheStack),
                                                        get_frame_locals(event.TheStack), L"Index")
                                    .value_or(-1));
                            page_work.request();
                        }
                        else if (dictionary_context(event.Context) &&
                                 app_context.Get() == event.Context)
                        {
                            page_work.request();
                        }
                    },
                    nullptr);
                registered = true;
            }
            catch (...)
            {
                UObjectGlobals::UnregisterHook(focus_in, in_ids);
                UObjectGlobals::UnregisterHook(focus_out, out_ids);
                throw;
            }
            install_journal_state_refresh();
        }
        auto startup_asset_context() -> UObject*
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
            auto* viewport = try_read_reflected_object_property_value(owner, L"GameViewport");
            auto* world = is_uobject_valid_fast(viewport)
                              ? try_read_reflected_object_property_value(viewport, L"World")
                              : nullptr;
            // Asset loading is allowed during title/startup. This context is never
            // used to grant an item; grant contexts continue to require Sora.
            auto* instance =
                is_uobject_valid_fast(world)
                    ? try_read_reflected_object_property_value(world, L"OwningGameInstance")
                    : nullptr;
            return is_uobject_valid_fast(instance) ? instance : nullptr;
        }
        auto preload(std::chrono::steady_clock::time_point now) -> void
        {
            if (movie_complete)
            {
                return;
            }
            if (auto* loaded = find_loaded_runtime_asset(movie_path); is_uobject_valid_fast(loaded))
            {
                patch_asset(loaded);
                if (movie_complete)
                {
                    return;
                }
            }
            if (!requested)
            {
                auto* owner = startup_asset_context();
                if (!is_uobject_valid_fast(owner))
                {
                    owner = config.context();
                }
                if (!is_uobject_valid_fast(owner))
                {
                    return;
                }
                requested = request_runtime_asset_load(owner, movie_path, nullptr,
                                                       ingredient_movie_load_request_id);
                if (!requested)
                {
                    return;
                }
                started = now;
                Output::send<LogLevel::Normal>(
                    L"[KH3APProbeCpp] Ingredient journal movie preload requested context={}\n",
                    owner->GetClassPrivate()->GetName());
            }
            auto* asset = find_loaded_runtime_asset(movie_path);
            if (is_uobject_valid_fast(asset))
            {
                patch_asset(asset);
            }
            if (!movie_complete && now - started >= ingredient_journal_movie_timeout)
            {
                movie_complete = true;
                Output::send<LogLevel::Warning>(
                    L"[KH3APProbeCpp] Ingredient journal preload timed out; original retained\n");
            }
        }
        auto patch_asset(UObject* asset) -> void
        {
            if (movie_complete)
            {
                return;
            }
            auto* property = is_uobject_valid_fast(asset)
                                 ? asset->GetPropertyByNameInChain(L"RawData")
                                 : nullptr;
            if (!property || property->GetClass().GetName() != L"ArrayProperty")
            {
                return;
            }
            auto* inner = static_cast<FArrayProperty*>(property)->GetInner();
            if (!inner || inner->GetClass().GetName() != L"ByteProperty" ||
                inner->GetElementSize() != 1)
            {
                return;
            }
            auto* bytes = property->ContainerPtrToValuePtr<RC::Unreal::TArray<uint8_t>>(asset);
            if (!bytes || bytes->Num() <= 0 || bytes->Num() > maximum_journal_movie_bytes ||
                !bytes->GetData())
            {
                return;
            }
            movie_complete = true;
            namespace patch = ingredient_journal_movie;
            std::vector<uint8_t> extended;
            if (!decode_shop_movie_patch({bytes->GetData(), static_cast<size_t>(bytes->Num())},
                                         patch::delta_bytes(), patch::original_size,
                                         patch::original_hash, patch::patched_size,
                                         patch::patched_hash, extended))
            {
                Output::send<LogLevel::Warning>(L"[KH3APProbeCpp] Ingredient journal refused "
                                                L"unknown Gummiphone movie; original retained\n");
                return;
            }
            RC::Unreal::TArray<uint8_t> replacement;
            replacement.Append(extended.data(), static_cast<int32_t>(extended.size()));
            *bytes = std::move(replacement);
            asset->SetRootSet();
            movie_ready = true;
            Output::send<LogLevel::Normal>(
                L"[KH3APProbeCpp] Ingredient journal movie ready mode=memory_extension "
                L"unique_ingredients=51 bytes={}\n",
                bytes->Num());
        }
        auto helper() -> UObject*
        {
            auto* movie = overlay_movie.Get();
            auto* value = is_uobject_valid_fast(movie)
                              ? try_read_reflected_object_property_value(movie, L"Root")
                              : root.Get();
            if (!movie_ready || !is_uobject_valid_fast(value))
            {
                return nullptr;
            }
            return try_gfx_invoke_string_arg_object(config.gfx_paths, value, L"getChildByName",
                                                    L"kh3apIngredients");
        }
        auto home_tile() -> UObject*
        {
            auto* native_root = root.Get();
            if (!portal_context(context.Get()) || !is_uobject_valid_fast(native_root))
            {
                return nullptr;
            }
            if (auto* cached = home_button.Get(); is_uobject_valid_fast(cached))
            {
                return cached;
            }
            auto* portal =
                try_gfx_get_member_value_object(config.gfx_paths, native_root, L"mobilePortal");
            auto* buttons =
                is_uobject_valid_fast(portal)
                    ? try_gfx_get_member_value_object(config.gfx_paths, portal, L"menuButtonSet")
                    : nullptr;
            auto* tile = is_uobject_valid_fast(buttons)
                             ? try_gfx_get_member_value_object(
                                   config.gfx_paths, buttons,
                                   std::format(L"menuButton{}", ingredients_home_anchor))
                             : nullptr;
            if (!is_uobject_valid_fast(tile) ||
                try_gfx_get_member_value_number(config.gfx_paths, tile, L"index") !=
                    ingredients_home_anchor)
            {
                return nullptr;
            }
            home_button = make_uobject_weak(tile);
            auto* icons =
                try_gfx_get_member_value_object(config.gfx_paths, tile, L"mobilePortalIconSet");
            auto* icon = is_uobject_valid_fast(icons)
                             ? try_gfx_get_member_value_object(config.gfx_paths, icons,
                                                               L"coMobilePortalIconL")
                             : nullptr;
            if (is_uobject_valid_fast(icon))
            {
                home_icon = make_uobject_weak(icon);
            }
            auto* mark = try_gfx_get_member_value_object(config.gfx_paths, tile, L"newMark");
            if (is_uobject_valid_fast(mark))
            {
                home_mark = make_uobject_weak(mark);
            }
            auto* help = try_gfx_get_member_value_object(config.gfx_paths, portal, L"helpText");
            auto* text =
                is_uobject_valid_fast(help)
                    ? try_gfx_get_member_value_object(config.gfx_paths, help, L"txHelpText")
                    : nullptr;
            if (is_uobject_valid_fast(text))
            {
                home_help = make_uobject_weak(text);
            }
            return tile;
        }
        auto publish_home_tile() -> void
        {
            auto* tile = home_tile();
            if (!is_uobject_valid_fast(tile))
            {
                return;
            }
            // Ingredients occupies Character Files; Adversaries remains separate.
            if (try_gfx_get_member_value_string(config.gfx_paths, tile, L"label") != L"Ingredients")
            {
                try_gfx_set_member_value_string(config.gfx_paths, tile, L"label", L"Ingredients");
            }
            if (try_gfx_get_member_value(config.gfx_paths, tile, L"enabled")
                    .find(L"ASBoolean=true") == std::wstring::npos)
            {
                try_gfx_set_member_value_bool(config.gfx_paths, tile, L"enabled", true);
            }
            auto* icon = home_icon.Get();
            // The native cooking symbol already lives in the portal's atlas.
            if (is_uobject_valid_fast(icon) &&
                try_gfx_get_member_value_number(config.gfx_paths, icon, L"index") !=
                    ingredient_home_icon_index)
            {
                try_gfx_set_member_value_int(config.gfx_paths, icon, L"index",
                                             ingredient_home_icon_index);
            }
            if (auto* mark = home_mark.Get();
                is_uobject_valid_fast(mark) &&
                try_gfx_get_member_value(config.gfx_paths, mark, L"visible")
                        .find(L"ASBoolean=true") != std::wstring::npos)
            {
                try_gfx_set_member_value_bool(config.gfx_paths, mark, L"visible", false);
            }
            if (try_gfx_get_member_value(config.gfx_paths, tile, L"selected")
                    .find(L"ASBoolean=true") != std::wstring::npos)
            {
                if (auto* text = home_help.Get(); is_uobject_valid_fast(text))
                {
                    constexpr auto description =
                        L"Track unique ingredients and see where to find them.";
                    if (try_gfx_get_member_value_string(config.gfx_paths, text, L"text") !=
                        description)
                    {
                        try_gfx_set_member_value_string(config.gfx_paths, text, L"text",
                                                        description);
                    }
                }
            }
            auto* portal =
                try_gfx_get_member_value_object(config.gfx_paths, root.Get(), L"mobilePortal");
            auto* buttons =
                try_gfx_get_member_value_object(config.gfx_paths, portal, L"menuButtonSet");
            auto* chests = try_gfx_get_member_value_object(config.gfx_paths, buttons, L"menuButton33");
            if (is_uobject_valid_fast(chests))
            {
                try_gfx_set_member_value_string(config.gfx_paths, chests, L"label", L"Chests");
                try_gfx_set_member_value_bool(config.gfx_paths, chests, L"enabled", true);
                if (last_home_anchor == chests_home_anchor)
                {
                    try_gfx_set_member_value_string(config.gfx_paths, home_help.Get(), L"text",
                        L"Track opened chests and see where to find them.");
                }
            }
            auto* enemies =
                try_gfx_get_member_value_object(config.gfx_paths, buttons, L"menuButton23");
            auto* gates =
                try_gfx_get_member_value_object(config.gfx_paths, buttons, L"menuButton12");
            if (is_uobject_valid_fast(gates))
            {
                try_gfx_set_member_value_bool(config.gfx_paths, gates, L"enabled", true);
                try_gfx_set_member_value_string(config.gfx_paths, gates, L"label", L"Gate Opener");
                if (try_gfx_get_member_value(config.gfx_paths, gates, L"selected")
                        .find(L"ASBoolean=true") != std::wstring::npos)
                {
                    try_gfx_set_member_value_string(
                        config.gfx_paths, home_help.Get(), L"text",
                        L"Spend a Gate Opener to permanently unlock a Garden world door.");
                }
            }
            if (is_uobject_valid_fast(enemies))
            {
                try_gfx_set_member_value_bool(config.gfx_paths, enemies, L"enabled", true);
                if (try_gfx_get_member_value_string(config.gfx_paths, enemies, L"label") !=
                    L"Adversaries")
                {
                    try_gfx_set_member_value_string(config.gfx_paths, enemies, L"label",
                                                    L"Adversaries");
                }
                if (try_gfx_get_member_value(config.gfx_paths, enemies, L"selected")
                        .find(L"ASBoolean=true") != std::wstring::npos)
                {
                    try_gfx_set_member_value_string(
                        config.gfx_paths, home_help.Get(), L"text",
                        L"Track unique enemy defeats and view their vanilla locations.");
                }
            }
        }
        auto publish_native_title() -> void
        {
            auto* owner = app_context.Get();
            if (!dictionary_context(owner) || app_chests)
            {
                return;
            }
            auto* title = app_title.Get();
            if (!is_uobject_valid_fast(title))
            {
                if (app_gates)
                {
                    title = as_object(
                        try_read_reflected_object_property_value(owner, L"m_ASProxyUIComponent"));
                    for (const auto* member : {L"header", L"anm", L"title", L"textField"})
                    {
                        title =
                            is_uobject_valid_fast(title)
                                ? try_gfx_get_member_value_object(config.gfx_paths, title, member)
                                : nullptr;
                    }
                }
                else
                {
                    auto* header =
                        as_object(try_read_reflected_object_property_value(owner, L"m_header"));
                    auto* enemy = is_uobject_valid_fast(header)
                                      ? try_gfx_get_member_value_object(
                                            config.gfx_paths, header,
                                            app_adversaries ? L"headerEnemy" : L"headerChara")
                                      : nullptr;
                    title = is_uobject_valid_fast(enemy)
                                ? try_gfx_get_member_value_object(config.gfx_paths, enemy,
                                                                  L"coHeaderText")
                                : nullptr;
                }
                if (!is_uobject_valid_fast(title))
                {
                    return;
                }
                app_title = make_uobject_weak(title);
            }
            const auto label = app_gates         ? L"GATE OPENER"
                               : app_adversaries ? L"ADVERSARIES"
                                                 : L"INGREDIENTS";
            if (try_gfx_get_member_value_string(config.gfx_paths, title, L"text") != label)
            {
                try_gfx_set_member_value_string(config.gfx_paths, title, L"text", label);
            }
        }
        auto make_overlay() -> void
        {
            if (overlay_attempted || !movie_ready)
            {
                return;
            }
            std::array<UObject*, ingredient_overlay_food_count> food{};
            bool food_ready = true;
            auto* owner_context = startup_asset_context();
            if (!is_uobject_valid_fast(owner_context))
            {
                owner_context = config.context();
            }
            for (int i = 0; i < static_cast<int>(food.size()); ++i)
            {
                const auto name = L"food00" + std::to_wstring(i + 1);
                const auto path = i < ingredient_overlay_vanilla_food_count
                                      ? L"/Game/UI/Icon/Item/" + name + L"." + name
                                      : gate_opener::texture(gate_opener::worlds[i - 8]);
                food[i] = find_loaded_runtime_asset(path);
                if (!is_uobject_valid_fast(food[i]))
                {
                    food_ready = false;
                    if (!food_requested && is_uobject_valid_fast(owner_context))
                    {
                        request_runtime_asset_load(owner_context, path, nullptr,
                                                   journal_asset_load_request_id_base + i);
                    }
                }
            }
            if (!food_ready)
            {
                if (is_uobject_valid_fast(owner_context))
                {
                    food_requested = true;
                }
                return;
            }
            overlay_attempted = true;
            auto* original = find_loaded_runtime_asset(movie_path);
            auto* movie_class = find_loaded_runtime_asset_class(L"/Script/ScaleformUI.SwfMovie");
            if (!is_uobject_valid_fast(original) || !is_uobject_valid_fast(movie_class))
            {
                return;
            }
            FStaticConstructObjectParameters construction(movie_class, original->GetOuterPrivate());
            construction.SetFlags = static_cast<EObjectFlags>(RF_Transient | RF_Public);
            auto* asset = UObjectGlobals::StaticConstructObject(construction);
            if (!is_uobject_valid_fast(asset))
            {
                return;
            }
            // A new transient SwfMovie has a distinct Scaleform cache identity.
            // Native code/art stay in the player's memory, and no pak is changed.
            auto* raw = asset->GetPropertyByNameInChain(L"RawData");
            auto* source_raw = original->GetPropertyByNameInChain(L"RawData");
            if (!raw || !source_raw || raw->GetClass().GetName() != L"ArrayProperty")
            {
                return;
            }
            auto* source =
                source_raw->ContainerPtrToValuePtr<RC::Unreal::TArray<uint8_t>>(original);
            if (!source || source->Num() != ingredient_journal_movie::patched_size ||
                shop_movie_fingerprint({source->GetData(), static_cast<size_t>(source->Num())}) !=
                    ingredient_journal_movie::patched_hash)
            {
                return;
            }
            raw->ContainerPtrToValuePtr<RC::Unreal::TArray<uint8_t>>(asset)->Append(
                source->GetData(), source->Num());
            for (const auto* key : {L"References", L"UserReferences"})
            {
                auto* property = original->GetPropertyByNameInChain(key);
                auto* target = asset->GetPropertyByNameInChain(key);
                if (!property || !target || property->GetClass().GetName() != L"ArrayProperty" ||
                    static_cast<FArrayProperty*>(property)->GetInner()->GetClass().GetName() !=
                        L"ObjectProperty")
                {
                    return;
                }
                auto* values =
                    property->ContainerPtrToValuePtr<RC::Unreal::TArray<UObject*>>(original);
                if (values->Num() < 0 || values->Num() > max_journal_movie_values)
                {
                    return;
                }
                if (values->Num())
                {
                    target->ContainerPtrToValuePtr<RC::Unreal::TArray<UObject*>>(asset)->Append(
                        values->GetData(), values->Num());
                }
            }
            if (auto* references = asset->GetPropertyByNameInChain(L"UserReferences"))
            {
                references->ContainerPtrToValuePtr<RC::Unreal::TArray<UObject*>>(asset)->Append(
                    food.data(), static_cast<int32_t>(food.size()));
            }
            auto* names = original->GetPropertyByNameInChain(L"ReferencedAssetNames");
            auto* target_names = asset->GetPropertyByNameInChain(L"ReferencedAssetNames");
            if (!names || !target_names || names->GetClass().GetName() != L"ArrayProperty" ||
                static_cast<FArrayProperty*>(names)->GetInner()->GetClass().GetName() !=
                    L"StrProperty")
            {
                return;
            }
            auto* imports = names->ContainerPtrToValuePtr<RC::Unreal::TArray<FString>>(original);
            if (imports->Num() < 0 || imports->Num() > max_journal_movie_imports)
            {
                return;
            }
            if (imports->Num())
            {
                target_names->ContainerPtrToValuePtr<RC::Unreal::TArray<FString>>(asset)->Append(
                    imports->GetData(), imports->Num());
            }
            write_bool_function_param(asset->GetPropertyByNameInChain(L"bUseGFxExport"), asset,
                                      true);
            asset->SetRootSet();
            overlay_asset = make_uobject_weak(asset);
            auto* owner = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, L"/Script/ScaleformUI.Default__GFxMoviePlayer");
            auto* fn = is_uobject_valid_fast(owner) ? owner->GetFunctionByNameInChain(L"OpenMovie")
                                                    : nullptr;
            if (!is_uobject_valid_fast(fn))
            {
                return;
            }
            FStaticConstructObjectParameters player_construction(owner->GetClassPrivate(),
                                                                 asset->GetOuterPrivate());
            player_construction.SetFlags = RF_Transient;
            auto* configured = UObjectGlobals::StaticConstructObject(player_construction);
            if (!is_uobject_valid_fast(configured) ||
                !write_integral_function_param(configured->GetPropertyByNameInChain(L"Priority"),
                                               configured, ingredient_overlay_movie_priority))
            {
                return;
            }
            std::vector<std::byte> params(fn->GetParmsSize());
            if (!write_object_function_param(fn, params.data(), L"SwfMovie", asset) ||
                !write_object_function_param(fn, params.data(), L"MoviePlayer", configured))
            {
                return;
            }
            write_bool_function_param(find_function_param_by_name(fn, L"DisplayWithHudOff"),
                                      params.data(), true);
            owner->ProcessEvent(fn, params.data());
            auto* player = try_read_function_object_param(fn, params.data(), L"ReturnValue");
            auto* value = is_uobject_valid_fast(player)
                              ? try_read_reflected_object_property_value(player, L"Root")
                              : nullptr;
            if (!is_uobject_valid_fast(value) ||
                !try_gfx_set_member_value_bool(config.gfx_paths, value, L"visible", false))
            {
                return;
            }
            for (const auto* key : {L"mobilePortal", L"appBg", L"btnInfo"})
            {
                if (auto* child = try_gfx_get_member_value_object(config.gfx_paths, value, key);
                    is_uobject_valid_fast(child))
                {
                    try_gfx_set_member_value_bool(config.gfx_paths, child, L"visible", false);
                }
            }
            for (const auto* key :
                 {L"bAllowInput", L"bAllowFocus", L"bCaptureKeyboardInput", L"bCaptureMouseInput",
                  L"bCaptureGamepadInput", L"bCaptureTouchInput"})
            {
                write_bool_function_param(player->GetPropertyByNameInChain(key), player, false);
            }
            // This movie retains only UI assets and textures, with no external
            // interface or gameplay object. Hide it on focus loss and reuse it.
            write_bool_function_param(player->GetPropertyByNameInChain(L"bCloseOnLevelChange"),
                                      player, false);
            overlay_movie = make_uobject_weak(player);
            Output::send<LogLevel::Normal>(
                L"[KH3APProbeCpp] Ingredient journal transient movie opened asset={} "
                L"helper_present={}\n",
                asset->GetPathName(),
                is_uobject_valid_fast(try_gfx_invoke_string_arg_object(
                    config.gfx_paths, value, L"getChildByName", L"kh3apIngredients")));
        }
        auto overlay_input(bool enabled) -> void
        {
            if (overlay_input_enabled == enabled)
            {
                return;
            }
            auto* movie = overlay_movie.Get();
            if (!is_uobject_valid_fast(movie))
            {
                return;
            }
            write_bool_function_param(movie->GetPropertyByNameInChain(L"bCaptureMouseInput"), movie,
                                      enabled);
            auto* fn = movie->GetFunctionByNameInChain(L"SetMovieCanReceiveInput");
            if (!is_uobject_valid_fast(fn) || fn->GetParmsSize() != 1)
            {
                return;
            }
            uint8_t params = enabled ? 1 : 0;
            movie->ProcessEvent(fn, &params);
            overlay_input_enabled = enabled;
        }
        auto native_cursor() -> std::pair<UObject*, UObject*>
        {
            auto* page = dictionary_context(app_context.Get()) ? app_context.Get() : context.Get();
            auto* manager = try_read_reflected_object_property_value(page, L"m_pUIManager");
            auto* owner = try_read_reflected_object_property_value(manager, L"m_Cursor");
            auto* value =
                as_object(try_read_reflected_object_property_value(owner, L"m_ASProxyUIComponent"));
            return {owner, value};
        }
        auto suppress_native_cursor(bool suppress) -> void
        {
            if (!suppress)
            {
                if (cursor_alpha && is_uobject_valid_fast(cursor_owner.Get()) &&
                    is_uobject_valid_fast(cursor_root.Get()))
                {
                    try_gfx_set_member_value_number(config.gfx_paths, cursor_root.Get(), L"alpha",
                                                    *cursor_alpha);
                }
                cursor_owner = nullptr;
                cursor_root = nullptr;
                cursor_alpha.reset();
                return;
            }
            const auto [owner, value] = native_cursor();
            if (!is_uobject_valid_fast(owner) || !is_uobject_valid_fast(value))
            {
                return;
            }
            if (cursor_owner.Get() != owner)
            {
                suppress_native_cursor(false);
                cursor_alpha = try_gfx_get_member_value_number(config.gfx_paths, value, L"alpha");
                if (!cursor_alpha)
                {
                    return;
                }
                cursor_owner = make_uobject_weak(owner);
                if (cursor_owner.Get() != owner)
                {
                    cursor_alpha.reset();
                    return;
                }
            }
            // Hide the cursor's parent, rather than its animated light child.
            // Stock anchor changes can restart the child without exposing it.
            // Preserve the opacity across replacement wrappers for this AS root.
            cursor_root = make_uobject_weak(value);
            if (cursor_root.Get() != value)
            {
                return;
            }
            if (try_gfx_get_member_value_number(config.gfx_paths, value, L"alpha") != 0)
            {
                try_gfx_set_member_value_number(config.gfx_paths, value, L"alpha", 0);
            }
        }
        auto tick() -> void
        {
            const auto now = std::chrono::steady_clock::now();
            const auto ingredient_generation =
                config.collected_revision ? config.collected_revision() : 0;
            const auto enemy_generation = read_adversary_revision();
            const auto chest_generation = native_chest_journal_revision();
            const auto current_kh02_access = RuntimePortalService::kh02_access();
            if (current_kh02_access != kh02_access)
            {
                kh02_access = current_kh02_access;
                if (app_chests)
                {
                    page_work.request();
                }
            }
            const auto epoch = reconciliation::epoch();
            const auto current_gate_view = reconciliation::view();
            if (gate_view != current_gate_view)
            {
                gate_view = current_gate_view;
                if (app_gates)
                {
                    page_work.request();
                }
            }
            if (ingredient_generation != collected_revision ||
                enemy_generation != adversary_revision || chest_generation != chest_revision ||
                epoch != observed_epoch)
            {
                collected_revision = ingredient_generation;
                adversary_revision = enemy_generation;
                chest_revision = chest_generation;
                observed_epoch = epoch;
                page_work.request();
            }
            if (preload_work.take(now))
            {
                preload(now);
                preload_work.finish(now, movie_complete, ingredient_journal_readiness_retry);
                if (movie_complete)
                {
                    page_work.request();
                }
            }
            if (page_work.take(now))
            {
                RuntimeConditionalProfileScope<(KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS != 0)> profile{
                    L"journal:event_work"};
                page_work.finish(now, update_page(), ingredient_journal_readiness_retry);
            }
        }
        auto update_page() -> bool
        {
            // Focus/anchor/input events can replace the wrapper for an AS root.
            if (auto* owner = context.Get(); is_uobject_valid_fast(owner))
            {
                // KH3 can replace the UObject wrapper for the same AS root while
                // finishing focus. Refresh that wrapper without dropping focus.
                auto* current = as_object(
                    try_read_reflected_object_property_value(owner, L"m_ASProxyUIComponent"));
                if (is_uobject_valid_fast(current) && current != root.Get())
                {
                    root = make_uobject_weak(current);
                    home_button = nullptr;
                    home_icon = nullptr;
                    home_mark = nullptr;
                    home_help = nullptr;
                }
            }
            if (movie_ready && portal_context(context.Get()))
            {
                publish_home_tile();
            }
            if (!dictionary_context(app_context.Get()))
            {
                suppress_native_cursor(false);
                maps.cancel();
                if (!overlay_visible)
                {
                    return !portal_context(context.Get()) ||
                           (movie_ready && is_uobject_valid_fast(home_button.Get()));
                }
                if (auto* page = helper(); is_uobject_valid_fast(page))
                {
                    try_gfx_invoke_int_arg(config.gfx_paths, page, L"setFocus", 0);
                }
                if (auto* movie = overlay_movie.Get(); is_uobject_valid_fast(movie))
                {
                    if (auto* value = try_read_reflected_object_property_value(movie, L"Root");
                        is_uobject_valid_fast(value))
                    {
                        try_gfx_set_member_value_bool(config.gfx_paths, value, L"visible", false);
                    }
                }
                overlay_input(false);
                overlay_visible = false;
                return true;
            }
            publish_native_title();
            make_overlay();
            auto* page = helper();
            if (!is_uobject_valid_fast(page))
            {
                if (movie_ready && !missing_logged)
                {
                    missing_logged = true;
                    auto* value = root.Get();
                    auto* failure = try_gfx_invoke_string_arg_object(
                        config.gfx_paths, value, L"getChildByName", L"kh3apIngredientsError");
                    Output::send<LogLevel::Warning>(
                        L"[KH3APProbeCpp] Ingredient journal helper unavailable on Gummiphone home "
                        L"root={} constructor_error={}\n",
                        summarize_gfx_member_values(config.gfx_paths, L"home", value),
                        is_uobject_valid_fast(failure)
                            ? try_gfx_get_member_value(config.gfx_paths, failure, L"failure")
                            : L"<none>");
                }
                return false;
            }
            if (app_gates)
            {
                if (catalog_kind != ingredient_dictionary_catalog_kind)
                {
                    if (!try_gfx_invoke_int_arg(config.gfx_paths, page, L"setJournal",
                                                ingredient_dictionary_catalog_kind))
                    {
                        return false;
                    }
                    catalog_kind = ingredient_dictionary_catalog_kind;
                }
                const int request = static_cast<int>(
                    try_gfx_get_member_value_number(config.gfx_paths, page, L"gateRequest")
                        .value_or(0));
                if (request)
                {
                    try_gfx_set_member_value_int(config.gfx_paths, page, L"gateRequest", 0);
                    if (!RuntimePortalService::use_gate_opener(request))
                    {
                        (void)try_gfx_invoke_no_args_object(config.gfx_paths, page,
                                                            L"gateRejected");
                    }
                }
                const auto payload = RuntimePortalService::gate_opener_menu();
                if (!published || payload != gate_payload)
                {
                    if (!is_uobject_valid_fast(try_gfx_invoke_string_arg_object(
                            config.gfx_paths, page, L"setGateState", payload)))
                    {
                        return false;
                    }
                    gate_payload = payload;
                }
            }
            std::optional<ChestJournalPage> chest_page;
            if (app_chests)
            {
                if (const auto snapshot = read_native_chest_journal())
                {
                    chest_page = chest_journal_page(*snapshot, kh02_access);
                    if (chest_catalog != chest_page->catalog)
                    {
                        published = false;
                        catalog_kind = -1;
                    }
                }
            }
            auto collected = app_chests ? (chest_page ? std::optional{chest_page->collected} : std::nullopt)
                             : app_gates         ? std::optional<std::wstring>{L""}
                             : app_adversaries ? adversary_collected()
                                               : config.collected();
            if (!collected)
            {
                suppress_native_cursor(false);
                (void)try_gfx_invoke_int_arg(config.gfx_paths, page, L"setFocus", 0);
                return false;
            }
            if (!published && !app_gates)
            {
                const auto kind = app_chests ? chest_catalog_kind : app_adversaries ? 1 : 0;
                if (catalog_kind != kind)
                {
                    if (!try_gfx_invoke_int_arg(config.gfx_paths, page, L"setJournal",
                                                kind))
                    {
                        return false;
                    }
                    catalog_kind = kind;
                }
                auto* result = try_gfx_invoke_string_arg_object(
                    config.gfx_paths, page, L"loadCatalog",
                    app_chests ? std::wstring_view(chest_page->catalog)
                               : app_adversaries ? adversary_journal_catalog : ingredient_journal_catalog);
                if (!is_uobject_valid_fast(result) ||
                    try_gfx_get_member_value_number(config.gfx_paths, page, L"catalogCount") !=
                        (app_chests ? chest_page->count : app_adversaries ? adversary_catalog_count : ingredient_catalog_count))
                {
                    return false;
                }
            }
            if (app_chests && chest_page)
            {
                chest_catalog = chest_page->catalog;
            }
            if (!app_gates)
            {
                maps.tick(page);
            }
            if (!app_gates && (!published || *collected != last_collected))
            {
                auto* result =
                    collected->empty()
                        ? try_gfx_invoke_no_args_object(config.gfx_paths, page, L"clearCollected")
                        : try_gfx_invoke_string_arg_object(config.gfx_paths, page, L"setCollected",
                                                           *collected);
                if (!is_uobject_valid_fast(result))
                {
                    return false;
                }
                last_collected = *collected;
            }
            if (!published)
            {
                cursor_sound_serial =
                    try_gfx_get_member_value_number(config.gfx_paths, page, L"cursorSoundSerial")
                        .value_or(0);
                (void)try_gfx_invoke_int_arg(config.gfx_paths, page, L"setFocus", 1);
                if (auto* movie = overlay_movie.Get(); is_uobject_valid_fast(movie))
                {
                    if (auto* value = try_read_reflected_object_property_value(movie, L"Root");
                        is_uobject_valid_fast(value))
                    {
                        try_gfx_set_member_value_bool(config.gfx_paths, value, L"visible", true);
                    }
                }
                overlay_input(true);
                overlay_visible = true;
                published = true;
                Output::send<LogLevel::Normal>(
                    L"[KH3APProbeCpp] Journal native page ready adversaries={} entry_visible={}\n",
                    app_adversaries,
                    try_gfx_get_member_value_number(config.gfx_paths, page, L"entryVisible")
                        .value_or(-1));
            }
            // Input/focus events already schedule this work, including mouse hover.
            // The movie increments only for actual cursor changes, never repaint or
            // asynchronous map/icon completion. Both journals share this route.
            const auto serial =
                try_gfx_get_member_value_number(config.gfx_paths, page, L"cursorSoundSerial")
                    .value_or(cursor_sound_serial);
            if (serial != cursor_sound_serial)
            {
                cursor_sound_serial = serial;
                (void)ui_sound.play(app_context.Get(), RuntimeUiSound::cursor);
            }
            suppress_native_cursor(overlay_visible);
            return !maps.pending() && is_uobject_valid_fast(cursor_root.Get());
        }
        auto route(int controller, std::wstring_view key, int event, bool gamepad) -> bool
        {
            // Include mouse press/release: Flash may handle the click itself.
            // Queue the native refresh without consuming unfamiliar input.
            if (overlay_visible)
            {
                page_work.request();
            }
            return input.route(
                controller, std::wstring{key}, event,
                [&]()
                {
                    if (event != 0 || !published || !dictionary_context(app_context.Get()) ||
                        (gamepad && !config.background_input_allowed() && !foreground()))
                    {
                        return false;
                    }
                    auto* page = helper();
                    if (!is_uobject_valid_fast(page))
                    {
                        return false;
                    }
                    const auto mode =
                        try_gfx_get_member_value_number(config.gfx_paths, page, L"mode");
                    int command = -1;
                    if (mode != 1)
                    {
                        return false;
                    }
                    const auto face_button = runtime_menu_face_button(key);
                    if (key == L"Left" || key == L"A" || key == L"Gamepad_DPad_Left" ||
                        key == L"Gamepad_LeftStick_Left")
                    {
                        command = navigate_left;
                    }
                    else if (key == L"Right" || key == L"D" || key == L"Gamepad_DPad_Right" ||
                             key == L"Gamepad_LeftStick_Right")
                    {
                        command = navigate_right;
                    }
                    else if (key == L"Up" || key == L"W" || key == L"Gamepad_DPad_Up" ||
                             key == L"Gamepad_LeftStick_Up")
                    {
                        command = navigate_up;
                    }
                    else if (key == L"Down" || key == L"S" || key == L"Gamepad_DPad_Down" ||
                             key == L"Gamepad_LeftStick_Down")
                    {
                        command = navigate_down;
                    }
                    else if (key == L"Q" || key == L"Gamepad_LeftShoulder")
                    {
                        command = navigate_previous_category;
                    }
                    else if (key == L"E" || key == L"Gamepad_RightShoulder")
                    {
                        command = navigate_next_category;
                    }
                    else if (key == L"Escape" || key == L"BackSpace" || key == L"SpaceBar" ||
                             face_button == RuntimeMenuFaceButton::back)
                    {
                        // Back returns through map -> sources -> grid before handing
                        // the grid's Back to the normal native app controller.
                        if (try_gfx_get_member_value_number(config.gfx_paths, page, L"paneMode")
                                .value_or(0) == 0)
                        {
                            return false;
                        }
                        command = navigate_back;
                    }
                    else if (key == L"PageUp")
                    {
                        command = navigate_previous_page;
                    }
                    else if (key == L"PageDown")
                    {
                        command = navigate_next_page;
                    }
                    else if (key == L"Enter" || face_button == RuntimeMenuFaceButton::confirm)
                    {
                        command = navigate_confirm;
                    }
                    if (command < 0)
                    {
                        return false;
                    }
                    const bool handled =
                        try_gfx_invoke_int_arg(config.gfx_paths, page, L"navigate", command);
                    if (handled)
                    {
                        page_work.request();
                    }
                    return handled;
                });
        }
    };
    RuntimeIngredientJournalService::RuntimeIngredientJournalService(
        RuntimeIngredientJournalConfig value)
        : impl_(std::make_unique<Impl>(std::move(value)))
    {
    }
    RuntimeIngredientJournalService::~RuntimeIngredientJournalService() = default;
    auto RuntimeIngredientJournalService::pointer_motion() -> void
    {
        if (impl_->overlay_visible)
        {
            impl_->page_work.request();
        }
    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto RuntimeIngredientJournalService::diagnostic_json(int navigation) -> std::string
    {
        auto* page = impl_->helper();
        if (navigation >= 0 && navigation <= last_journal_navigation_command &&
            navigation != focus_ingredients_navigation_command && impl_->published &&
            is_uobject_valid_fast(page))
        {
            try_gfx_invoke_int_arg(impl_->config.gfx_paths, page, L"navigate", navigation);
            impl_->page_work.request();
        }
        if ((navigation == focus_ingredients_navigation_command ||
             navigation == focus_adversaries_navigation_command ||
             navigation == focus_gates_navigation_command ||
             navigation == focus_chests_navigation_command) &&
            is_uobject_valid_fast(impl_->home_tile()))
        {
            try_gfx_invoke_int_arg(impl_->config.gfx_paths, impl_->root.Get(), L"focusAnchor",
                                   navigation == focus_chests_navigation_command ? chests_home_anchor
                                   : navigation == focus_gates_navigation_command ? gates_home_anchor
                                   : navigation == focus_adversaries_navigation_command
                                       ? adversaries_home_anchor
                                       : ingredients_home_anchor);
        }
        std::ostringstream result;
        result << "{\"focused\":"
               << (portal_context(impl_->context.Get()) ||
                           dictionary_context(impl_->app_context.Get())
                       ? "true"
                       : "false")
               << ",\"home_focused\":" << (portal_context(impl_->context.Get()) ? "true" : "false")
               << ",\"app_focused\":"
               << (dictionary_context(impl_->app_context.Get()) ? "true" : "false")
               << ",\"movie_ready\":" << (impl_->movie_ready ? "true" : "false")
               << ",\"published\":" << (impl_->published ? "true" : "false")
               << ",\"adversaries\":" << (impl_->app_adversaries ? "true" : "false")
               << ",\"chests\":" << (impl_->app_chests ? "true" : "false")
               << ",\"gates\":" << (impl_->app_gates ? "true" : "false") << ",\"gate_payload\":\""
               << json_escape(impl_->gate_payload) << "\""
               << ",\"helper_present\":" << (is_uobject_valid_fast(page) ? "true" : "false")
               << ",\"map_service\":\"" << json_escape(impl_->maps.status()) << "\""
               << ",\"native_cursor_suppressed\":" << (impl_->cursor_alpha ? "true" : "false")
               << ",\"native_cursor_alpha\":"
               << try_gfx_get_member_value_number(impl_->config.gfx_paths,
                                                  impl_->native_cursor().second, L"alpha")
                      .value_or(-1)
               << ",\"members\":{";
        bool comma = false;
        for (const auto* name :
             {L"gateYes", L"gateCount", L"gateRequest", L"mode", L"catalogCount", L"entryVisible",
              L"selectedIngredient", L"selectedWorld", L"detailText", L"detailLineCount",
              L"loadedIconCount", L"selectedIconAlpha", L"paneMode", L"selectedLocation",
              L"mapRequestKey", L"mapState", L"mapTexture", L"mapMarkerCount", L"mapPageCount"})
        {
            if (comma)
            {
                result << ',';
            }
            comma = true;
            result << '"' << json_escape(name) << "\":\""
                   << json_escape(
                          is_uobject_valid_fast(page)
                              ? try_gfx_get_member_value(impl_->config.gfx_paths, page, name)
                              : L"<absent>")
                   << '"';
        }
        result << "},\"home_tile\":{";
        comma = false;
        auto* tile = impl_->home_tile();
        for (const auto* name :
             {L"label", L"index", L"enabled", L"selected", L"toLeft", L"toRight", L"x", L"y"})
        {
            if (comma)
            {
                result << ',';
            }
            comma = true;
            result << '"' << json_escape(name) << "\":\""
                   << json_escape(
                          is_uobject_valid_fast(tile)
                              ? try_gfx_get_member_value(impl_->config.gfx_paths, tile, name)
                              : L"<absent>")
                   << '"';
        }
        result << ",\"icon\":\""
               << json_escape(try_gfx_get_member_value(impl_->config.gfx_paths,
                                                       impl_->home_icon.Get(), L"index"))
               << "\",\"description\":\""
               << json_escape(try_gfx_get_member_value(impl_->config.gfx_paths,
                                                       impl_->home_help.Get(), L"text"))
               << "\"},\"native_title\":\""
               << json_escape(try_gfx_get_member_value(impl_->config.gfx_paths,
                                                       impl_->app_title.Get(), L"text"))
               << "\",\"native_app\":\""
               << json_escape(dictionary_context(impl_->app_context.Get())
                                  ? impl_->app_context.Get()->GetFullName()
                                  : L"<absent>")
               << "\",\"root\":\""
               << json_escape(summarize_gfx_member_values(impl_->config.gfx_paths, L"home",
                                                          impl_->root.Get()))
               << "\",\"children\":[";
        const auto count = try_gfx_get_member_value_number(impl_->config.gfx_paths,
                                                           impl_->root.Get(), L"numChildren")
                               .value_or(0);
        for (int i = 0; i < count && i < max_journal_diagnostic_children; ++i)
        {
            if (i)
            {
                result << ',';
            }
            result << '"'
                   << json_escape(summarize_gfx_member_values(
                          impl_->config.gfx_paths, L"child",
                          try_gfx_invoke_get_child_at(impl_->config.gfx_paths, impl_->root.Get(),
                                                      i)))
                   << '"';
        }
        result << "]}";
        return result.str();
    }
#endif
    auto RuntimeIngredientJournalService::install_once() -> void
    {
        impl_->install_once();
    }
    auto RuntimeIngredientJournalService::tick() -> void
    {
        impl_->tick();
    }
    auto RuntimeIngredientJournalService::route(int controller, std::wstring_view key, int event,
                                                bool gamepad) -> bool
    {
        return impl_->route(controller, key, event, gamepad);
    }
} // namespace kh3ap
