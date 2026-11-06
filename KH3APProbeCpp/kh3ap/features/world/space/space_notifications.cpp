#include "kh3ap/features/world/space/space_notifications.h"
#include "kh3ap/features/world/space/space_profile.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Windows.h>
#include <array>
#include <cstring>
#include <deque>
#include <string>

namespace kh3ap {
    using space::native_profile;
    using namespace RC;
    using namespace RC::Unreal;
    namespace {
        UObject* object_field(UObject* object, const wchar_t* name) {
            if (!is_uobject_valid_fast(object)) return nullptr;
            auto* p = object->GetPropertyByNameInChain(name);
            if (!p || p->GetElementSize() != sizeof(UObject*) || p->GetClass().GetName() != L"ObjectProperty") return nullptr;
            auto* value = *p->ContainerPtrToValuePtr<UObject*>(object);
            return is_uobject_valid_fast(value) ? value : nullptr;
        }
        bool supported(uintptr_t base) {
            // These native UI routines are EGS CL1065258 only. The caller also
            // requires the space service's full image/build signature check.
            if (!native_profile().supported) return false;
            for (const auto& s : native_profile().notification_signatures)
                if (std::memcmp(reinterpret_cast<void*>(base+s.rva),s.bytes.data(),s.bytes.size())) return false;
            return true;
        }
    }
    struct RuntimeSpaceNotifications::Impl {
        // UE4SS only copies an existing weak serial; freshly constructed panels
        // have serial zero. Root this one owned object and validate every use.
        // The root is released when its manager changes or the service exits.
        UObject* panel{};
        FWeakObjectPtr manager;
        std::deque<std::wstring> queue;
        bool loaded{};
        bool positioned{};
        bool positioned_overview{};
        float root_x{}, root_y{};
        unsigned long long shown{};
        std::wstring blocked;
        void waiting(const wchar_t* reason) {
            if (blocked == reason) return;
            blocked = reason;
            Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Space item banner waiting={}\n"),blocked);
        }
        uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        bool verified = supported(base);

