#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/core/settings/pak_seed_settings.h"
#include "kh3ap/core/events/revision_hooks.h"
#include "kh3ap/features/party/party_member_service.h"
#include "kh3ap/features/party/party_helpers.h"
#include "kh3ap/features/party/party_runtime_constants.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/catalog/catalog.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/player/characters/character_party.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <utility>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;

    using party_catalog::choice_for_row_id;
    using party_catalog::default_unlocked_choices;
    using party_catalog::party_choices;
    using party_catalog::stable_seed;
    using party_native::is_friend_component;
    using namespace party_runtime;

    namespace
    {
        struct NavigationNotificationSite
        {
            uintptr_t relative_address;
            std::span<const uint8_t> expected_bytes;
        };

        struct NavigationNotificationProfile
        {
            uint32_t image_timestamp;
            uint32_t image_size;
            std::span<const NavigationNotificationSite> sites;
        };

        // Original stores at OnNavigationInitDone / OnNavDataRegistered,
        // after the engine chooses MyNavData. Native calls bypass ProcessEvent.
        constexpr uint8_t egs_navigation_initialized_bytes[]{0x48, 0x89, 0x87, 0x40, 0x03, 0, 0};
        constexpr uint8_t steam_navigation_initialized_bytes[]{0x48, 0x89, 0x83, 0x40, 0x03, 0, 0};
        constexpr uint8_t navigation_registered_bytes[]{0x48, 0x89, 0xbb, 0x40, 0x03, 0, 0};
        constexpr NavigationNotificationSite egs_navigation_sites[]{
            {0x41e33c5, egs_navigation_initialized_bytes},
            {0x41e349f, navigation_registered_bytes},
        };
        constexpr NavigationNotificationSite steam_navigation_sites[]{
            {0x3c85e35, steam_navigation_initialized_bytes},
            {0x3c85ed5, steam_navigation_initialized_bytes},
            {0x3c85c6f, navigation_registered_bytes},
        };
        constexpr NavigationNotificationProfile navigation_profiles[]{
            {.image_timestamp = kh3ap::native_build::egs.timestamp, .image_size = kh3ap::native_build::egs.image_size, .sites = egs_navigation_sites},
            {.image_timestamp = kh3ap::native_build::steam.timestamp,
             .image_size = kh3ap::native_build::steam.image_size,
             .sites = steam_navigation_sites},
        };

        [[nodiscard]] auto install_party_navigation_notifications(RuntimeNativeRevisionHooks& hooks)
            -> bool
        {
            const auto image_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const auto* dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(image_base);
            const auto* nt_header =
                reinterpret_cast<const IMAGE_NT_HEADERS64*>(image_base + dos_header->e_lfanew);
            for (const auto& profile : navigation_profiles)
            {
                if (nt_header->FileHeader.TimeDateStamp != profile.image_timestamp ||
                    nt_header->OptionalHeader.SizeOfImage != profile.image_size)
                {
                    continue;
                }
                bool ready = true;
                for (const auto& site : profile.sites)
                {
                    // Attempt every site even if an earlier signature was rejected.
                    ready =
                        hooks.install(image_base + site.relative_address, site.expected_bytes) &&
                        ready;
                }
                return ready;
            }
            return false;
        }
    } // namespace

    RuntimePartyMemberService::RuntimePartyMemberService(RuntimePartyMemberConfig config)
        : config_(std::move(config))
    {
    }

    RuntimePartyMemberService::~RuntimePartyMemberService()
    {
        character_party::set_observer({});
    }

    auto RuntimePartyMemberService::install_once() -> void
    {
        if (installed_)
        {
            return;
        }
        const auto& metadata = installed_pak_seed_settings();
        if (!metadata) return;
        installed_ = true;
        bridge_dir_ = config_.bridge_dir_resolver ? config_.bridge_dir_resolver() : std::nullopt;
        std::wstring seed_identity = JsonFieldReader::string_field(*metadata, "seed").value();
        active_ = true;
        character_party::set_observer([this](UObject* previous, UObject* manager, UObject* pawn)
        {
            current_friend_manager_ = manager;
            if (vanilla_friend_manager_.Get() == previous) { vanilla_friend_manager_ = manager; }
            // A controlled swap is neither a room reroll nor a new story entry.
            goa_entry_pawn_ = pawn;
            context_entry_pawn_ = pawn;
            ship_control_sora_ = pawn;
            maintenance_.request();
        });
        for (const std::size_t choice : default_unlocked_choices)
        {
            (void)unlock_choice(choice);
        }
        (void)refresh_unlocks_from_bridge();
        random_.seed(stable_seed(seed_identity));

        navigation_hooks_ = std::make_unique<RuntimeNativeRevisionHooks>();
        const bool navigation_ready = install_party_navigation_notifications(*navigation_hooks_);
        const bool teleport_ready = teleport_wake_.install(
            teleport_function_path.data(), teleport_parameter_size, maintenance_.notifier());
        Output::send<LogLevel::Normal>(
            L"[KH3APProbeCpp] Party maintenance events navigation={} teleport={}\n",
            navigation_ready, teleport_ready);

        set_friend_all_hook_ids_ = UObjectGlobals::RegisterHook(
            std::wstring{set_friend_all_path},
            [](UnrealScriptFunctionCallableContext&, void*)
            {
            },
            [this](UnrealScriptFunctionCallableContext& context, void*)
            {
                if (character_party::transferring()) { return; }
                maintenance_.request();
                (void)capture_vanilla_party(context.Context);
                observe_set_friend_all(context.Context);
            },
            nullptr);
        controller_possess_hook_ids_ = UObjectGlobals::RegisterHook(
            std::wstring{controller_possess_path},
            [](UnrealScriptFunctionCallableContext&, void*)
            {
            },
            [this](UnrealScriptFunctionCallableContext& context, void*)
            {
                maintenance_.request();
                observe_controller_possess(context.Context);
            },
            nullptr);
        can_battle_hook_ids_ = UObjectGlobals::RegisterHook(
            std::wstring{can_battle_path},
            [](UnrealScriptFunctionCallableContext&, void*)
            {
            },
            [this](UnrealScriptFunctionCallableContext& context, void*)
            {
                if (!context.RESULT_DECL)
                {
                    return;
                }
                uint8_t result = 0;
                std::memcpy(&result, context.RESULT_DECL, sizeof(result));
                observe_can_battle(context.Context, result != 0);
            },
            nullptr);
        hook_installed_ =
            (set_friend_all_hook_ids_.first != 0 || set_friend_all_hook_ids_.second != 0) &&
            (controller_possess_hook_ids_.first != 0 || controller_possess_hook_ids_.second != 0) &&
            (can_battle_hook_ids_.first != 0 || can_battle_hook_ids_.second != 0);
        request_reconcile(STR("install"), true);

        Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Runtime party member service installed "
                                           "hook={} reroll_every_room={} unlocked={} choices={}\n"),
                                       hook_installed_, status().reroll_every_room,
                                       status().unlocked_count, party_choices.size());
    }

    auto RuntimePartyMemberService::apply_unlock_item(std::wstring_view runtime_token,
                                                      UObject* sora_pawn,
                                                      std::wstring* failure_reason) -> bool
    {
        if (failure_reason)
        {
            failure_reason->clear();
        }
        if (!active_)
        {
            if (failure_reason)
            {
                *failure_reason = STR("party_member_service_inactive_for_seed");
            }
            return false;
        }
        if (!runtime_token.starts_with(party_unlock_token_prefix))
        {
            if (failure_reason)
            {
                *failure_reason = STR("invalid_party_unlock_token_prefix");
            }
            return false;
        }
        const std::size_t choice =
            choice_for_row_id(runtime_token.substr(party_unlock_token_prefix.size()));
        if (choice == 0)
        {
            if (failure_reason)
            {
                *failure_reason = STR("unknown_tres_party_data_row");
            }
            return false;
        }

        const bool newly_unlocked = unlock_choice(choice);
        if (sora_pawn && is_uobject_valid_fast(sora_pawn))
        {
            UObject* const manager =
                try_read_reflected_object_property_value(sora_pawn, STR("MyFrdMgr"));
            if (is_friend_component(manager))
            {
                current_friend_manager_ = manager;
            }
        }
        if (newly_unlocked)
        {
            Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Runtime party member unlocked "
                                               "member={} active_friend_1={} active_friend_2={}\n"),
                                           party_choices[choice].display_name,
                                           party_choices[active_choices_[0]].display_name,
                                           party_choices[active_choices_[1]].display_name);
        }
        return true;
    }

    auto RuntimePartyMemberService::refresh_unlocks_from_bridge() -> bool
    {
        if (!bridge_dir_.has_value())
        {
            return false;
        }
        const auto items_path = *bridge_dir_ / channels::ap_items_file;
        const auto revision = pipe::input_revision(items_path);
        if (revision == unlock_input_revision_)
        {
            return false;
        }
        const auto items = read_text_file(items_path);
        if (!items.has_value())
        {
            return false;
        }
        unlock_input_revision_ = revision;

        bool changed = false;
        for (const std::string& item : JsonFieldReader::object_array_field(*items, "items"))
        {
            const auto item_id = JsonFieldReader::int64_field(item, "item_id");
            if (!item_id.has_value())
            {
                continue;
            }
            const auto token = native_catalog::runtime_token_for_item(*item_id);
            if (!token.starts_with(party_unlock_token_prefix))
            {
                continue;
            }
            const std::size_t choice =
                choice_for_row_id(token.substr(party_unlock_token_prefix.size()));
            changed = unlock_choice(choice) || changed;
        }
        return changed;
    }

} // namespace kh3ap
