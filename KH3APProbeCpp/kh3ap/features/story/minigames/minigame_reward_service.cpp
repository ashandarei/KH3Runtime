#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/story/minigames/minigame_reward_service.h"
#include "kh3ap/features/story/minigames/minigame_reward_rules.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/utilities.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/FScriptArray.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <array>
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
using namespace RC;
using namespace RC::Unreal;
namespace kh3ap
{
    namespace
    {
        constexpr auto pooh_result_retry_interval = std::chrono::milliseconds(100);
        constexpr auto max_minigame_parameter_bytes = 256;
        constexpr auto max_pooh_state_entries = 32;
        constexpr auto max_pooh_state_capacity = 64;

        constexpr auto hook_path = L"/Script/TresGame.TresRemunerationStatics:GetRemunerationData";
        constexpr auto state_file = channels::runtime_minigame_rewards_file;
        UObject* pooh_object(UObject* owner, const wchar_t* name)
        {
            if (!is_uobject_valid_fast(owner))
            {
                return nullptr;
            }
            auto* field = owner->GetPropertyByNameInChain(name);
            if (!field || field->GetClass().GetName() != L"ObjectProperty" ||
                field->GetElementSize() != sizeof(UObject*))
            {
                return nullptr;
            }
            auto** value = field->ContainerPtrToValuePtr<UObject*>(owner);
            return value && is_uobject_valid_fast(*value) ? *value : nullptr;
        }
        struct Call
        {
            UObject* owner;
            UFunction* fn;
            std::vector<std::byte> params;
            Call(UObject* object, const wchar_t* name) : owner(object)
            {
                if (!is_uobject_valid_fast(owner))
                {
                    throw std::runtime_error("pooh_owner");
                }
                fn = owner->GetFunctionByNameInChain(name);
                if (!is_uobject_valid_fast(fn) || fn->GetParmsSize() <= 0 ||
                    fn->GetParmsSize() > max_minigame_parameter_bytes)
                {
                    throw std::runtime_error("pooh_function");
                }
                params.resize(fn->GetParmsSize());
                fn->InitializeStruct(params.data());
            }
            ~Call()
            {
                fn->DestroyStruct(params.data());
            }
            Call(const Call&) = delete;
            FProperty* param(const wchar_t* name)
            {
                auto* p = find_function_param_by_name(fn, name);
                if (!p)
                {
                    throw std::runtime_error("pooh_parameter");
                }
                return p;
            }
            void object(const wchar_t* name, UObject* value)
            {
                if (!write_object_function_param(fn, params.data(), name, value))
                {
                    throw std::runtime_error("pooh_object_param");
                }
            }
            void invoke()
            {
                owner->ProcessEvent(fn, params.data());
            }
        };
        template <class T> bool pooh_read(uintptr_t address, T& value) noexcept
        {
            __try
            {
                std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }
        template <class T> T pooh_field(uintptr_t owner, size_t offset)
        {
            T value{};
            if (!owner || !pooh_read(owner + offset, value))
            {
                throw std::runtime_error("pooh_unreadable_state");
            }
            return value;
        }
        int pooh_phase(UObject* mode)
        {
            auto* manager = pooh_object(mode, L"m_pPuzzleManager");
            auto* puzzle = pooh_object(manager, L"m_pPuzzle");
            if (!is_uobject_valid_fast(puzzle))
            {
                return -1;
            }
            auto* puzzle_level = puzzle->GetOuterPrivate();
            auto* mode_level = mode->GetOuterPrivate();
            if (!is_uobject_valid_fast(puzzle_level) || !is_uobject_valid_fast(mode_level) ||
                puzzle_level->GetOuterPrivate() != mode_level->GetOuterPrivate())
            {
                return -1;
            }
            // Each native puzzle subclass ends in its state-machine pointer. Resolve
            // the native class, not the Blueprint's larger generated property size.
            size_t state_offset{};
            auto* type = puzzle->GetClassPrivate();
            for (int depth = 0; depth < 8 && is_uobject_valid_fast(type); ++depth)
            {
                const auto name = type->GetName();
                if (name == L"TresWinniePuzzleVegetable" || name == L"TresWinniePuzzleFruit" ||
                    name == L"TresWinniePuzzleFlower")
                {
                    const auto size = type->GetPropertiesSize();
                    if (size < 0x580 || size > 0x800 || size % sizeof(void*))
                    {
                        throw std::runtime_error("pooh_state_class_layout");
                    }
                    state_offset = size - sizeof(void*);
                    break;
                }
                type = static_cast<UClass*>(type->GetSuperStruct());
            }
            if (!state_offset)
            {
                throw std::runtime_error("pooh_state_class_unavailable");
            }
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const auto pe = base + pooh_field<int32_t>(base, 0x3c);
            const auto stamp = pooh_field<uint32_t>(pe, 8);
            const auto image_size = pooh_field<uint32_t>(pe, 24 + 56);
            if (kh3ap::native_build::identify(stamp, image_size) ==
                kh3ap::native_build::Build::Unsupported)
            {
                throw std::runtime_error("pooh_state_unsupported_build");
            }
            const auto object = reinterpret_cast<uintptr_t>(puzzle);
            const auto machine = pooh_field<uintptr_t>(object, state_offset);
            if (!machine)
            {
                return -1; // Native BeginPlay has not constructed states yet.
            }
            const auto table = pooh_field<uintptr_t>(machine, 0);
            const auto first = pooh_field<uintptr_t>(table, 0);
            if (table < base || table >= base + image_size || first < base ||
                first >= base + image_size)
            {
                throw std::runtime_error("pooh_state_machine_vtable");
            }
            const auto data = pooh_field<uintptr_t>(machine, 8);
            const auto count = pooh_field<int32_t>(machine, 0x10);
            const auto capacity = pooh_field<int32_t>(machine, 0x14);
            const auto active = pooh_field<uintptr_t>(machine, 0x30);
            if (!active)
            {
                return -1;
            }
            if (!data || count < 1 || count > max_pooh_state_entries || capacity < count ||
                capacity > max_pooh_state_capacity)
            {
                throw std::runtime_error("pooh_state_catalog_bounds");
            }
            bool active_found = false;
            uint32_t seen{};
            for (int i = 0; i < count; ++i)
            {
                const auto state = pooh_field<uintptr_t>(data, size_t(i) * sizeof(void*));
                const auto id = pooh_field<int32_t>(state, 0x18);
                const auto vtable = pooh_field<uintptr_t>(state, 0);
                if (pooh_field<uintptr_t>(state, 8) != object || id < 0 ||
                    id > pooh_reward::result_phase || seen & (1u << id) || vtable < base ||
                    vtable >= base + image_size)
                {
                    throw std::runtime_error("pooh_state_catalog_identity");
                }
                seen |= 1u << id;
                active_found |= state == active;
            }
            if (!active_found || !(seen & (1u << pooh_reward::result_phase)))
            {
                throw std::runtime_error("pooh_active_state_not_in_catalog");
            }
            const int phase = pooh_field<int32_t>(active, 0x18);
            if (pooh_field<uintptr_t>(object, state_offset) != machine ||
                pooh_field<uintptr_t>(machine, 0x30) != active)
            {
                return -1;
            }
            return phase;
        }
        UObject* library()
        {
            return UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, L"/Script/TresGame.Default__TresGameBlueprintLibrary");
        }
        UObject* active_world()
        {
            auto* engine = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, L"/Engine/Transient.TresGameEngine_0");
            auto* viewport = pooh_object(engine, L"GameViewport");
            return pooh_object(viewport, L"World");
        }
        bool current_sora(UObject* sora)
        {
            if (!is_uobject_valid_fast(sora))
            {
                return false;
            }
            auto* level = sora->GetOuterPrivate();
            if (!is_uobject_valid_fast(level) || level->GetOuterPrivate() != active_world())
            {
                return false;
            }
            auto* controller = pooh_object(sora, L"Controller");
            return is_uobject_valid_fast(controller) && pooh_object(controller, L"Pawn") == sora;
        }
        int panaceas(UObject* sora)
        {
            Call count(library(), L"BP_TresGetBackyardItemNum");
            count.object(L"WorldContextObject", sora);
            if (!write_name_or_string_function_param(count.param(L"InItemKeyName"),
                                                     count.params.data(), L"ITEM_ALLCURE"))
            {
                throw std::runtime_error("pooh_inventory_token");
            }
            auto* result = count.param(L"ReturnValue");
            if (result->GetClass().GetName() != L"IntProperty" ||
                result->GetElementSize() != sizeof(int32_t))
            {
                throw std::runtime_error("pooh_inventory_count");
            }
            count.invoke();
            return *result->ContainerPtrToValuePtr<int32_t>(count.params.data());
        }
        bool pay_panaceas(UObject* sora)
        {
            const int before = panaceas(sora);
            if (before < 0 || before > pooh_reward::panacea_stack_limit)
            {
                return false;
            }
            if (before == pooh_reward::panacea_stack_limit)
            {
                return true;
            }
            Call grant(library(), L"BP_TresGetItem");
            grant.object(L"WorldContextObject", sora);
            if (!write_name_or_string_function_param(grant.param(L"InItemKeyName"),
                                                     grant.params.data(), L"ITEM_ALLCURE") ||
                !write_integral_function_param(grant.param(L"InNum"), grant.params.data(),
                                               pooh_reward::panacea_stack_limit) ||
                !write_bool_function_param(grant.param(L"bShowInfo"), grant.params.data(), true))
            {
                return false;
            }
            const RuntimeGrantInternalCallDepth::Scope internal;
            grant.invoke();
            // Consume the pending bonus after dispatch even if readback fails. Retrying
            // a void grant could refill stock again after the player sold the first stack.
            try
            {
                const int after = panaceas(sora);
                if (after == pooh_reward::panacea_stack_limit)
                {
                    Output::send<LogLevel::Normal>(
                        STR("[KH3APProbeCpp] Pooh Panacea bonus before={} after={}\n"), before,
                        after);
                }
                else
                {
                    Output::send<LogLevel::Error>(STR("[KH3APProbeCpp] Pooh Panacea bonus readback "
                                                      "mismatch before={} after={} expected={}\n"),
                                                  before, after, pooh_reward::panacea_stack_limit);
                }
            }
            catch (...)
            {
                Output::send<LogLevel::Error>(STR(
                    "[KH3APProbeCpp] Pooh Panacea bonus dispatched; inventory readback failed\n"));
            }
            return true;
        }
    } // namespace
    struct RuntimeMinigameRewardService::Impl
    {
        using Thunk = void (*)(UObject*, FFrame&, void*);
        inline static Impl* instance{};
        std::function<std::optional<std::filesystem::path>()> bridge_dir;
        std::function<UObject*()> playable_sora;
        FWeakObjectPtr pooh_mode;
        std::array<uint64_t, 3> pooh_class_names{};
        PoohPanaceaBonus pooh_bonus;
        std::optional<std::filesystem::path> pooh_bridge;
        int pooh_ui_state{-1};
        bool pooh_error_logged{};
        std::chrono::steady_clock::time_point next_pooh_poll{};
        Thunk original{};
        int crc_offset{}, array_offset{};
        struct Reward
        {
            std::wstring key;
            std::vector<std::wstring> tokens;
        };
        std::map<uint32_t, Reward> rewards;
        std::filesystem::path bridge;
        std::wstring seed;
        int64_t slot{};
        std::set<std::wstring> completed;
        explicit Impl(decltype(bridge_dir) dir, decltype(playable_sora) pawn)
            : bridge_dir(std::move(dir)), playable_sora(std::move(pawn))
        {
        }
        void tick_pooh()
        {
            const auto now = std::chrono::steady_clock::now();
            if (now < next_pooh_poll)
            {
                return;
            }
            next_pooh_poll = now + pooh_result_retry_interval;
            // Minigames possess a puzzle pawn. Wait for the ordinary, verified Sora
            // context before granting inventory; never use the result/UI/game mode.
            auto* sora = pooh_bonus.pending && playable_sora ? playable_sora() : nullptr;
            if (pooh_bridge && pooh_bridge == bridge_dir() && current_sora(sora) &&
                pay_panaceas(sora))
            {
                pooh_bonus.paid();
                pooh_bonus.leave_game();
                pooh_mode = FWeakObjectPtr{};
                return;
            }
            const auto current_bridge = bridge_dir();
            if (pooh_bridge && pooh_bridge != current_bridge)
            {
                pooh_bonus = {};
                pooh_mode = FWeakObjectPtr{};
                pooh_bridge.reset();
                return;
            }
            if (!current_bridge)
            {
                return;
            }
            // Follow the active world's authority, as the passive inspector does.
            // Constructor notifications can precede map/bridge initialization.
            auto* mode = pooh_object(active_world(), L"AuthorityGameMode");
            if (!is_uobject_valid_fast(mode))
            {
                return;
            }
            uint64_t bits{};
            const auto& name = mode->GetClassPrivate()->GetNamePrivate();
            std::memcpy(&bits, &name, sizeof(bits));
            if (std::find(pooh_class_names.begin(), pooh_class_names.end(), bits) ==
                pooh_class_names.end())
            {
                return;
            }
            if (pooh_mode.Get() != mode)
            {
                pooh_mode = make_uobject_weak(mode);
                pooh_bonus.leave_game();
                pooh_bridge = current_bridge;
                pooh_ui_state = -1;
                pooh_error_logged = false;
                Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Pooh minigame reward tracking started\n"));
            }
            if (is_uobject_valid_fast(mode) && pooh_bridge)
            {
                const int phase = pooh_phase(mode);
                if (phase >= 0)
                {
                    if (phase != pooh_ui_state)
                    {
                        pooh_ui_state = phase;
                        Output::send<LogLevel::Normal>(
                            STR("[KH3APProbeCpp] Pooh reward native_phase={}\n"), phase);
                    }
                    const bool was_pending = pooh_bonus.pending;
                    pooh_bonus.observe_phase(phase);
                    if (!was_pending && pooh_bonus.pending)
                    {
                        Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Pooh Panacea bonus "
                                                           "earned; waiting for playable Sora\n"));
                    }
                }
            }
        }
        ~Impl()
        {
            if (original)
            {
                auto* fn = UObjectGlobals::StaticFindObject<UFunction*>(UFunction::StaticClass(),
                                                                        nullptr, hook_path);
                if (is_uobject_valid_fast(fn) && fn->GetFuncPtr() == thunk)
                {
                    fn->SetFuncPtr(original);
                }
            }
            if (instance == this)
            {
                instance = nullptr;
            }
        }
        bool scope()
        {
            const auto dir = bridge_dir();
            if (!dir)
            {
                return false;
            }
            if (*dir == bridge)
            {
                return !rewards.empty();
            }
            auto document = JsonObject::read_seed(*dir / "slot_data.json");
            if (!document.valid())
            {
                return false;
            }
            const auto identity = document.object_field("kh3_seed");
            auto name = identity.string_field("seed_name").value_or(L"");
            auto player = identity.int64_field("slot").value_or(0);
            if (name.empty() || player < 1)
            {
                return false;
            }
            rewards.clear();
            completed.clear();
            bridge = *dir;
            seed = name;
            slot = player;
            for (const auto& row : document.object_field("bridge").objects_field("locations"))
            {
                if (row.string_field("type").value_or(L"") != L"minigame_reward")
                {
                    continue;
                }
                auto key = row.string_field("source_id").value_or(L"");
                if (minigame_reward_owner(key).empty())
                {
                    continue;
                }
                const auto crc = minigame_reward_crc(key);
                if (row.int64_field("minigame_reward_crc").value_or(-1) != crc)
                {
                    continue;
                }
                auto tokens = row.string_array_field("minigame_reward_tokens");
                if (tokens.empty() || tokens.size() > 8)
                {
                    continue;
                }
                rewards.emplace(crc, Reward{key, std::move(tokens)});
            }
            auto previous = read_text_file(bridge / state_file);
            if (previous &&
                JsonFieldReader::string_field(*previous, "schema").value_or(L"") ==
                    L"kh3ap.minigame_rewards.v1" &&
                JsonFieldReader::string_field(*previous, "seed_name").value_or(L"") == seed &&
                JsonFieldReader::int64_field(*previous, "slot").value_or(0) == slot &&
                JsonFieldReader::string_field(*previous, "source").value_or(L"") ==
                    L"remuneration_reward")
            {
                for (auto& key :
                     JsonFieldReader::string_array_field(*previous, "completed_rewards"))
                {
                    if (auto it = rewards.find(minigame_reward_crc(key));
                        it != rewards.end() && it->second.key == key)
                    {
                        completed.insert(key);
                    }
                }
            }
            return !rewards.empty();
        }
        bool persist()
        {
            std::ostringstream json;
            json << "{\"schema\":\"kh3ap.minigame_rewards.v1\",\"source\":\"remuneration_reward\","
                    "\"seed_name\":\""
                 << json_escape(seed) << "\",\"slot\":" << slot << ",\"completed_rewards\":[";
            bool first = true;
            for (auto& key : completed)
            {
                if (!first)
                {
                    json << ',';
                }
                first = false;
                json << '"' << json_escape(key) << '"';
            }
            json << "]}\n";
            return write_text_file_atomic_verified(bridge / state_file, json.str());
        }
        static std::vector<std::wstring> callers(FFrame& stack)
        {
            std::vector<std::wstring> names;
            auto* frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
            for (int depth = 0; depth < 4 && frame; ++depth)
            {
                std::wstring name;
                if (try_get_uobject_full_name_fast(frame->Object, name))
                {
                    names.push_back(std::move(name));
                }
                if (frame->PreviousFrame == frame)
                {
                    break;
                }
                frame = frame->PreviousFrame;
            }
            return names;
        }
        static void thunk(UObject* context, FFrame& stack, void* result)
        {
            // Capture the Blueprint caller before the native thunk advances the VM.
            std::vector<std::wstring> owners;
            try
            {
                if (!RuntimeGrantInternalCallDepth::is_active())
                {
                    owners = callers(stack);
                }
            }
            catch (...)
            {
            }
            instance->original(context, stack, result);
            try
            {
                instance->award(owners, result);
            }
            catch (...)
            {
            }
        }
        void award(const std::vector<std::wstring>& owners, void* result)
        {
            if (!result || owners.empty() || !scope())
            {
                return;
            }
            auto* bytes = static_cast<std::byte*>(result);
            const auto it = rewards.find(*reinterpret_cast<uint32_t*>(bytes + crc_offset));
            if (it == rewards.end())
            {
                return;
            }
            const auto& reward = it->second;
            bool matched = false;
            for (auto& name : owners)
            {
                if (minigame_reward_caller_matches(reward.key, name))
                {
                    matched = true;
                    break;
                }
            }
            if (!matched)
            {
                return;
            }
            auto* array = reinterpret_cast<FScriptArray*>(bytes + array_offset);
            if (array->Num() != reward.tokens.size() || !array->GetData())
            {
                return;
            }
            auto* items = static_cast<FName*>(array->GetData());
            for (int i = 0; i < array->Num(); ++i)
            {
                if (items[i].ToString() != reward.tokens[i])
                {
                    return;
                }
            }
            // These specific Blueprint branches select prizes after scoring. Record
            // only that selection, never a global item grant or a table inspection.
            // Persist before suppressing the vanilla reward so an I/O error cannot
            // silently consume a check. Repeated tiers stay suppressed after reload.
            const bool added = completed.insert(reward.key).second;
            if (added && !persist())
            {
                completed.erase(reward.key);
                return;
            }
            for (int i = 0; i < array->Num(); ++i)
            {
                items[i] = FName{};
            }
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Minigame reward={} new_check={} native_prize=suppressed\n"),
                reward.key, added);
        }
        void install_once()
        {
            if (original)
            {
                return;
            }
            constexpr const wchar_t* names[]{L"TresWinnieGameModeFlower_C",
                                             L"TresWinnieGameModeFruit_C",
                                             L"TresWinnieGameModeVegetable_C"};
            for (size_t i = 0; i < pooh_class_names.size(); ++i)
            {
                const FName name{names[i], FNAME_Add};
                std::memcpy(&pooh_class_names[i], &name, sizeof(uint64_t));
            }
            auto* fn = UObjectGlobals::StaticFindObject<UFunction*>(UFunction::StaticClass(),
                                                                    nullptr, hook_path);
            if (!is_uobject_valid_fast(fn) || !(fn->GetFunctionFlags() & FUNC_Native) ||
                !fn->GetFuncPtr())
            {
                return;
            }
            auto* ret = find_function_param_by_name(fn, L"ReturnValue");
            if (!ret || ret->GetClass().GetName() != L"StructProperty")
            {
                return;
            }
            auto* structure = static_cast<FStructProperty*>(ret)->GetStruct();
            if (!is_uobject_valid_fast(structure) ||
                structure->GetName() != L"TresRemunerationData")
            {
                return;
            }
            auto* crc = find_struct_field_by_name(structure, L"Crc");
            auto* array = find_struct_field_by_name(structure, L"ItemKeyArray");
            if (!crc || crc->GetClass().GetName() != L"UInt32Property" ||
                crc->GetElementSize() != sizeof(uint32_t) || !array ||
                array->GetClass().GetName() != L"ArrayProperty" ||
                array->GetElementSize() != sizeof(FScriptArray))
            {
                return;
            }
            auto* inner = static_cast<FArrayProperty*>(array)->GetInner();
            if (!inner || inner->GetClass().GetName() != L"NameProperty" ||
                inner->GetElementSize() != sizeof(FName))
            {
                return;
            }
            crc_offset = crc->GetOffset_Internal();
            array_offset = array->GetOffset_Internal();
            if (crc_offset < 0 || array_offset < 0 ||
                crc_offset + sizeof(uint32_t) > ret->GetElementSize() ||
                array_offset + sizeof(FScriptArray) > ret->GetElementSize())
            {
                return;
            }
            original = fn->GetFuncPtr();
            instance = this;
            fn->SetFuncPtr(thunk);
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Minigame item reward hook installed mode=event_driven\n"));
        }
    };
    RuntimeMinigameRewardService::RuntimeMinigameRewardService(
        std::function<std::optional<std::filesystem::path>()> dir, std::function<UObject*()> pawn)
        : impl_(std::make_unique<Impl>(std::move(dir), std::move(pawn)))
    {
    }
    RuntimeMinigameRewardService::~RuntimeMinigameRewardService() = default;
    void RuntimeMinigameRewardService::install_once()
    {
        impl_->install_once();
    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    UObject* RuntimeMinigameRewardService::diagnostic_controller()
    {
        auto* impl = Impl::instance;
        if (!impl || !impl->pooh_bridge || impl->pooh_bridge != impl->bridge_dir())
        {
            return nullptr;
        }
        auto* mode = impl->pooh_mode.Get();
        if (!is_uobject_valid_fast(mode))
        {
            return nullptr;
        }
        auto* level = mode->GetOuterPrivate();
        if (!is_uobject_valid_fast(level) || level->GetOuterPrivate() != active_world())
        {
            return nullptr;
        }
        auto* pawn = pooh_object(mode, L"m_pPlayerPawn");
        auto* controller = pooh_object(pawn, L"Controller");
        return is_uobject_valid_fast(pawn) && is_uobject_valid_fast(controller) &&
                       pooh_object(controller, L"Pawn") == pawn
                   ? controller
                   : nullptr;
    }
#endif
    void RuntimeMinigameRewardService::tick()
    {
        try
        {
            impl_->tick_pooh();
        }
        catch (const std::exception& error)
        {
            if (!impl_->pooh_error_logged)
            {
                impl_->pooh_error_logged = true;
                const std::string reason = error.what();
                Output::send<LogLevel::Error>(
                    STR("[KH3APProbeCpp] Pooh reward observation failed: {}\n"),
                    std::wstring(reason.begin(), reason.end()));
            }
        }
        catch (...)
        {
        }
    }
} // namespace kh3ap
