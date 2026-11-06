#include "kh3ap/core/platform/game_build.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/tutorials/tutorial_suppression.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/world/space/space_service.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Windows.h>
#include <intrin.h>
#include <array>
#include <cstring>
#include <memory>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    namespace
    {
        namespace world_map_state
        {
            inline constexpr size_t manager_state_offset = 0x510;
            inline constexpr int overview_tutorial_selection = 107;
            inline constexpr int overview_tutorial_dialogue = 108;
            inline constexpr int embark_tutorial_dialogue = 112;
            inline constexpr int embark_destination_selection = 55;
        } // namespace world_map_state

        uintptr_t base{};
        uintptr_t help_caller_rva{0x18fe8b1}, help_complete_rva{0x18fe9c0};
        bool attempted{};
        unsigned script_tutorial_depth{};
        thread_local unsigned keyblade_menu_tutorial_depth{};
        thread_local unsigned kupo_coin_tutorial_depth{};
        thread_local unsigned synthesis_tutorial_depth{};
        uint64_t help_count{}, completed_help_count{}, dialogue_count{};
        uint64_t original_help{}, original_dialogue{}, original_embark{}, original_keyblade_tutorial{};
        uint64_t original_kupo_coin_tutorial{};
        uint64_t original_savepoint_notice{}, savepoint_notice_count{};
        std::unique_ptr<PLH::x64Detour> help_hook, dialogue_hook, embark_hook, keyblade_tutorial_hook;
        std::unique_ptr<PLH::x64Detour> kupo_coin_tutorial_hook;
        std::unique_ptr<PLH::x64Detour> savepoint_notice_hook;
        uint64_t original_synthesis_tutorial{}, original_synthesis_notice{}, synthesis_notice_count{};
        std::unique_ptr<PLH::x64Detour> synthesis_tutorial_hook, synthesis_notice_hook;
        std::array<uintptr_t,4> synthesis_notice_callers{};
        std::array<uintptr_t,3> savepoint_notice_callers{};
        // KH3 UI panels can have serial zero. Keep the weak owner and resolve
        // its current panel afresh; the integer is identity only, never dereferenced.
        struct Completion { FWeakObjectPtr manager; uintptr_t panel_identity{}; };
        std::vector<Completion> pending;
        using HelpOpen = void(*)(UObject*, uint8_t, int32_t, void*, void*);
        using Dialogue = void(*)(UObject*);

        struct AutomaticTutorialScope
        {
            unsigned& depth;
            explicit AutomaticTutorialScope(unsigned& value) : depth(value) { ++depth; }
            ~AutomaticTutorialScope() { --depth; }
        };

        void enter_keyblade_menu_tutorial(UObject* equipment_menu)
        {
            // Scope only the native automatic equipment-menu request. Manual
            // Game Help remains available, even if it opens this same help ID.
            AutomaticTutorialScope scope(keyblade_menu_tutorial_depth);
            reinterpret_cast<Dialogue>(original_keyblade_tutorial)(equipment_menu);
        }

        void enter_kupo_coin_tutorial(UObject* shop_menu)
        {
            AutomaticTutorialScope scope(kupo_coin_tutorial_depth);
            reinterpret_cast<Dialogue>(original_kupo_coin_tutorial)(shop_menu);
        }

        uint8_t enter_synthesis_tutorial(UObject* menu)
        {
            AutomaticTutorialScope scope(synthesis_tutorial_depth);
            using SelectHelp = uint8_t(*)(UObject*);
            return reinterpret_cast<SelectHelp>(original_synthesis_tutorial)(menu);
        }

        uint8_t suppress_synthesis_notice(void* queue, void* text)
        {
            const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - base;
            bool suppress = false;
            for (const auto site : synthesis_notice_callers)
                suppress = suppress || caller == site;
            if (suppress)
            {
                // These callers enqueue only {FString, bool} display records.
                // They ignore the result and destroy their temporary FString
                // afterward. Leaving it unmoved preserves native ownership.
                // Material/reward writes and recipe processing are outside
                // the enqueue and still run. The empty queue opens no dialog.
                ++synthesis_notice_count;
                Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] SynthesisNotice queued=false caller_rva=0x{:X} count={}\n"),
                    caller, synthesis_notice_count);
                return 1;
            }
            using Enqueue = uint8_t(*)(void*, void*);
            return reinterpret_cast<Enqueue>(original_synthesis_notice)(queue, text);
        }

        void enter_steam_kupo_coin_tutorial(UObject* shop_menu, uint8_t only_if_unseen)
        {
            // Steam adds a first-time flag. Preserve its second argument and
            // keep explicit help requests (flag false) outside suppression.
            using Tutorial=void(*)(UObject*,uint8_t);
            if (!only_if_unseen)
            {
                reinterpret_cast<Tutorial>(original_kupo_coin_tutorial)(shop_menu,only_if_unseen);
                return;
            }
            AutomaticTutorialScope scope(kupo_coin_tutorial_depth);
            reinterpret_cast<Tutorial>(original_kupo_coin_tutorial)(shop_menu,only_if_unseen);
        }

        uint8_t suppress_savepoint_notice(void* queue, const void* text)
        {
            const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
            for (size_t index=0;index<savepoint_notice_callers.size();++index)
            {
                if (caller!=savepoint_notice_callers[index]) continue;
                ++savepoint_notice_count;
                Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] TutorialSuppression savepoint_notice={} queued=false count={}\n"),index,savepoint_notice_count);
                // These three callers ignore the enqueue result and then set
                // their native seen bits. They have already added the menu
                // options. An empty notice queue takes the normal idle path.
                return 1;
            }
            using Enqueue=uint8_t(*)(void*,const void*);
            return reinterpret_cast<Enqueue>(original_savepoint_notice)(queue,text);
        }

        bool script_is_tutorial(UnrealScriptFunctionCallableContext& context, bool with_message)
        {
            if (!with_message) return true;
            auto* fn=get_frame_node(context.TheStack);
            auto* params=get_frame_locals(context.TheStack);
            auto* flag=is_uobject_valid_fast(fn)?find_function_param_by_name(fn,L"bTutorial"):nullptr;
            return params && flag && flag->IsA<FBoolProperty>() &&
                static_cast<FBoolProperty*>(flag)->GetPropertyValueInContainer(params);
        }

        void suppress_help(UObject* panel, uint8_t id, int32_t kind, void* text, void* callback)
        {
            auto* manager=is_uobject_valid_fast(panel)?
                try_read_reflected_object_property_value(panel,L"m_pUIManager"):nullptr;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] TutorialSuppression request id={} kind={} caller_rva={} script_depth={} space={} manager_valid={} owner_matches={} panel_open={}\n"),
                id,kind,reinterpret_cast<uintptr_t>(_ReturnAddress())-base,script_tutorial_depth,
                RuntimeSpaceService::is_space_active(),is_uobject_valid_fast(manager),
                is_uobject_valid_fast(manager)&&try_read_reflected_object_property_value(manager,L"m_GameHelp")==panel,
                is_uobject_valid_fast(panel)?*(reinterpret_cast<uint8_t*>(panel)+0xa1):255);
