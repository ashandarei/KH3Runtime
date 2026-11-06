#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/ui/map_overlay/minimap_context_service.h"
#include "kh3ap/core/platform/steam_parity_profile.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/FProperty.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <sstream>
#include <array>
#include <cstring>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/NameTypes.hpp>
#include <UnrealDef.hpp>
#define NOMINMAX
#include <windows.h>

using namespace RC::Unreal;
namespace kh3ap {
namespace {
        constexpr auto committed_minimap_name_offset = 0x1f8;
        constexpr auto max_minimap_ui_parts = 128;

auto object(UObject* owner, const wchar_t* field) -> UObject* {
    if (!is_uobject_valid_fast(owner)) return nullptr;
    auto* value = try_read_reflected_object_property_value(owner, field);
    return is_uobject_valid_fast(value) ? value : nullptr;
}
}

struct RuntimeMinimapContextService::Impl {
    RuntimeMinimapContextConfig config;
    using SelectMap = void (*)(UObject*, const FName*, bool);
    inline static Impl* owner{};
    std::unique_ptr<PLH::x64Detour> detour;
    uint64_t trampoline{};
    FName pending_nav_name;
    std::wstring pending_row, last_row, last_texture_path;
    bool pending{};

    // EGS CL1065258: the native NaviMapGra selector called for both resident
    // textures and async completion (the latter calls it with load=false).
    // 0x1f8 is its committed FName; 0x2a0 is the pending async FName. Only
    // observe the committed row after the original returns, never its request.
    static auto selected_row(UObject* nav) -> FName {
        FName value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(nav) + committed_minimap_name_offset, sizeof(value));
        return value;
    }
    static auto on_select(UObject* nav, const FName* requested, bool load) -> void {
        auto* self = owner;
        if (!self || !self->trampoline) return;
        const bool valid = is_uobject_valid_fast(nav);
        const auto before = valid ? selected_row(nav) : FName{};
        reinterpret_cast<SelectMap>(self->trampoline)(nav, requested, load);
        try {
            if (!valid || !is_uobject_valid_fast(nav)) return;
            const auto after = selected_row(nav);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            if (!(before == after)) RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Minimap selected old={} new={} loaded={}\n"), before.ToString(), after.ToString(), object(nav, L"m_LoadedAsyncTexture") != nullptr);
#endif
            if (before == after || !object(nav, L"m_LoadedAsyncTexture")) return;
            const RuntimeHotPathProfileScope profile{L"minimap_context:event"};
            // KH3 UI parts can have no weak serial. Retain their identity only;
            // resolve through the owning UI manager when draining this event.
            self->pending_nav_name = nav->GetNamePrivate();
            self->pending_row = after.ToString();
            self->pending = true;
        } catch (...) {
            RC::Output::send<RC::LogLevel::Error>(STR("[KH3APProbeCpp] Minimap change capture failed\n"));
        }
    }
    auto install() -> void {
        if (detour) return;
        const auto* base = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto* profile=steam_parity_native_profile(nt->FileHeader.TimeDateStamp,nt->OptionalHeader.SizeOfImage);
        if (nt->Signature != IMAGE_NT_SIGNATURE || !profile ||
            std::memcmp(base+profile->minimap_select.rva,profile->minimap_select.bytes.data(),profile->minimap_select.bytes.size()) ||
            std::memcmp(base+profile->minimap_compare.rva,profile->minimap_compare.bytes.data(),profile->minimap_compare.bytes.size())) {
            RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Minimap event hook unavailable: unsupported image; level tracking remains active\n"));
            return;
        }
        DWORD64 image_base{};
        const auto* function = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(base + profile->minimap_select.rva), &image_base, nullptr);
        if (!function || image_base != reinterpret_cast<DWORD64>(base) ||
            function->BeginAddress != profile->minimap_select.rva || function->EndAddress != profile->minimap_end) return;
        owner = this;
        detour = std::make_unique<PLH::x64Detour>(reinterpret_cast<uint64_t>(base + profile->minimap_select.rva),
            reinterpret_cast<uint64_t>(&on_select), &trampoline);
        if (!detour->hook() || !trampoline) {
            detour.reset(); owner = nullptr;
            RC::Output::send<RC::LogLevel::Error>(STR("[KH3APProbeCpp] Minimap event hook install failed\n"));
            return;
        }
        RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Minimap event hook installed storefront={} selector=0x{:X}; no minimap polling\n"),profile->image_size==kh3ap::native_build::steam.image_size?STR("steam"):STR("egs"),profile->minimap_select.rva);
    }
    auto uninstall() -> void {
        if (detour) { detour->unHook(); detour.reset(); }
        if (owner == this) owner = nullptr;
        trampoline = 0;
        pending = false;
    }
    ~Impl() { uninstall(); }
    FWeakObjectPtr last_pawn;
    std::filesystem::path last_bridge;
    uint64_t sequence{};

    explicit Impl(RuntimeMinimapContextConfig value) : config(std::move(value)) {}
    auto flush_pending() -> void {
        // The existing game-thread drain only checks this flag when idle. No
        // timer, UI discovery or texture reads occur without a selection event.
        if (!pending) return;
        // Menus/loading can temporarily remove the HUD. They are not new areas.
        if (!config.gameplay_ready || !config.gameplay_ready()) return;
        auto* pawn = config.pawn ? config.pawn() : nullptr;
        if (!is_uobject_valid_fast(pawn)) return;
        const RuntimeHotPathProfileScope profile{L"minimap_context:observe"};
        const auto bridge = config.bridge_dir ? config.bridge_dir() : std::nullopt;
        if (!bridge) return;
        auto* level = pawn->GetOuterPrivate();
        auto* world = is_uobject_valid_fast(level) ? level->GetOuterPrivate() : nullptr;
        if (!is_uobject_valid_fast(world)) return;
        auto* manager = object(object(world, L"OwningGameInstance"), L"m_UIManager");
        if (!manager) return;
        // Only the UI manager's bounded, engine-owned list is visited, once per
        // event. StaticFindObject can scan the global UObject array in UE4SS.
        auto* property = manager->GetPropertyByNameInChain(L"m_UIPartsAry");
        if (!property || property->GetClass().GetName() != L"ArrayProperty") { pending = false; return; }
        struct Array { UObject** data; int32_t count, capacity; };
        const auto* parts = property->ContainerPtrToValuePtr<Array>(manager);
        if (!parts || !parts->data || parts->count < 0 || parts->count > max_minimap_ui_parts || parts->capacity < parts->count) { pending = false; return; }
        UObject* nav = nullptr;
        for (int i = 0; i < parts->count; ++i) {
            auto* part = parts->data[i];
            if (is_uobject_valid_fast(part) && part->GetNamePrivate() == pending_nav_name &&
                part->GetClassPrivate()->GetName() == L"TresUIP_NaviMapGra") { nav = part; break; }
        }
        if (!is_uobject_valid_fast(nav)) { pending = false; return; }
        if (selected_row(nav).ToString() != pending_row) { pending = false; return; }
        auto* texture = object(nav, L"m_LoadedAsyncTexture");
        if (!texture) { pending = false; return; }
        const auto texture_path = texture->GetPathName();
        if (last_pawn.Get() == pawn && last_texture_path == texture_path && last_bridge == *bridge && last_row == pending_row) { pending = false; return; }
        if (object(nav, L"m_pUIManager") != manager) {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Minimap pending rejected: UI manager changed\n"));
#endif
            pending = false;
            return;
        }
        std::ostringstream json;
        json << "{\"schema\":\"kh3ap.runtime_minimap_context.v1\",\"process_id\":" << GetCurrentProcessId()
             << ",\"sequence\":" << sequence + 1
             << ",\"world\":\"" << json_escape(world->GetPathName())
             << "\",\"pawn\":\"" << json_escape(pawn->GetFullName())
             << "\",\"minimap_id\":\"" << json_escape(pending_row)
             << "\",\"texture_id\":\"" << json_escape(texture->GetName())
             << "\",\"texture_path\":\"" << json_escape(texture->GetPathName())
             << "\",\"observed_at_utc\":\"" << now_iso8601_utc() << "\"}";
        if (write_text_file_atomic_verified(*bridge / channels::runtime_minimap_context_file, json.str())) {
            last_pawn = pawn;
            last_texture_path = texture_path;
            last_bridge = *bridge;
            last_row = pending_row;
            pending = false;
            ++sequence;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Minimap event published row={} texture={} sequence={}\n"), pending_row, texture->GetName(), sequence);
#endif
        }
    }
};
RuntimeMinimapContextService::RuntimeMinimapContextService(RuntimeMinimapContextConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
RuntimeMinimapContextService::~RuntimeMinimapContextService() = default;
auto RuntimeMinimapContextService::install() -> void { impl_->install(); }
auto RuntimeMinimapContextService::uninstall() -> void { impl_->uninstall(); }
auto RuntimeMinimapContextService::flush_pending() -> void { impl_->flush_pending(); }
}