        void close() {
            auto* owner = panel;
            if (loaded && verified && is_uobject_valid_fast(owner))
                reinterpret_cast<void(*)(UObject*)>(base+native_profile().hud_close_rva)(owner);
            loaded = false;
            positioned = false;
        }
        ~Impl() { if (is_uobject_valid_fast(panel)) panel->ClearRootSet(); }
        UObject* prepare(UObject* ui) {
            if (!verified || !is_uobject_valid_fast(ui)) return nullptr;
            auto* owner = panel;
            if (manager.Get() != ui || !is_uobject_valid_fast(owner)) {
                close();
                if (is_uobject_valid_fast(owner)) owner->ClearRootSet();
                panel = nullptr;
                auto* klass = UObjectGlobals::StaticFindObject<UClass*>(nullptr,nullptr,L"/Script/TresGame.TresUIP_HudLeft");
                if (!is_uobject_valid_fast(klass)) return nullptr;
                FStaticConstructObjectParameters params(klass,klass->GetOuterPrivate());
                params.SetFlags = RF_Transient;
                owner = UObjectGlobals::StaticConstructObject(params);
                if (!is_uobject_valid_fast(owner)) return nullptr;
                owner->SetRootSet();
                // The native manager owns/updates this registered panel, as it
                // does the normal cockpit's panel. The separate root makes
                // this new object's lifetime explicit without a weak serial.
                reinterpret_cast<int(*)(UObject*,UObject*)>(base+native_profile().ui_register_panel_rva)(ui,owner);
                panel = owner; manager = ui;
                // Above the world-map movie, below modal dialogs/loading.
                reinterpret_cast<uint8_t*>(owner)[native_profile().panel_layer] = 150;
            }
            if (!loaded) {
                auto* movie = UObjectGlobals::StaticFindObject<UObject*>(nullptr,nullptr,
                    L"/Game/UI/Scaleform/01_hud/hud_left/HudLeft.HudLeft");
                auto* movie_property = owner->GetPropertyByNameInChain(L"m_SwfMovie");
                if (!is_uobject_valid_fast(movie) || !movie_property || movie_property->GetOffset_Internal()!=native_profile().panel_movie ||
                    movie_property->GetElementSize()!=sizeof(UObject*)) return nullptr;
                *movie_property->ContainerPtrToValuePtr<UObject*>(owner) = movie;
                reinterpret_cast<void(*)(UObject*)>(base+native_profile().hud_load_rva)(owner);
                loaded = true;
            }
            return object_field(owner,L"m_GetItem") ? owner : nullptr;
        }
        void position(UObject* owner, bool overview) {
            if (positioned && positioned_overview == overview) return;
            auto* root = object_field(object_field(owner,L"m_pMoviePlayer"),L"Root");
            if (!root) return;
            auto* get = root->GetFunctionByNameInChain(L"GetPosition");
            auto* set = root->GetFunctionByNameInChain(L"SetPosition");
            if (!is_uobject_valid_fast(get) || !is_uobject_valid_fast(set) ||
                get->GetParmsSize()!=9 || set->GetParmsSize()!=8) return;
            if (!positioned) {
                struct { float x{},y{}; uint8_t valid{}; uint8_t padding[3]{}; } p;
                root->ProcessEvent(get,&p);
                if (!p.valid) return;
                root_x=p.x; root_y=p.y; positioned=true;
            }
            // The overview's Current Location box occupies the ordinary HUD
            // banner position. Offset this private movie in design coordinates.
            std::array<float,2> p{root_x,root_y+(overview?120.0f:0.0f)};
            root->ProcessEvent(set,p.data());
            positioned_overview = overview;
        }
        void tick(UObject* controller, bool in_space, bool overview) {
            if (!in_space) { close(); return; }
            if (!is_uobject_valid_fast(controller)) return;
            if (loaded && is_uobject_valid_fast(panel)) position(panel,overview);
            if (queue.empty()) return;
            auto* ui = UObjectGlobals::StaticFindObject<UObject*>(nullptr,nullptr,L"/Engine/Transient.TresUIManager_0");
            auto* owner = prepare(ui);
            auto* item_panel = object_field(owner,L"m_GetItem");
            if (!item_panel) { waiting(L"item_panel"); return; }
            position(owner,overview);
            auto* bytes = reinterpret_cast<uint8_t*>(item_panel);
            if (bytes[native_profile().banner_animating]) return; // Native banner animation is still active.
            auto* instance = object_field(ui,L"m_pGameInstance");
            auto* items = object_field(instance,L"m_ItemMan");
            if (!items) { waiting(L"item_catalog"); return; }
            const auto token = queue.front();
            const auto id = reinterpret_cast<int(*)(UObject*,FName)>(base+native_profile().item_catalog_id_rva)(items,FName(token,FNAME_Add));
            if (id <= 0) {
                Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Space item banner has no catalog row token={}\n"),token);
                queue.pop_front(); return;
            }
            // The item panel resolves the localized item name/icon itself. This
            // is presentation only; it never calls an inventory grant function.
            reinterpret_cast<void(*)(UObject*,int,bool)>(base+native_profile().banner_set_item_rva)(item_panel,id,false);
            auto** vtable = *reinterpret_cast<void***>(item_panel);
            reinterpret_cast<void(*)(UObject*,int)>(vtable[native_profile().banner_start_vtable_offset/8])(item_panel,0);
            reinterpret_cast<uint8_t*>(owner)[native_profile().hud_item_visible] = 1;
            if (!bytes[native_profile().banner_animating]) { waiting(L"animation_start"); return; }
            blocked.clear();
            queue.pop_front(); ++shown;
            Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Space item banner shown token={} catalog_id={}\n"),token,id);
        }
    };
    RuntimeSpaceNotifications::RuntimeSpaceNotifications() : impl_(std::make_unique<Impl>()) {}
    RuntimeSpaceNotifications::~RuntimeSpaceNotifications() = default;
    void RuntimeSpaceNotifications::enqueue(std::wstring_view token) { if (!token.empty()) impl_->queue.emplace_back(token); }
    void RuntimeSpaceNotifications::tick(UObject* controller,bool in_space,bool overview) { impl_->tick(controller,in_space,overview); }
    unsigned long long RuntimeSpaceNotifications::shown() const { return impl_->shown; }
    unsigned long long RuntimeSpaceNotifications::pending() const { return impl_->queue.size(); }
}