#endif
            // Only the owning UI manager's verified call site: it has already
            // marked help seen, stored the caller's delegate and acquired pause.
            // Never open a movie or synthesize input. Complete on the next tick
            // after the caller has finished installing its waiting state.
            if (reinterpret_cast<uintptr_t>(_ReturnAddress())==base+help_caller_rva &&
                (script_tutorial_depth || RuntimeSpaceService::is_space_active() ||
                 (keyblade_menu_tutorial_depth && id==14) ||
                 (kupo_coin_tutorial_depth && id==2) ||
                 (synthesis_tutorial_depth && id>=16 && id<=18)) &&
                is_uobject_valid_fast(manager) &&
                try_read_reflected_object_property_value(manager,L"m_GameHelp")==panel &&
                !*(reinterpret_cast<uint8_t*>(panel)+0xa1))
            {
                Completion completion{}; completion.manager=manager;
                completion.panel_identity=reinterpret_cast<uintptr_t>(panel);
                if (completion.manager.Get()==manager)
                {
                    pending.push_back(completion); ++help_count;
                    Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] TutorialSuppression help_id={} opened=false count={}\n"),id,help_count);
                    return;
                }
            }
            reinterpret_cast<HelpOpen>(original_help)(panel,id,kind,text,callback);
        }

        bool owns_overview(UObject* manager)
        {
            if (!RuntimeSpaceService::is_space_active() || !is_uobject_valid_fast(manager)) return false;
            auto* ui=try_read_reflected_object_property_value(manager,L"m_pUIManager");
            auto* controller=try_read_reflected_object_property_value(manager,L"m_pController");
            auto* hud=try_read_reflected_object_property_value(manager,L"m_pGummiHUD");
            if (!is_uobject_valid_fast(ui) || !is_uobject_valid_fast(controller) ||
                !is_uobject_valid_fast(hud) ||
                try_read_reflected_object_property_value(ui,L"m_pPlayerController")!=controller ||
                manager->GetOuterPrivate()!=controller->GetOuterPrivate()) return false;
            auto* property=manager->GetPropertyByNameInChain(L"m_pUIManager");
            return property && property->GetOffset_Internal()==0x480;
        }

        void suppress_dialogue(UObject* manager)
        {
            // Keep native selection, seen bits and any accompanying unlocks.
            // The dispatcher queues state 108; only the next manager tick
            // opens the Chip/Dale window and splits the text into pages.
            reinterpret_cast<Dialogue>(original_dialogue)(manager);
            if (!owns_overview(manager)) return;
            auto* state = reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(manager) +
                                                     world_map_state::manager_state_offset);
            if (*state != world_map_state::overview_tutorial_dialogue)
            {
                return;
            }
            *state =
                world_map_state::overview_tutorial_selection; // Re-enter native tutorial selection;
                                                              // eventually exits to state 19.
            ++dialogue_count;
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] TutorialSuppression world_map_dialogue opened=false all_pages=true count={}\n"),dialogue_count);
        }

        void suppress_embark_dialogue(UObject* manager)
        {
            // Destination-selector setup is a separate entry point from the
            // overview introduction. Native setup marks seen bit 0x200 and
            // queues state 112 (top_0430), bypassing the state-107 dispatcher.
            // Keep all setup and the seen bit, then take its already-seen
            // continuation before state 112 can open the Chip/Dale panel.
            reinterpret_cast<Dialogue>(original_embark)(manager);
            if (!owns_overview(manager)) return;
            auto* state = reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(manager) +
                                                     world_map_state::manager_state_offset);
            if (*state != world_map_state::embark_tutorial_dialogue)
            {
                return;
            }
            *state = world_map_state::embark_destination_selection;
            ++dialogue_count;
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] TutorialSuppression embark_destination_dialogue opened=false all_pages=true count={}\n"),dialogue_count);
        }

        void register_script_tutorial_hooks()
        {
        for (const bool with_message:{false,true})
        {
            const auto* path=with_message?L"/Script/TresGame.TresGameBlueprintLibrary:TresUI_OpenGameHelpWithMessage":
                L"/Script/TresGame.TresGameBlueprintLibrary:TresUI_OpenGameHelp";
            UObjectGlobals::RegisterHook(path,[with_message](UnrealScriptFunctionCallableContext& context,void*) {
                if (script_is_tutorial(context,with_message)) ++script_tutorial_depth;
            },[with_message](UnrealScriptFunctionCallableContext& context,void*) {
                if (script_is_tutorial(context,with_message) && script_tutorial_depth) --script_tutorial_depth;
            },nullptr);
        }
        }

        bool matches(uintptr_t address, std::initializer_list<uint8_t> bytes)
        { return !std::memcmp(reinterpret_cast<void*>(address),bytes.begin(),bytes.size()); }

        bool matches_relative_target(uintptr_t address, std::initializer_list<uint8_t> prefix, uintptr_t target)
        {
            if (!matches(address,prefix)) return false;
            int32_t displacement{};
            std::memcpy(&displacement,reinterpret_cast<void*>(address+prefix.size()),sizeof(displacement));
            return address+prefix.size()+sizeof(displacement)+displacement==target;
        }

        void install_savepoint_notice_suppression(bool steam)
        {
            // Queue entries for kh3_menu.savepoint.dialog_0001/0006/0007:
            // Area selection, Bistro and Tutorials. Keep menu construction
            // and the native seen writes; skip only their notice enqueue.
            const uintptr_t entry=steam?0x16fbba0:0x1aada10;
            const std::array<uintptr_t,3> sites=steam?
                std::array<uintptr_t,3>{0x1718475,0x17184ac,0x17184de}:
                std::array<uintptr_t,3>{0x1a9453b,0x1a94570,0x1a945a5};
            const std::array<uintptr_t,3> keys=steam?
                std::array<uintptr_t,3>{0x86f3d10,0x86f3d20,0x86f3d30}:
                std::array<uintptr_t,3>{0x8d3d180,0x8d3d190,0x8d3d1a0};
            bool signatures=matches(base+entry,{0x40,0x57,0x48,0x83,0xec,0x30,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff,0x48,0x89,0x5c,0x24,0x40,0x48,0x89,0x74,0x24,0x48});
            for (size_t index=0;index<sites.size();++index)
            {
                const auto site=base+sites[index];
                const auto bit=static_cast<uint8_t>(1u<<index);
                signatures=signatures &&
                    (steam?matches(site-33,{0xf6,0x83,0x84,0x03,0x00,0x00,bit,0x75,0x24,0x33,0xd2}):
                           matches(site-31,{0xf6,0x86,0x3c,0x49,0x01,0x00,bit,0x75,0x22})) &&
                    matches_relative_target(site-22,{0x48,0x8d,0x0d},base+keys[index]) &&
                    matches_relative_target(site-15,{0xe8},base+(steam?0x1580780:0x18d7bc0)) &&
                    matches(site-10,{0x48,0x8b,0xd0,0x48,0x8d,static_cast<uint8_t>(steam?0x8e:0x8d),0x20,0x06,0x00,0x00}) &&
                    matches_relative_target(site,{0xe8},base+entry) &&
                    (steam?matches(site+5,{0x80,0x8b,0x84,0x03,0x00,0x00,bit}):
                           matches(site+5,{0x80,0x8e,0x3c,0x49,0x01,0x00,bit}));
                savepoint_notice_callers[index]=site+5;
            }
            if (!signatures)
            {
                Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] SavePointNotice suppression rejected reason=signature storefront={}\n"),steam?STR("steam"):STR("egs"));
                return;
            }
            savepoint_notice_hook=std::make_unique<PLH::x64Detour>(base+entry,
                reinterpret_cast<uintptr_t>(&suppress_savepoint_notice),&original_savepoint_notice);
            const bool installed=savepoint_notice_hook->hook();
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] SavePointNotice suppression installed={} area_selection=true bistro=true tutorials=true native_seen_bits=true menu_options_preserved=true storefront={}\n"),installed,steam?STR("steam"):STR("egs"));
        }

        void install_keyblade_menu_tutorial_suppression(bool steam)
        {
            // Help_01400 (14), "Switching Between Keyblades". Both routines
            // count equipped/backyard Keyblades, require two, and use the
            // normal help manager's seen-bit write. Their completion delegate
            // is a no-op: there is no grant, ability unlock or equipment write.
            // Keep the entire routine; suppress only the scoped panel open.
            const uintptr_t entry=steam?0x15ef640:0x194c080;
            const bool signatures=steam
                ? matches(base+entry,{0x48,0x8b,0xc4,0x57,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0xa0,0x00,0x00,0x00}) &&
                  matches(base+0x15ef81a,{0x41,0xb1,0x01,0x4c,0x8d,0x44,0x24,0x50,0xb2,0x0e,0x48,0x8b,0x8e,0xd8,0x00,0x00,0x00,0xe8,0x20,0x2c,0xfb,0xff}) &&
                  matches(base+0x15ef7f2,{0x48,0x8d,0x05,0x27,0xf9,0xe2,0xfe,0x48,0x89,0x43,0x10}) &&
                  matches(base+0x41f120,{0xc2,0x00,0x00})
                : matches(base+entry,{0x48,0x8b,0xc4,0x57,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0xb0,0x00,0x00,0x00}) &&
                  matches(base+0x194c29a,{0xf7,0x80,0x10,0x45,0x01,0x00,0x00,0x40,0x00,0x00,0x75,0x34}) &&
                  matches(base+0x194c2c1,{0x45,0x33,0xc0,0xb2,0x0e,0xe8,0xb5,0x23,0xfb,0xff}) &&
                  matches(base+0x194c246,{0x48,0x8d,0x05,0x23,0xf9,0xc3,0xfe,0x49,0x89,0x46,0x10}) &&
                  matches(base+0x58bb70,{0xc2,0x00,0x00});
            if (!signatures)
            {
                Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] KeybladeMenuTutorial suppression rejected reason=signature storefront={}\n"),steam?STR("steam"):STR("egs"));
                return;
            }
            keyblade_tutorial_hook=std::make_unique<PLH::x64Detour>(
                base+entry,reinterpret_cast<uintptr_t>(&enter_keyblade_menu_tutorial),&original_keyblade_tutorial);
            const bool installed=keyblade_tutorial_hook->hook();
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] KeybladeMenuTutorial suppression installed={} help_id=14 pre_open=true native_seen_bit=true native_completion=true manual_help_preserved=true storefront={}\n"),installed,steam?STR("steam"):STR("egs"));
        }

        void install_kupo_coin_tutorial_suppression(bool steam)
        {
            // Help_00300 (2), "Kupo Coins". The shop's tutorial request only
            // constructs a no-op delegate and opens help. Keep native first-
            // time selection, seen-bit handling and completion; purchase and
            // revival logic live outside this routine and remain untouched.
            const uintptr_t entry=steam?0x173f030:0x1ab5600;
            const bool signatures=steam
                ? matches(base+entry,{0x48,0x8b,0xc4,0x55,0x57,0x41,0x56,0x48,0x8d,0x68,0xa1,0x48,0x81,0xec,0xb0,0x00,0x00,0x00}) &&
                  matches(base+0x173f060,{0x0f,0xb6,0xf2,0x48,0x8b,0xf9}) &&
                  matches(base+0x173f102,{0x44,0x0f,0xb6,0xce,0x4c,0x8d,0x45,0xf7,0xb2,0x02,0x48,0x8b,0x4f,0x70,0xe8,0x3b,0x33,0xe6,0xff}) &&
                  matches(base+0x173f077,{0x48,0x8d,0x05,0xa2,0x00,0xce,0xfe,0x48,0x89,0x45,0xcf}) &&
                  matches(base+0x41f120,{0xc2,0x00,0x00}) &&
                  matches(base+0x15a2474,{0x45,0x84,0xc9,0x74,0x19,0xe8,0xa2,0x61,0x90,0xff,0x40,0x0f,0xb6,0xd7,0x48,0x8d,0x88,0xa0,0x3c,0x01,0x00,0xe8,0x82,0xd0,0x8e,0xff,0x84,0xc0,0x75,0x3d})
                : matches(base+entry,{0x48,0x8b,0xc4,0x55,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8d,0x68,0xa1,0x48,0x81,0xec,0xc0,0x00,0x00,0x00}) &&
                  matches(base+0x1ab56f1,{0xf6,0x80,0x10,0x45,0x01,0x00,0x04,0x75,0x2b}) &&
                  matches(base+0x1ab5714,{0x45,0x33,0xc0,0xb2,0x02,0xe8,0x62,0x8f,0xe4,0xff}) &&
                  matches(base+0x1ab5648,{0x48,0x8d,0x05,0x21,0x65,0xad,0xfe,0x48,0x89,0x45,0xbf}) &&
                  matches(base+0x58bb70,{0xc2,0x00,0x00});
            if (!signatures)
            {
                Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] KupoCoinTutorial suppression rejected reason=signature storefront={}\n"),steam?STR("steam"):STR("egs"));
                return;
            }
            const auto replacement=steam?reinterpret_cast<uintptr_t>(&enter_steam_kupo_coin_tutorial):
                reinterpret_cast<uintptr_t>(&enter_kupo_coin_tutorial);
            kupo_coin_tutorial_hook=std::make_unique<PLH::x64Detour>(base+entry,replacement,&original_kupo_coin_tutorial);
            const bool installed=kupo_coin_tutorial_hook->hook();
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] KupoCoinTutorial suppression installed={} help_id=2 pre_open=true native_seen_bit=true native_completion=true manual_help_preserved=true storefront={}\n"),installed,steam?STR("steam"):STR("egs"));
        }

        void install_synthesis_suppression(bool steam)
        {
            // Preserve each storefront's selector, its seen tests,
            // delegates and return value; the help manager completes normally.
            const uintptr_t tutorial_entry=steam?0x173d940:0x1ac57c0;
            const bool tutorial_signatures = steam ?
                matches(base+0x173d940,{0x48,0x8b,0xc4,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x68,0xa1}) &&
                matches(base+0x173da11,{0x41,0xb1,0x01,0x4c,0x8d,0x45,0xe7,0xb2,0x10,0x49,0x8b,0x8f,0xd8,0x00,0x00,0x00,0xe8,0x2a,0x4a,0xe6,0xff}) &&
                matches(base+0x173db9f,{0x41,0xb1,0x01,0x4c,0x8d,0x45,0xe7,0xb2,0x11,0x49,0x8b,0x8f,0xd8,0x00,0x00,0x00,0xe8,0x9c,0x48,0xe6,0xff}) &&
                matches(base+0x173dcc6,{0x41,0xb1,0x01,0x4c,0x8d,0x45,0xe7,0xb2,0x12,0x49,0x8b,0x8f,0xd8,0x00,0x00,0x00,0xe8,0x75,0x47,0xe6,0xff}) &&
                matches(base+0x15a2474,{0x45,0x84,0xc9,0x74,0x19,0xe8,0xa2,0x61,0x90,0xff,0x40,0x0f,0xb6,0xd7,0x48,0x8d,0x88,0xa0,0x3c,0x01,0x00,0xe8,0x82,0xd0,0x8e,0xff,0x84,0xc0,0x75,0x3d}) :
                matches(base+0x1ac57c0,{0x48,0x8b,0xc4,0x55,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8d,0x68,0xa1}) &&
                matches(base+0x1ac582c,{0xf7,0x80,0x10,0x45,0x01,0x00,0x00,0x00,0x01,0x00}) &&
                matches(base+0x1ac5853,{0x45,0x33,0xc0,0xb2,0x10,0xe8,0x23,0x8e,0xe3,0xff}) &&
                matches(base+0x1ac5991,{0xf7,0x80,0x10,0x45,0x01,0x00,0x00,0x00,0x02,0x00}) &&
                matches(base+0x1ac59b8,{0x45,0x33,0xc0,0xb2,0x11,0xe8,0xbe,0x8c,0xe3,0xff}) &&
                matches(base+0x1ac5ae7,{0xf7,0x80,0x10,0x45,0x01,0x00,0x00,0x00,0x04,0x00}) &&
                matches(base+0x1ac5b0e,{0x45,0x33,0xc0,0xb2,0x12,0xe8,0x68,0x8b,0xe3,0xff});
            if (tutorial_signatures)
            {
                synthesis_tutorial_hook=std::make_unique<PLH::x64Detour>(base+tutorial_entry,
                    reinterpret_cast<uintptr_t>(&enter_synthesis_tutorial),&original_synthesis_tutorial);
            }
            const bool tutorial_installed=tutorial_signatures && synthesis_tutorial_hook->hook();

            // All four queue producers use the synthesis menu's +0x1a0
            // queue and ignore AL. Guard the call, argument setup and normal
            // FString cleanup, as well as the enqueue's move-only payload.
            const uintptr_t notice_entry=steam?0x172e8f0:0x1ad33c0;
            const bool notice_signatures = steam ?
                matches(base+0x172e8f0,{0x40,0x57,0x48,0x83,0xec,0x30,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff}) &&
                matches(base+0x172e938,{0x48,0x8b,0x03,0x49,0x89,0x00,0x4c,0x89,0x13,0x8b,0x43,0x08,0x41,0x89,0x40,0x08}) &&
                matches(base+0x172f087,{0x48,0x8d,0x8f,0xa0,0x01,0x00,0x00,0x48,0x8d,0x54,0x24,0x38,0xe8,0x58,0xf8,0xff,0xff,0x90,0x48,0x8b,0x4c,0x24,0x38,0x48,0x85,0xc9}) &&
                matches(base+0x172ec83,{0x49,0x8d,0x8e,0xa0,0x01,0x00,0x00,0x48,0x8d,0x54,0x24,0x70,0xe8,0x5c,0xfc,0xff,0xff,0x90,0x48,0x8b,0x4c,0x24,0x70,0x48,0x85,0xc9}) &&
                matches(base+0x172eea3,{0x49,0x8d,0x8e,0xa0,0x01,0x00,0x00,0x48,0x8d,0x55,0x88,0xe8,0x3d,0xfa,0xff,0xff,0x90,0x48,0x8b,0x4d,0x88,0x48,0x85,0xc9}) &&
                matches(base+0x172f1e8,{0x48,0x8d,0x8f,0xa0,0x01,0x00,0x00,0x48,0x8d,0x54,0x24,0x50,0xe8,0xf7,0xf6,0xff,0xff,0x90,0x48,0x8b,0x4c,0x24,0x50,0x48,0x85,0xc9}) :
                matches(base+0x1ad33c0,{0x40,0x57,0x48,0x83,0xec,0x30,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff}) &&
                matches(base+0x1ad3423,{0x48,0x8b,0x03,0x48,0x89,0x01,0x4c,0x89,0x03,0x8b,0x43,0x08,0x89,0x41,0x08}) &&
                matches(base+0x1ac3db5,{0x48,0x8d,0x8b,0xa0,0x01,0x00,0x00,0x48,0x8d,0x55,0xcf,0xe8,0xfb,0xf5,0x00,0x00,0x90,0x48,0x8b,0x4d,0xcf,0x48,0x85,0xc9}) &&
                matches(base+0x1ac4a62,{0x49,0x8d,0x8f,0xa0,0x01,0x00,0x00,0x48,0x8d,0x55,0x80,0xe8,0x4e,0xe9,0x00,0x00,0x90,0x48,0x8b,0x4d,0x80,0x48,0x85,0xc9}) &&
                matches(base+0x1ac4caf,{0x49,0x8d,0x8f,0xa0,0x01,0x00,0x00,0x48,0x8d,0x55,0x98,0xe8,0x01,0xe7,0x00,0x00,0x90,0x48,0x8b,0x4d,0x98,0x48,0x85,0xc9}) &&
                matches(base+0x1ac5019,{0x48,0x8d,0x8f,0xa0,0x01,0x00,0x00,0x48,0x8d,0x54,0x24,0x48,0xe8,0x96,0xe3,0x00,0x00,0x90,0x48,0x8b,0x4c,0x24,0x48,0x48,0x85,0xc9});
            if (notice_signatures)
            {
                synthesis_notice_callers=steam?
                    std::array<uintptr_t,4>{0x172f098,0x172ec94,0x172eeb3,0x172f1f9}:
                    std::array<uintptr_t,4>{0x1ac3dc5,0x1ac4a72,0x1ac4cbf,0x1ac502a};
                synthesis_notice_hook=std::make_unique<PLH::x64Detour>(base+notice_entry,
                    reinterpret_cast<uintptr_t>(&suppress_synthesis_notice),&original_synthesis_notice);
            }
            const bool notice_installed=notice_signatures && synthesis_notice_hook->hook();
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] SynthesisSuppression tutorials_installed={} notices_installed={} help_ids=16,17,18 native_processing=preserved storefront={}\n"),
                tutorial_installed,notice_installed,steam?STR("steam"):STR("egs"));
        }

        bool install_steam_embark_suppression()
        {
            // Steam 0x66743F34: destination setup at 0x17d8c60 calls the
            // native test-and-set of seen bit 9, then queues state 112 or 55.
            // Change only the first-time continuation to 55. All selector
            // setup, the seen-bit write and native cleanup remain intact.
            // This is independent of the EGS-only help/overview detours.
            if (!matches(base+0x17d8c60,{0x40,0x57,0x48,0x83,0xec,0x50,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff}) ||
                !matches(base+0x17d8da4,{0xe8,0x67,0x9d,0xd4,0xff,0x48,0x85,0xc0,0x74,0x1c,0x48,0x8d,0x88,0x78,0x46,0x01,0x00,0xe8,0xf6,0x4e,0x8c,0xff,0x3c,0x01,0x75,0x0c}) ||
                !matches(base+0x109dcb0,{0x0f,0xb7,0x81,0xfe,0x01,0x00,0x00,0xba,0x00,0x02,0x00,0x00,0x66,0x85,0xc2,0x75,0x0d,0x66,0x0b,0xc2,0x66,0x89,0x81,0xfe,0x01,0x00,0x00,0xb0,0x01,0xc3,0x32,0xc0,0xc3}) ||
                !matches(base+0x17d8dbe,{0xc7,0x83,0x10,0x05,0x00,0x00,0x70,0x00,0x00,0x00,0xeb,0x0a,0xc7,0x83,0x10,0x05,0x00,0x00,0x37,0x00,0x00,0x00})) return false;
            auto* immediate=reinterpret_cast<uint8_t*>(base+0x17d8dc4);
            DWORD protection{};
            if (!VirtualProtect(immediate,1,PAGE_EXECUTE_READWRITE,&protection)) return false;
            *immediate=55;
            const bool flushed=FlushInstructionCache(GetCurrentProcess(),immediate,1)!=FALSE;
            DWORD ignored{};
            const bool restored=VirtualProtect(immediate,1,protection,&ignored)!=FALSE;
            if (flushed && restored && *immediate==55) return true;
            DWORD rollback{};
            if (VirtualProtect(immediate,1,PAGE_EXECUTE_READWRITE,&rollback))
            {
                *immediate=112;
                FlushInstructionCache(GetCurrentProcess(),immediate,1);
                VirtualProtect(immediate,1,protection,&ignored);
            }
            return false;
        }
    }

    uint64_t runtime_suppressed_help_count() { return help_count; }
    uint64_t runtime_suppressed_help_completion_count() { return completed_help_count; }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    bool test_runtime_space_dialogue_suppression(UObject* ui)
    {
        if (!original_dialogue || !original_embark || !RuntimeSpaceService::is_space_active() || !is_uobject_valid_fast(ui)) return false;
        auto* hud=try_read_reflected_object_property_value(ui,L"m_ActiveHUD");
        auto* menu=is_uobject_valid_fast(hud)?try_read_reflected_object_property_value(hud,L"m_pGummiMenu"):nullptr;
        auto* manager=is_uobject_valid_fast(menu)?try_read_reflected_object_property_value(menu,L"m_pMenuManager"):nullptr;
        auto* instance=try_read_reflected_object_property_value(ui,L"m_pGameInstance");
        if (!is_uobject_valid_fast(manager) || !is_uobject_valid_fast(instance) ||
            try_read_reflected_object_property_value(manager,L"m_pUIManager")!=ui) return false;
        auto* state = reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(manager) +
                                                 world_map_state::manager_state_offset);
        // 20 is the idle overview; 55 is the idle Embark destination selector.
        // Each has a different native tutorial entry point and seen bit.
        if (*state != 20 && *state != world_map_state::embark_destination_selection)
        {
            return false;
        }
        const auto original_state=*state;
        const bool embark = original_state == world_map_state::embark_destination_selection;
        const uint16_t seen_bit=embark?0x200:1;
        auto* seen=reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(instance)+0x150e6);
        const auto backup=*seen;
        const auto count=dialogue_count;
        const auto menu_state=*reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(menu)+0x3d8);
        const auto region=*reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(manager)+0x6bc);
        std::array<int32_t,3> selection{};
        std::memcpy(selection.data(),reinterpret_cast<uint8_t*>(manager)+0x650,sizeof(selection));
        // Exercise the installed detour via the real native entry, rather
        // than directly calling our wrapper (which cannot prove hook wiring).
        *seen=backup&~seen_bit;
        reinterpret_cast<Dialogue>(base+(embark?0x1b721a0:0x1b734c0))(manager);
        const bool passed =
            dialogue_count == count + 1 &&
            *state == (embark ? world_map_state::embark_destination_selection
                              : world_map_state::overview_tutorial_selection) &&
            (*seen & seen_bit) &&
            *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(manager) + 0x6bc) == region &&
            !std::memcmp(selection.data(), reinterpret_cast<uint8_t*>(manager) + 0x650,
                         sizeof(selection)) &&
            *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(menu) + 0x3d8) == menu_state;
        *seen=backup; *state=original_state;
        Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] TutorialSuppression dialogue fixture passed={} destination_selector={} all_pages_skipped=true original_seen={} restored_seen={}\n"),passed,embark,backup,*seen);
        return passed;
    }
