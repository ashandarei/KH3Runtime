#include "kh3ap/features/player/player_ids.h"
#include "kh3ap/features/player/characters/character_layout.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/kh02/core/kh02_profile.h"
#include "kh02_mickey_link.h"
#include "kh3ap/features/kh02/core/kh02_context_refresh.h"
#include "kh3ap/features/kh02/core/kh02_data_table.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/core/kh02_localization.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/features/party/party_helpers.h"
#include "kh3ap/core/events/world_context_events.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <nlohmann/json.hpp>
#include <array>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr int light_chain_command = 197;
        constexpr int popping_holy_command = 237;
        constexpr int sora_player_id = player_id_value(PlayerId::Sora);
        constexpr int aqua_player_id = player_id_value(PlayerId::Aqua);
        constexpr auto link_handoff_timeout = std::chrono::seconds(5);
        constexpr float diagnostic_offer_seconds = 30.0f;
        constexpr size_t reflected_parameter_limit_bytes = 64;

        // Audited shared ABI of the copied TresCommandKindData in both host builds.
        // These are ABI offsets, not gameplay tuning values.
        constexpr size_t state_queue_interface_offset = character_native::layout::player_state_interface_offset;
        constexpr size_t can_dispatch_vtable_offset = 0x340;
        constexpr size_t controller_command_info_offset = 0x890;
        constexpr size_t controller_command_cache_offset = 0x8d8;
        constexpr size_t command_row_bytes = 72;
        constexpr size_t command_type_offset = 24;
        constexpr size_t command_category_offset = 25;
        constexpr size_t command_old_kind_offset = 26;
        constexpr size_t command_label_offset = 56;
        constexpr uint8_t reaction_command_type = 4;
        constexpr uint8_t light_chain_category = 18;
        constexpr int maximum_command_rows = 256;
        constexpr int maximum_command_capacity = 1024;
        using Dispatch = bool (*)(void*, int, UObject*);
        using FriendEligibility = bool (*)(UObject*, int, void*);
        using Clock = std::chrono::steady_clock;

        auto native_command_ready(void* queue_interface, int command) -> bool
        {
            using CanDispatch = bool (*)(void*, int, void*);
            auto* vtable = *reinterpret_cast<uintptr_t**>(queue_interface);
            return reinterpret_cast<CanDispatch>(vtable[can_dispatch_vtable_offset / sizeof(uintptr_t)])(
                queue_interface, command, nullptr);
        }

        auto player_kind(UObject* pawn) -> int
        {
            auto* property =
                is_uobject_valid_fast(pawn) ? pawn->GetPropertyByNameInChain(L"m_PlayerUniqueID") : nullptr;
            return property && property->GetElementSize() == sizeof(uint8_t)
                       ? *property->ContainerPtrToValuePtr<uint8_t>(pawn)
                       : -1;
        }

        auto in_islands(UObject* pawn) -> bool
        {
            auto* world = kh02_actor_world(pawn);
            return is_uobject_valid_fast(world) && world == kh02_current_story_world() &&
                   world->GetPathName() == L"/Game/Maps/APKH02Mirrors/APKH02DestinyIslands.APKH02DestinyIslands";
        }
    } // namespace

    struct Kh02MickeyLink::Impl
    {
        inline static Impl* active{};
        Kh02ContextRefresh refresh;
        Kh02Localization localization;
        std::unique_ptr<PLH::x64Detour> hook;
        std::unique_ptr<PLH::x64Detour> friend_hook;
        uint64_t original{};
        uint64_t original_friend{};
        FWeakObjectPtr source_pawn;
        FWeakObjectPtr source_world;
        FWeakObjectPtr command_info;
        Clock::time_point deadline{};
        bool pending{};
        bool switched{};
        bool label_ready{};
        uint64_t selections{};
        uint64_t native_accepts{};
        uint64_t native_rejections{};
        std::string phase{"idle"};
        std::string label_text;

        static bool friend_eligible(UObject* manager, int command, void* output)
        {
            auto* self = active;
            if (command == light_chain_command && party_native::is_friend_component(manager))
            {
                auto* controller = party_native::read_object_like_property(manager, L"m_pController");
                auto* pawn = party_native::read_object_like_property(controller, L"Pawn");
                if (in_islands(pawn) && party_native::read_object_like_property(pawn, L"MyFrdMgr") == manager)
                {
                    // KH3 retains the Light Chain state but omits command 197
                    // from its friend eligibility switch. Reuse its supported
                    // Mickey team-command gate; the surrounding native player
                    // state, input and readiness gates still run unchanged.
                    return reinterpret_cast<FriendEligibility>(self->original_friend)(manager, popping_holy_command,
                                                                                      output);
                }
            }
            return reinterpret_cast<FriendEligibility>(self->original_friend)(manager, command, output);
        }

        static bool dispatch(void* queue_interface, int command, UObject* info)
        {
            auto* self = active;
            auto* pawn =
                reinterpret_cast<UObject*>(static_cast<std::byte*>(queue_interface) - state_queue_interface_offset);
            if (command != light_chain_command || !in_islands(pawn))
            {
                return reinterpret_cast<Dispatch>(self->original)(queue_interface, command, info);
            }
            ++self->selections;
            if (player_kind(pawn) == sora_player_id && Kh02Story::native_save_ready(pawn) &&
                runtime_sora_control_ready(pawn) && native_command_ready(queue_interface, command))
            {
                if (self->pending)
                {
                    return true;
                }
                // Defer replacement until the native controller has unwound.
                // Only a real selection reaches this dispatcher; offering the
                // command or rendering its HUD entry never changes character.
                self->source_pawn = make_uobject_weak(pawn);
                self->source_world = make_uobject_weak(kh02_actor_world(pawn));
                self->command_info = make_uobject_weak(info);
                self->deadline = Clock::now() + link_handoff_timeout;
                self->pending = true;
                self->switched = false;
                self->phase = "waiting_to_switch_to_aqua";
                return true;
            }
            const bool accepted = reinterpret_cast<Dispatch>(self->original)(queue_interface, command, info);
            accepted ? ++self->native_accepts : ++self->native_rejections;
            self->phase = accepted ? "native_link_dispatched" : "native_link_rejected";
            return accepted;
        }

        bool install_hooks()
        {
            if (hook)
            {
                return true;
            }
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            using kh02_native::Site;
            if (!kh02_native::verify_sites({Site::link_dispatch, Site::friend_eligibility}))
            {
                phase = "unsupported_link_build";
                return false;
            }
            active = this;
            hook = std::make_unique<PLH::x64Detour>(base + kh02_native::rva(Site::link_dispatch),
                                                    reinterpret_cast<uintptr_t>(&dispatch), &original);
            if (!hook->hook())
            {
                hook.reset();
                phase = "link_hook_failed";
                return false;
            }
            friend_hook =
                std::make_unique<PLH::x64Detour>(base + kh02_native::rva(Site::friend_eligibility),
                                                 reinterpret_cast<uintptr_t>(&friend_eligible), &original_friend);
            if (!friend_hook->hook())
            {
                friend_hook.reset();
                hook.reset();
                phase = "link_friend_hook_failed";
                return false;
            }
            return true;
        }

        bool initialize(UObject* pawn)
        {
            if (!kh02_current_story_world() || !install_hooks())
            {
                return true;
            }
            return prepare_label(pawn);
        }

        bool prepare_label(UObject* pawn)
        {
            auto* table = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr,
                                                                     L"/Game/Load/Common/TresCmdData.TresCmdData");
            if (!is_uobject_valid_fast(table))
            {
                return false;
            }
            Kh02DataTableView view(table, L"/Script/TresGame.TresCommandKindData", "kh02_link_command_table");
            auto* row = view.find(std::to_wstring(light_chain_command).c_str());
            auto* property = view.schema->GetPropertyByNameInChain(L"LocKey");
            if (!row || !property || property->GetClass().GetName() != L"StrProperty" ||
                property->GetOffset_Internal() != command_label_offset || property->GetElementSize() != sizeof(FString))
            {
                phase = "link_label_layout_mismatch";
                return true;
            }
            auto* key = property->ContainerPtrToValuePtr<FString>(row);
            auto localized = localization.lookup(L"kh02_system.command", L"friend_001");
            if (!localized["found"].get<bool>())
            {
                localization.prepare();
                localized = localization.lookup(L"kh02_system.command", L"friend_001");
            }
            if (!localized["found"].get<bool>())
            {
                phase = "link_label_unavailable";
                return true;
            }
            label_text = localized["text"].get<std::string>();
            constexpr auto label = L"kh02_system.command.friend_001";
            if (!key->GetCharArray() || !key->GetCharArray()[0])
            {
                *key = FString(label);
            }
            // The controller copies TresCmdData during startup, before this
            // world's assets are ready. Repair that authoritative cache too.
            auto* controller = party_native::read_object_like_property(pawn, L"Controller");
            if (!is_uobject_valid_fast(controller))
            {
                return false;
            }
            auto* info_property = controller->GetPropertyByNameInChain(L"m_pUICommandInfo");
            if (!info_property || info_property->GetOffset_Internal() != controller_command_info_offset)
            {
                phase = "link_command_cache_layout_mismatch";
                return true;
            }

            struct CommandCache
            {
                std::byte* rows;
                int32_t count;
                int32_t capacity;
            };

            auto* cache = *reinterpret_cast<CommandCache**>(reinterpret_cast<std::byte*>(controller) +
                                                            controller_command_cache_offset);
            if (!cache || !cache->rows || cache->count <= light_chain_command || cache->count > maximum_command_rows ||
                cache->capacity < cache->count || cache->capacity > maximum_command_capacity)
            {
                return false;
            }
            auto* cached_row = cache->rows + light_chain_command * command_row_bytes;
            if (std::to_integer<uint8_t>(cached_row[command_type_offset]) != reaction_command_type ||
                std::to_integer<uint8_t>(cached_row[command_category_offset]) != light_chain_category ||
                std::to_integer<uint8_t>(cached_row[command_old_kind_offset]) != light_chain_command)
            {
                phase = "link_command_cache_row_mismatch";
                return true;
            }
            auto* cached_key = reinterpret_cast<FString*>(cached_row + command_label_offset);
            if (!cached_key->GetCharArray() || !cached_key->GetCharArray()[0])
            {
                *cached_key = FString(label);
            }
            label_ready = true;
            return true;
        }
    };

    Kh02MickeyLink::Kh02MickeyLink() : impl(std::make_unique<Impl>()) {}

    Kh02MickeyLink::~Kh02MickeyLink() = default;

    void Kh02MickeyLink::tick(const std::function<UObject*()>& player,
                              const std::function<bool(UObject*)>& change_to_aqua, bool companion_pending)
    {
        const auto now = Clock::now();
        impl->refresh.observe(world_context_revision.load(std::memory_order_acquire));
        if (impl->refresh.due(now))
        {
            try
            {
                impl->refresh.complete(impl->initialize(player()), now);
            }
            catch (const std::exception& error)
            {
                impl->phase = error.what();
                impl->refresh.complete(true, now);
            }
        }
        if (!impl->pending)
        {
            return;
        }
        auto* pawn = player();
        auto* world = impl->source_world.Get();
        if (now >= impl->deadline || !world || world != kh02_current_story_world())
        {
            impl->pending = false;
            impl->phase = "link_handoff_canceled";
            return;
        }
        // Possession may briefly have no pawn during the character replacement.
        if (!is_uobject_valid_fast(pawn))
        {
            return;
        }
        if (kh02_actor_world(pawn) != world)
        {
            impl->pending = false;
            impl->phase = "link_handoff_canceled";
            return;
        }
        if (!impl->switched)
        {
            if (pawn == impl->source_pawn.Get() && Kh02Story::native_save_ready(pawn) &&
                runtime_sora_control_ready(pawn) && change_to_aqua(pawn))
            {
                impl->switched = true;
                impl->phase = "waiting_for_aqua_companion";
            }
            return;
        }
        if (player_kind(pawn) != aqua_player_id || companion_pending || !Kh02Story::native_save_ready(pawn))
        {
            return;
        }
        auto* queue_interface = reinterpret_cast<std::byte*>(pawn) + state_queue_interface_offset;
        const bool accepted =
            reinterpret_cast<Dispatch>(impl->original)(queue_interface, light_chain_command, impl->command_info.Get());
        accepted ? ++impl->native_accepts : ++impl->native_rejections;
        impl->phase = accepted ? "native_link_dispatched_as_aqua" : "native_link_rejected_after_handoff";
        impl->pending = false;
    }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto Kh02MickeyLink::snapshot() const -> std::string
    {
        return nlohmann::json{{"phase", impl->phase},
                              {"pending", impl->pending},
                              {"label_ready", impl->label_ready},
                              {"label_text", impl->label_text},
                              {"selections", impl->selections},
                              {"native_accepts", impl->native_accepts},
                              {"native_rejections", impl->native_rejections}}
            .dump();
    }

    auto Kh02MickeyLink::offer(UObject* pawn) -> bool
    {
        if (!in_islands(pawn) || !Kh02Story::native_save_ready(pawn))
        {
            return false;
        }
        std::array<UObject*, 6> friends{};
        auto* manager = party_native::read_object_like_property(pawn, L"MyFrdMgr");
        if (!party_native::read_friend_slots(manager, friends))
        {
            return false;
        }
        for (auto* companion : friends)
        {
            if (!is_uobject_valid_fast(companion) || companion->GetClassPrivate()->GetName() != L"n_dw003_Pawn_C")
            {
                continue;
            }
            // The final boss offers a situation command on the controller;
            // Mickey's generic Aqua offer path always substitutes Wayfinder.
            auto* controller = party_native::read_object_like_property(pawn, L"Controller");
            Kh02ReflectedCall offer(controller, L"NotifySetActionCommand", "kh02_link_offer",
                                    reflected_parameter_limit_bytes);
            offer.integer(L"inCommand", light_chain_command);
            offer.number(L"inTimer", diagnostic_offer_seconds);
            offer.object(L"pUICommandInfo", nullptr);
            offer.run();
            return true;
        }
        return false;
    }
#endif
} // namespace kh3ap