#endif

    void tick_runtime_tutorial_suppression(std::chrono::steady_clock::time_point)
    {
        // Move the queue first: completion can legitimately request another
        // tutorial. That new request must complete on a later tick as well.
        auto completions=std::move(pending); pending.clear();
        for (auto& completion:completions)
        {
            auto* manager=completion.manager.Get();
            auto* panel=is_uobject_valid_fast(manager)?
                try_read_reflected_object_property_value(manager,L"m_GameHelp"):nullptr;
            if (!is_uobject_valid_fast(manager) || !is_uobject_valid_fast(panel) ||
                reinterpret_cast<uintptr_t>(panel)!=completion.panel_identity ||
                *(reinterpret_cast<uint8_t*>(panel)+0xa1)) continue;
            using Complete=void(*)(UObject*,int32_t);
            reinterpret_cast<Complete>(base+help_complete_rva)(manager,0);
            ++completed_help_count;
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] TutorialSuppression completion delivered opened=false\n"));
        }
        if (attempted) return;
        attempted=true;
        base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (!base || dos->e_magic!=IMAGE_DOS_SIGNATURE) return;
        auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        if (pe->Signature!=IMAGE_NT_SIGNATURE) return;
        if (kh3ap::native_build::steam.matches(pe->FileHeader.TimeDateStamp, pe->OptionalHeader.SizeOfImage))
        {
            const bool installed=install_steam_embark_suppression();
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] TutorialSuppression steam_embark installed={} pre_open=true native_seen_bit=true\n"),installed);
            install_savepoint_notice_suppression(true);
            // Steam's UI manager stores the caller delegate at +0x8c0 and
            // owns pause at +0x900. Its normal completion releases both.
            if (!matches(base+0x1606d30,{0x40,0x53,0x48,0x83,0xec,0x20,0x88,0x91,0xc8,0x01,0x00,0x00,0x48,0x8b,0xd9,0x44,0x89,0x81,0x08,0x02,0x00,0x00,0x49,0x8b}) ||
                !matches(base+0x15a2761,{0xe8,0xca,0x45,0x06,0x00}) ||
                !matches(base+0x15a26b4,{0x48,0x8d,0x8f,0xc0,0x08,0x00,0x00,0x48,0x8b,0x55,0x97,0xe8,0x0c,0xe3,0xe8,0xfe,0x48,0x8d,0x05,0x85,0xcf,0xff,0xff,0x48,0x89,0x45,0x9f}) ||
                !matches(base+0x159f650,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x30,0x80,0xb9,0x00,0x09,0x00,0x00,0x00,0x8b,0xfa,0x48,0x8b,0xd9,0x74,0x19,0x45,0x33,0xc9,0xc6,0x44,0x24})) return;
            help_caller_rva=0x15a2766; help_complete_rva=0x159f650;
            help_hook=std::make_unique<PLH::x64Detour>(base+0x1606d30,reinterpret_cast<uintptr_t>(&suppress_help),&original_help);
            if (!help_hook->hook()) return;
            register_script_tutorial_hooks();
            install_keyblade_menu_tutorial_suppression(true);
            install_kupo_coin_tutorial_suppression(true);
            install_synthesis_suppression(true);
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] TutorialSuppression steam_help installed=true pre_open=true native_completion=true\n"));
            return;
        }
        if (!kh3ap::native_build::egs.matches(pe->FileHeader.TimeDateStamp, pe->OptionalHeader.SizeOfImage) ||
            !matches(base+0x1961c80,{0x40,0x53,0x48,0x83,0xec,0x30,0x88,0x91,0xc8,0x01,0x00,0x00,0x48,0x8b,0xd9}) ||
            !matches(base+0x18fe8ac,{0xe8,0xcf,0x33,0x06,0x00}) ||
            !matches(base+0x18fe9c0,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x30,0x80,0xb9,0xa0,0x08,0x00,0x00,0x00}) ||
            !matches(base+0x1b734c0,{0x48,0x8b,0xc4,0x55,0x41,0x56,0x41,0x57,0x48,0x8b,0xec,0x48,0x83,0xec,0x50}) ||
            !matches(base+0x1b73688,{0xc7,0x83,0x10,0x05,0x00,0x00,0x6c,0x00,0x00,0x00}) ||
            !matches(base+0x1b74108,{0xc7,0x83,0x10,0x05,0x00,0x00,0x13,0x00,0x00,0x00}) ||
            !matches(base+0x1b721a0,{0x40,0x57,0x48,0x83,0xec,0x50,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff}) ||
            !matches(base+0x1b72373,{0xba,0x00,0x02,0x00,0x00,0x66,0x85,0xc2}) ||
            !matches(base+0x1b72380,{0x66,0x89,0x81,0x76,0x48,0x01,0x00,0xb8,0x70,0x00,0x00,0x00,0xeb,0x05,0xb8,0x37,0x00,0x00,0x00,0x89,0x83,0x10,0x05,0x00,0x00})) return;
        help_hook=std::make_unique<PLH::x64Detour>(base+0x1961c80,reinterpret_cast<uintptr_t>(&suppress_help),&original_help);
        dialogue_hook=std::make_unique<PLH::x64Detour>(base+0x1b734c0,reinterpret_cast<uintptr_t>(&suppress_dialogue),&original_dialogue);
        embark_hook=std::make_unique<PLH::x64Detour>(base+0x1b721a0,reinterpret_cast<uintptr_t>(&suppress_embark_dialogue),&original_embark);
        if (!help_hook->hook() || !dialogue_hook->hook() || !embark_hook->hook())
        {
            if (help_hook->isHooked()) help_hook->unHook();
            if (dialogue_hook->isHooked()) dialogue_hook->unHook();
            if (embark_hook->isHooked()) embark_hook->unHook();
            return;
        }
        register_script_tutorial_hooks();
        install_savepoint_notice_suppression(false);
        install_keyblade_menu_tutorial_suppression(false);
        install_kupo_coin_tutorial_suppression(false);
        install_synthesis_suppression(false);
        Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] TutorialSuppression installed pre_open=true automatic_close=false\n"));
    }
}
