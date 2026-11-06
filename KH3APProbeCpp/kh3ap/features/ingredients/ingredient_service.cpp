#include <memory>
#include <optional>
#include <utility>
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#ifdef S
#pragma push_macro("S")
#undef S
#define KH3AP_INGREDIENT_RESTORE_S
#endif
#include <polyhook2/Detour/x64Detour.hpp>
#ifdef KH3AP_INGREDIENT_RESTORE_S
#pragma pop_macro("S")
#undef KH3AP_INGREDIENT_RESTORE_S
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstring>
#include "kh3ap/features/ingredients/ingredient_service.h"
#include "kh3ap/features/ingredients/ingredient_checkpoint_writer.h"
#include "kh3ap/bridge/transport/checkpoint_file.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/performance_build.h"
#include "kh3ap/features/ingredients/ingredient_tracker.h"
#include "kh3ap/features/ingredients/ingredient_spawn_rates.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/utilities.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/Script.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <algorithm>
#include <cmath>
#include <array>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
using namespace RC;
using namespace RC::Unreal;
namespace kh3ap
{
    namespace
    {
        constexpr auto physical_prize_kind_offset = 0x40c;
        constexpr auto item_box_prize_kind = 15;
        constexpr auto max_ingredient_location_parameter_bytes = 128;
        constexpr auto ingredient_prize_parameter_count = 3;
        constexpr auto ingredient_release_parameter_count = 2;

        constexpr auto max_ingredient_event_frame_depth = 4;
        constexpr auto max_ingredient_actor_candidates = 4096;
        constexpr auto max_ingredient_call_parameter_bytes = 128;

        constexpr auto state_file = channels::runtime_ingredient_state_file;
        constexpr auto food_hook =
            L"/Script/TresGame.TresGameBlueprintLibrary:BP_FoodstuffGetTeamPrize";
        constexpr auto gimmick_hook =
            L"/Script/TresGame.TresGimmickActor:BP_GetFoodstuffPrizeToTeam";
        constexpr auto release_hook =
            L"/Script/TresGame.TresPrizeGenComponent:BP_SpawnFoodstuffPrize";
        struct IngredientMarkerRegistry final : FUObjectDeleteListener
        {
            struct Pending
            {
                UObject* object;
                FUObjectItem* slot;
            };
            std::unordered_set<uint64_t> names;
            std::unordered_map<const UObjectBase*, Pending> pending;
            std::unordered_map<const UObjectBase*, FWeakObjectPtr> actors;
            std::mutex mutex;
            std::atomic<bool> enabled{};
            uint64_t revision{};
            static auto name_bits(const FName& name) -> uint64_t
            {
                static_assert(sizeof(FName) == sizeof(uint64_t));
                uint64_t value{};
                std::memcpy(&value, &name, sizeof(value));
                return value;
            }
            void initialize()
            {
                if (enabled.load(std::memory_order_acquire))
                {
                    return;
                }
                for (const auto& spot : ingredient_spot_actors)
                {
                    const auto leaf = spot.path.substr(spot.path.rfind(L'.') + 1);
                    names.insert(name_bits(FName{std::wstring(leaf).c_str(), FNAME_Add}));
                }
                UObjectArray::AddUObjectDeleteListener(this);
                enabled.store(true, std::memory_order_release);
            }
            void constructed(UObject* object)
            {
                if (!enabled.load(std::memory_order_acquire) || !object ||
                    !names.contains(name_bits(object->GetNamePrivate())))
                {
                    return;
                }
                const std::lock_guard lock{mutex};
                if (!pending.contains(object) && !actors.contains(object))
                {
                    if (auto* slot = object->GetObjectItem())
                    {
                        pending.emplace(object, Pending{object, slot});
                        ++revision;
                    }
                }
            }
            void NotifyUObjectDeleted(const UObjectBase* object, int32) override
            {
                const std::lock_guard lock{mutex};
                if (pending.erase(object) + actors.erase(object))
                {
                    ++revision;
                }
            }
            void OnUObjectArrayShutdown() override
            {
                enabled.store(false, std::memory_order_release);
                UObjectArray::RemoveUObjectDeleteListener(this);
                const std::lock_guard lock{mutex};
                pending.clear();
                actors.clear();
            }
            auto snapshot() -> std::pair<uint64_t, std::vector<UObject*>>
            {
                const std::lock_guard lock{mutex};
                constexpr auto loading = static_cast<EObjectFlags>(RF_NeedLoad | RF_NeedPostLoad |
                                                                   RF_NeedPostLoadSubobjects);
                std::erase_if(pending,
                              [&](const auto& pair)
                              {
                                  const auto& entry = pair.second;
                                  if (entry.slot->GetUObject() != entry.object ||
                                      !is_uobject_valid_fast(entry.object))
                                  {
                                      return true;
                                  }
                                  if (entry.object->HasAnyFlags(loading))
                                  {
                                      return false;
                                  }
                                  const auto weak = make_uobject_weak(entry.object);
                                  if (weak.Get() != entry.object)
                                  {
                                      return false;
                                  }
                                  actors.emplace(pair.first, weak);
                                  ++revision;
                                  return true;
                              });
                std::vector<UObject*> result;
                result.reserve(actors.size());
                std::erase_if(actors,
                              [&](const auto& pair)
                              {
                                  auto* actor = pair.second.Get();
                                  if (!is_uobject_valid_fast(actor))
                                  {
                                      ++revision;
                                      return true;
                                  }
                                  result.push_back(actor);
                                  return false;
                              });
                std::ranges::sort(result, {},
                                  [](UObject* o)
                                  {
                                      return o->GetInternalIndex();
                                  });
                return {revision, std::move(result)};
            }
        };
        // Process lifetime: the shared create observer never calls a destroyed service.
        auto ingredient_markers() -> IngredientMarkerRegistry&
        {
            static auto* value = new IngredientMarkerRegistry;
            return *value;
        }
    } // namespace
    auto notify_ingredient_marker_created(UObject* object) -> void
    {
        ingredient_markers().constructed(object);
    }
    struct RuntimeIngredientService::Impl
    {
        RuntimeIngredientConfig config;
        using Thunk = void (*)(UObject*, FFrame&, void*);
        inline static Impl* instance{};
        Thunk original_food{}, original_gimmick{}, original_release{};
        bool installed{};
        std::unique_ptr<PLH::x64Detour> collection_hook;
        uint64_t collection_original{};
        struct Snapshot
        {
            FWeakObjectPtr pawn;
            IngredientCounts counts;
            std::wstring spot;
        };
        std::filesystem::path bridge;
        std::wstring seed;
        int64_t slot{};
        std::set<std::wstring> acquired, acquired_spots;
        struct MapSpot
        {
            FWeakObjectPtr actor;
            std::wstring key;
        };
        std::vector<MapSpot> map_spots;
        FWeakObjectPtr map_pawn;
        uint64_t marker_revision{UINT64_MAX};
        int event_depth{};
        struct EventDepth
        {
            int& depth;
            explicit EventDepth(int& d) : depth(d)
            {
                ++depth;
            }
            ~EventDepth()
            {
                --depth;
            }
        };
        int64_t last_pickup_sequence{};
        std::wstring last_pickup_spot, last_pickup_at;
        bool dirty{}, active{};
        uint64_t journal_revision{};
        uint64_t checkpoint_scope{}, checkpoint_revision{};
        bool checkpoint_failed{}, stopping{};
        std::shared_ptr<const IngredientCheckpointWriter::Checkpoint> publication;
        IngredientCheckpointWriter checkpoints{write_checkpoint_file_atomic_verified};
        explicit Impl(RuntimeIngredientConfig c) : config(std::move(c))
        {
        }
        ~Impl()
        {
            if (collection_hook)
            {
                collection_hook->unHook();
            }
            // Native reflected functions can lack a usable weak serial. Resolve
            // their stable paths again instead of retaining raw UObject pointers.
            if (installed)
            {
                auto* food = UObjectGlobals::StaticFindObject<UFunction*>(UFunction::StaticClass(),
                                                                          nullptr, food_hook);
                auto* gimmick = UObjectGlobals::StaticFindObject<UFunction*>(
                    UFunction::StaticClass(), nullptr, gimmick_hook);
                auto* release = UObjectGlobals::StaticFindObject<UFunction*>(
                    UFunction::StaticClass(), nullptr, release_hook);
                if (is_uobject_valid_fast(food) && food->GetFuncPtr() == food_thunk)
                {
                    food->SetFuncPtr(original_food);
                }
                if (is_uobject_valid_fast(gimmick) && gimmick->GetFuncPtr() == gimmick_thunk)
                {
                    gimmick->SetFuncPtr(original_gimmick);
                }
                if (is_uobject_valid_fast(release) && release->GetFuncPtr() == release_thunk)
                {
                    release->SetFuncPtr(original_release);
                }
            }
            if (instance == this)
            {
                instance = nullptr;
            }
            shutdown();
        }
        static bool collection_thunk(UObject* prize, bool remove)
        {
            const RuntimeHotPathProfileScope profile{L"ingredients:physical_collection"};
            auto& self = *instance;
            using Collect = bool (*)(UObject*, bool);
            auto original = reinterpret_cast<Collect>(self.collection_original);
            if (self.event_depth || RuntimeGrantInternalCallDepth::is_active())
            {
                return original(prize, remove);
            }
            EventDepth depth(self.event_depth);
            Snapshot snapshot{};
            snapshot.counts.fill(-1);
            int kind = -1;
            try
            {
                if (self.scope() && is_uobject_valid_fast(prize))
                {
                    auto* property = prize->GetPropertyByNameInChain(L"m_Kind");
                    if (property && property->GetOffset_Internal() == physical_prize_kind_offset &&
                        property->GetElementSize() == sizeof(int32_t))
                    {
                        kind = *property->ContainerPtrToValuePtr<int>(prize);
                        // Kind 15 is the item-box award branch in both verified
                        // native routines. Munny/HP/MP prizes need no stock reads.
                        if (kind == item_box_prize_kind)
                        {
                            auto* pawn = self.config.inventory_context();
                            if (auto values = self.counts(pawn))
                            {
                                snapshot.pawn = pawn;
                                snapshot.counts = *values;
                            }
                        }
                    }
                }
            }
            catch (...)
            {
            }
            const auto result = original(prize, remove);
            try
            {
                self.after(snapshot, 0);
                if (KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS && kind == item_box_prize_kind)
                {
                    Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Ingredient physical_prize kind={} result={}\n"), kind,
                        result);
                }
            }
            catch (...)
            {
            }
            return result;
        }
        void install_collection()
        {
            // TresPrize::collect awards the actual prize through the recipient's
            // native prize method before destroying it. Containers only spawn it.
            // ABI: bool(TresPrize*, bool). Both shipping images share this prologue
            // and the m_Kind/m_pGetter layout; unknown images fail closed.
            auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
            auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
            {
                return;
            }
            auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE ||
                nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            {
                return;
            }
            uintptr_t rva{}, end_rva{};
            if (kh3ap::native_build::egs.matches(nt->FileHeader.TimeDateStamp,
                                                 nt->OptionalHeader.SizeOfImage))
            {
                rva = 0x17aad90;
                end_rva = 0x17aaee2;
            }
            else if (kh3ap::native_build::steam.matches(nt->FileHeader.TimeDateStamp,
                                                        nt->OptionalHeader.SizeOfImage))
            {
                rva = 0x1479360;
                end_rva = 0x14794b2;
            }
            constexpr uint8_t signature[]{
                0x41, 0x56, 0x48, 0x83, 0xec, 0x40, 0x48, 0xc7, 0x44, 0x24, 0x20, 0xfe, 0xff,
                0xff, 0xff, 0x48, 0x89, 0x5c, 0x24, 0x50, 0x48, 0x89, 0x74, 0x24, 0x58, 0x48,
                0x89, 0x7c, 0x24, 0x60, 0x44, 0x0f, 0xb6, 0xf2, 0x48, 0x8b, 0xf9};
            if (!rva || std::memcmp(base + rva, signature, sizeof(signature)) != 0)
            {
                Output::send<LogLevel::Warning>(STR(
                    "[KH3APProbeCpp] Ingredient physical-prize hook rejected unsupported image\n"));
                return;
            }
            DWORD64 image_base{};
            auto* unwind =
                RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(base + rva), &image_base, nullptr);
            if (!unwind || image_base != reinterpret_cast<DWORD64>(base) ||
                unwind->BeginAddress != rva || unwind->EndAddress != end_rva)
            {
                return;
            }
            collection_hook = std::make_unique<PLH::x64Detour>(
                reinterpret_cast<uint64_t>(base + rva),
                reinterpret_cast<uint64_t>(collection_thunk), &collection_original);
            if (!collection_hook->hook())
            {
                collection_hook.reset();
                return;
            }
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Ingredient physical-prize collection hook installed\n"));
        }
        static void food_thunk(UObject* context, FFrame& stack, void* result)
        {
            instance->dispatch(instance->original_food, context, stack, result);
        }
        static void gimmick_thunk(UObject* context, FFrame& stack, void* result)
        {
            instance->dispatch(instance->original_gimmick, context, stack, result);
        }
        static void release_thunk(UObject* context, FFrame& stack, void* result)
        {
            // Breakable food points release physical prizes instead of directly
            // incrementing stock. Their native food-loot release is the location
            // event; ordinary prize/munny spawns and shops never enter this hook.
            if (instance->event_depth)
            {
                instance->original_release(context, stack, result);
                return;
            }
            EventDepth depth(instance->event_depth);
            std::wstring spot;
            try
            {
                if (!RuntimeGrantInternalCallDepth::is_active() && instance->scope())
                {
                    spot = event_spot(context, stack);
                }
            }
            catch (...)
            {
            }
            instance->original_release(context, stack, result);
            try
            {
                if (!spot.empty())
                {
                    const bool added = instance->acquired_spots.insert(spot).second;
                    instance->dirty = instance->dirty || added;
                    if (added || KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS)
                    {
                        Output::send<LogLevel::Warning>(
                            STR("[KH3APProbeCpp] Ingredient event=food_loot_release spot={} "
                                "new_spot={} mode=event_driven\n"),
                            spot, added);
                    }
                    instance->record_spot(spot);
                }
            }
            catch (...)
            {
            }
        }
        void dispatch(Thunk original, UObject* context, FFrame& stack, void* result)
        {
            // Nested Blueprint/native wrappers describe one pickup. Only the outer
            // event snapshots stock and records the pickup.
            if (event_depth)
            {
                original(context, stack, result);
                return;
            }
            const RuntimeHotPathProfileScope profile{L"ingredients:field_pickup"};
            EventDepth depth(event_depth);
            const auto start = std::chrono::steady_clock::now();
            Snapshot snapshot{};
            snapshot.counts.fill(-1);
            try
            {
                snapshot = before(context, stack);
            }
            catch (...)
            {
            }
            // Preserve the vanilla thunk, stack and return storage exactly. Direct
            // binding also sees Blueprint VM calls missed by UE4SS RegisterHook.
            {
                const RuntimeHotPathProfileScope original_profile{
                    L"ingredients:vanilla_field_prize"};
                original(context, stack, result);
            }
            try
            {
                after(snapshot, std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - start)
                                    .count());
            }
            catch (...)
            {
            }
        }
        bool scope()
        {
            auto dir = config.bridge_dir();
            if (!dir)
            {
                return false;
            }
            // Generated metadata is immutable within a seed-specific bridge.
            // Resolve it only on the first pickup event or a bridge change.
            if (*dir == bridge)
            {
                return active;
            }
            auto data = read_text_file(*dir / "slot_data.json");
            if (!data)
            {
                return false;
            }
            const auto identity = JsonObject{*data}.object_field("kh3_seed");
            auto name = identity.string_field("seed_name").value_or(L"");
            auto player = identity.int64_field("slot").value_or(0);
            if (name.empty() || player < 1)
            {
                return false;
            }
            // The area map needs first-pickup history even when Ingredientsanity
            // is off. This only records field events; AP location enablement still
            // belongs to the client/seed options. No inventory polling is added.
            active = true;
            if (bridge != *dir || seed != name || slot != player)
            {
                ++checkpoint_scope;
                publication.reset();
                checkpoint_failed = false;
                bridge = *dir;
                seed = name;
                slot = player;
                acquired.clear();
                acquired_spots.clear();
                dirty = false;
                ++journal_revision;
                map_spots.clear();
                map_pawn = nullptr;
                last_pickup_sequence = 0;
                last_pickup_spot.clear();
                last_pickup_at.clear();
                auto previous = read_text_file(bridge / state_file);
                if (previous &&
                    JsonFieldReader::string_field(*previous, "schema").value_or(L"") ==
                        L"kh3ap.runtime_ingredient_state.v1" &&
                    JsonFieldReader::string_field(*previous, "seed_name").value_or(L"") == seed &&
                    JsonFieldReader::int64_field(*previous, "slot").value_or(0) == slot &&
                    JsonFieldReader::string_field(*previous, "source").value_or(L"") ==
                        L"food_prize")
                {
                    for (auto& key :
                         JsonFieldReader::string_array_field(*previous, "acquired_ingredients"))
                    {
                        if (valid_ingredient_key(key))
                        {
                            acquired.insert(key);
                        }
                    }
                    for (auto& key :
                         JsonFieldReader::string_array_field(*previous, "acquired_spots"))
                    {
                        if (valid_ingredient_spot_key(key))
                        {
                            acquired_spots.insert(key);
                        }
                    }
                    last_pickup_sequence =
                        JsonFieldReader::int64_field(*previous, "last_pickup_sequence").value_or(0);
                    last_pickup_spot =
                        JsonFieldReader::string_field(*previous, "last_pickup_spot").value_or(L"");
                    last_pickup_at = JsonFieldReader::string_field(*previous, "last_pickup_at_utc")
                                         .value_or(L"");
                }
            }
            return active;
        }
        auto uncollected_markers(bool rediscover, std::vector<RuntimeIngredientMarker>& markers)
            -> void
        {
            markers.clear();
            if (!scope())
            {
                return;
            }
            auto* pawn = config.inventory_context();
            if (!is_uobject_valid_fast(pawn))
            {
                return;
            }
            auto& registry = ingredient_markers();
            registry.initialize();
            const bool new_pawn = map_pawn.Get() != pawn;
            if (new_pawn)
            {
                map_spots.clear();
                map_pawn = pawn;
                marker_revision = UINT64_MAX;
                auto* klass = find_loaded_runtime_asset_class(L"/Script/TresGame.TresGimmickActor");
                const auto actors =
                    find_live_actors_of_class(pawn, klass, max_ingredient_actor_candidates);
                if (actors.size() > max_ingredient_actor_candidates)
                {
                    return;
                }
                for (auto* actor : actors)
                {
                    registry.constructed(actor);
                }
            }
            auto [revision, actors] = registry.snapshot();
            if (revision != marker_revision)
            {
                map_spots.clear();
                marker_revision = revision;
                auto* level = pawn->GetOuterPrivate();
                auto* world = is_uobject_valid_fast(level) ? level->GetOuterPrivate() : nullptr;
                for (auto* actor : actors)
                {
                    if (!is_uobject_valid_fast(actor))
                    {
                        continue;
                    }
                    auto* actor_level = actor->GetOuterPrivate();
                    if (!is_uobject_valid_fast(actor_level) ||
                        try_read_reflected_object_property_value(actor_level, L"OwningWorld") !=
                            world)
                    {
                        continue;
                    }
                    auto key = spot_for_object(actor);
                    if (key.empty())
                    {
                        continue;
                    }
                    if (std::binary_search(ingredient_chance_spots.begin(),
                                           ingredient_chance_spots.end(), std::wstring_view{key}))
                    {
                        continue;
                    }
                    MapSpot spot;
                    spot.actor = actor;
                    spot.key = std::move(key);
                    map_spots.push_back(std::move(spot));
                }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Ingredient marker registry refresh revision={} loaded={} "
                        "bootstrap={} mode=event_driven\n"),
                    revision, map_spots.size(), new_pawn);
#endif
            }
            std::set<std::wstring> seen;
            for (const auto& spot : map_spots)
            {
                auto* actor = spot.actor.Get();
                if (acquired_spots.contains(spot.key) || seen.contains(spot.key) ||
                    !is_uobject_valid_fast(actor) ||
                    actor->HasAnyFlags(static_cast<EObjectFlags>(
                        RF_ClassDefaultObject | RF_ArchetypeObject | RF_NeedLoad | RF_NeedPostLoad |
                        RF_NeedPostLoadSubobjects)))
                {
                    continue;
                }
                // AActor::bHidden shares a byte with unrelated flags. Reading that
                // byte as bool hides visible actors whenever any other bit is set.
                auto* hidden = actor->GetPropertyByNameInChain(L"bHidden");
                if (!hidden || hidden->GetClass().GetName() != L"BoolProperty" ||
                    static_cast<FBoolProperty*>(hidden)->GetPropertyValueInContainer(actor))
                {
                    continue;
                }
                auto* fn = actor->GetFunctionByNameInChain(L"K2_GetActorLocation");
                if (!is_uobject_valid_fast(fn) || fn->GetNumParms() != 1 ||
                    fn->GetParmsSize() > max_ingredient_call_parameter_bytes ||
                    !field(fn, L"ReturnValue", L"StructProperty", sizeof(std::array<float, 3>)))
                {
                    continue;
                }
                std::array<std::byte, max_ingredient_location_parameter_bytes> params{};
                actor->ProcessEvent(fn, params.data());
                RuntimeIngredientMarker marker;
                auto* result = find_function_param_by_name(fn, L"ReturnValue");
                std::memcpy(marker.location.data(),
                            result->ContainerPtrToValuePtr<void>(params.data()),
                            sizeof(std::array<float, 3>));
                if (!std::all_of(marker.location.begin(), marker.location.end(),
                                 [](float v)
                                 {
                                     return std::isfinite(v);
                                 }))
                {
                    continue;
                }
                seen.insert(spot.key);
                markers.push_back(marker);
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            if (rediscover)
            {
                Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Area map ingredient discovery "
                                                    "loaded={} uncollected_visible={}\n"),
                                                map_spots.size(), markers.size());
            }
#endif
            return;
        }
        std::optional<IngredientCounts> counts(UObject* pawn)
        {
            const RuntimeHotPathProfileScope profile{L"ingredients:stock_snapshot"};
            if (!is_uobject_valid_fast(pawn))
            {
                return std::nullopt;
            }
            auto* library = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, L"/Script/TresGame.Default__TresGameBlueprintLibrary");
            if (!is_uobject_valid_fast(library))
            {
                return std::nullopt;
            }
            auto* fn = library->GetFunctionByNameInChain(L"BP_TresGetBackyardItemNum");
            if (!is_uobject_valid_fast(fn))
            {
                return std::nullopt;
            }
            auto* context = find_function_param_by_name(fn, L"WorldContextObject");
            auto* key = find_function_param_by_name(fn, L"InItemKeyName");
            auto* result = find_function_param_by_name(fn, L"ReturnValue");
            if (fn->GetNumParms() != ingredient_prize_parameter_count ||
                fn->GetParmsSize() > max_ingredient_call_parameter_bytes ||
                !field(fn, L"WorldContextObject", L"ObjectProperty", sizeof(UObject*)) ||
                !field(fn, L"InItemKeyName", L"NameProperty", sizeof(FName)) ||
                !field(fn, L"ReturnValue", L"IntProperty", sizeof(int)))
            {
                return std::nullopt;
            }
            std::vector<std::byte> params(fn->GetParmsSize());
            IngredientCounts values{};
            *context->ContainerPtrToValuePtr<UObject*>(params.data()) = pawn;
            for (int i = ingredient_ids::first_item; i <= ingredient_ids::last_item; ++i)
            {
                if (!is_field_ingredient(i))
                {
                    continue;
                }
                *key->ContainerPtrToValuePtr<FName>(params.data()) =
                    FName(ingredient_key(i).c_str(), FNAME_Add);
                library->ProcessEvent(fn, params.data());
                values[i - 1] = *result->ContainerPtrToValuePtr<int>(params.data());
                if (values[i - 1] < 0)
                {
                    return std::nullopt;
                }
            }
            return values;
        }
        static std::wstring spot_for_object(UObject* object)
        {
            std::wstring name;
            if (!try_get_uobject_full_name_fast(object, name))
            {
                return {};
            }
            const auto separator = name.find(L' ');
            if (separator == std::wstring::npos)
            {
                return {};
            }
            return std::wstring(ingredient_spot_key(std::wstring_view(name).substr(separator + 1)));
        }
        static std::wstring event_spot(UObject* context, FFrame& stack)
        {
            auto spot = spot_for_object(context);
            if (!spot.empty())
            {
                return spot;
            }
            // Library calls have a CDO context. The Blueprint VM frame identifies
            // the owning pickup actor without inspecting or advancing its arguments.
            auto* frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
            for (int depth = 0; depth < max_ingredient_event_frame_depth && frame; ++depth)
            {
                spot = spot_for_object(frame->Object);
                if (!spot.empty())
                {
                    return spot;
                }
                if (frame->PreviousFrame == frame)
                {
                    break;
                }
                frame = frame->PreviousFrame;
            }
            return {};
        }
        Snapshot before(UObject* context, FFrame& stack)
        {
            const RuntimeHotPathProfileScope profile{L"ingredients:before_pickup"};
            Snapshot snapshot{};
            snapshot.counts.fill(-1);
            if (!RuntimeGrantInternalCallDepth::is_active() && scope())
            {
                auto* pawn = config.inventory_context();
                auto values = counts(pawn);
                if (values)
                {
                    snapshot.pawn = pawn;
                    snapshot.counts = *values;
                    snapshot.spot = event_spot(context, stack);
                }
            }
            return snapshot;
        }
        void after(const Snapshot& snapshot, double elapsed_ms)
        {
            const RuntimeHotPathProfileScope profile{L"ingredients:after_pickup"};
            auto* pawn = snapshot.pawn.Get();
            if (!pawn || snapshot.counts[0] < 0)
            {
                return;
            }
            auto values = counts(pawn);
            if (!values)
            {
                return;
            }
            std::vector<std::wstring> additions;
            const auto changes = acquired_field_ingredients(snapshot.counts, *values, true);
            for (int i : changes)
            {
                auto key = ingredient_key(i);
                if (acquired.insert(key).second)
                {
                    additions.push_back(key);
                }
            }
            const bool new_spot = !changes.empty() && !snapshot.spot.empty() &&
                                  acquired_spots.insert(snapshot.spot).second;
            if (!additions.empty() || new_spot || KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Ingredient event=food_prize changed={} new_count={} "
                        "spot={} new_spot={} elapsed_ms={} mode=event_driven\n"),
                    changes.size(), additions.size(), snapshot.spot, new_spot, elapsed_ms);
            }
            dirty = dirty || !additions.empty() || new_spot;
            if (!additions.empty() || new_spot)
            {
                ++journal_revision;
            }
            if (!changes.empty())
            {
                record_spot(snapshot.spot);
            }
            persist();
        }
        void record_spot(std::wstring_view spot)
        {
            const RuntimeHotPathProfileScope profile{L"ingredients:record_spot"};
            const auto message = ingredient_spot_message(spot);
            if (message.empty())
            {
                return;
            }
            ++last_pickup_sequence;
            last_pickup_spot = spot;
            const auto timestamp = now_iso8601_utc();
            last_pickup_at.assign(timestamp.begin(), timestamp.end());
            dirty = true;
            persist();
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] {} sequence={}\n"), message,
                                            last_pickup_sequence);
        }
        void persist()
        {
            if (!dirty)
            {
                return;
            }
            const RuntimeHotPathProfileScope profile{L"ingredients:persist"};
            std::ostringstream json;
            json << "{\"schema\":\"kh3ap.runtime_ingredient_state.v1\",\"seed_name\":\""
                 << json_escape(seed) << "\",\"slot\":" << slot
                 << ",\"source\":\"food_prize\",\"acquired_ingredients\":[";
            bool first = true;
            for (auto& key : acquired)
            {
                if (!first)
                {
                    json << ',';
                }
                first = false;
                json << '"' << json_escape(key) << '"';
            }
            json << "],\"acquired_spots\":[";
            first = true;
            for (auto& key : acquired_spots)
            {
                if (!first)
                {
                    json << ',';
                }
                first = false;
                json << '"' << json_escape(key) << '"';
            }
            json << "],\"last_pickup_sequence\":" << last_pickup_sequence
                 << ",\"last_pickup_spot\":\"" << json_escape(last_pickup_spot)
                 << "\",\"last_pickup_at_utc\":\"" << json_escape(last_pickup_at)
                 << "\",\"updated_at_utc\":\"" << now_iso8601_utc() << "\"}\n";
            if (stopping)
            {
                dirty = !write_text_file_atomic_verified(bridge / state_file, json.str());
            }
            else
            {
                dirty = !checkpoints.submit(
                    {checkpoint_scope, ++checkpoint_revision, bridge / state_file, json.str()});
            }
        }
        void tick()
        {
            auto completed = checkpoints.take_completed();
            if (completed.empty() && !publication)
            {
                return;
            }
            if (!scope())
            {
                publication.reset();
                return;
            }
            for (const auto& result : completed)
            {
                const auto& value = *result.checkpoint;
                if (value.scope != checkpoint_scope || value.path != bridge / state_file)
                {
                    continue;
                }
                checkpoint_failed = !result.committed;
                if (!result.committed)
                {
                    Output::send<LogLevel::Error>(STR("[KH3APProbeCpp] Ingredient checkpoint "
                                                      "commit failed revision={} path={} "
                                                      "retry=pending_only\n"),
                                                  value.revision, value.path.wstring());
                    continue;
                }
                publication = result.checkpoint;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Ingredient checkpoint committed "
                        "revision={} worker_us={} path={}\n"),
                    value.revision, result.commit_us, value.path.wstring());
#endif
            }
            if (publication && pipe::write(publication->path, publication->payload))
            {
                publication.reset();
            }
            if (dirty && !completed.empty())
            {
                persist();
            }
        }
        void shutdown()
        {
            if (stopping)
            {
                return;
            }
            stopping = true;
            checkpoints.stop();
            tick();
            if (dirty || checkpoint_failed)
            {
                dirty = true;
                persist();
            }
        }
        static bool field(UFunction* fn, const wchar_t* name, const wchar_t* type, int size)
        {
            auto* p = find_function_param_by_name(fn, name);
            return p && p->GetClass().GetName() == type && p->GetElementSize() == size &&
                   p->GetOffset_Internal() >= 0 &&
                   p->GetOffset_Internal() + size <= fn->GetParmsSize();
        }
        void install_once()
        {
            if (installed)
            {
                return;
            }
            auto* food = UObjectGlobals::StaticFindObject<UFunction*>(UFunction::StaticClass(),
                                                                      nullptr, food_hook);
            auto* gimmick = UObjectGlobals::StaticFindObject<UFunction*>(UFunction::StaticClass(),
                                                                         nullptr, gimmick_hook);
            auto* release = UObjectGlobals::StaticFindObject<UFunction*>(UFunction::StaticClass(),
                                                                         nullptr, release_hook);
            if (!is_uobject_valid_fast(food) || !is_uobject_valid_fast(gimmick) ||
                !(food->GetFunctionFlags() & FUNC_Native) ||
                !(gimmick->GetFunctionFlags() & FUNC_Native) || gimmick->GetNumParms() != 0 ||
                gimmick->GetParmsSize() != 0 ||
                food->GetNumParms() != ingredient_prize_parameter_count ||
                !field(food, L"WorldContextObject", L"ObjectProperty", sizeof(UObject*)) ||
                !field(food, L"InKeyName", L"NameProperty", sizeof(FName)) ||
                !field(food, L"ReturnValue", L"BoolProperty", 1) || !food->GetFuncPtr() ||
                !gimmick->GetFuncPtr() || !is_uobject_valid_fast(release) ||
                !(release->GetFunctionFlags() & FUNC_Native) ||
                release->GetNumParms() != ingredient_release_parameter_count ||
                !field(release, L"InKeyName", L"NameProperty", sizeof(FName)) ||
                !field(release, L"InForMagicDraw", L"BoolProperty", 1) || !release->GetFuncPtr())
            {
                Output::send<LogLevel::Warning>(STR(
                    "[KH3APProbeCpp] Ingredient hooks rejected unsupported reflected layout\n"));
                return;
            }
            original_food = food->GetFuncPtr();
            original_gimmick = gimmick->GetFuncPtr();
            original_release = release->GetFuncPtr();
            instance = this;
            food->SetFuncPtr(food_thunk);
            gimmick->SetFuncPtr(gimmick_thunk);
            release->SetFuncPtr(release_thunk);
            installed = true;
            install_collection();
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Ingredient food-prize hook installed mode=event_driven\n"));
        }
    };
    RuntimeIngredientService::RuntimeIngredientService(RuntimeIngredientConfig c)
        : impl_(std::make_unique<Impl>(std::move(c)))
    {
    }
    RuntimeIngredientService::~RuntimeIngredientService() = default;
    void RuntimeIngredientService::tick()
    {
        impl_->tick();
    }
    void RuntimeIngredientService::shutdown()
    {
        impl_->shutdown();
    }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto RuntimeIngredientService::benchmark_stock(UObject* expected_pawn) -> std::string
    {
        auto* self = Impl::instance;
        if (!self || !is_uobject_valid_fast(expected_pawn) ||
            self->config.inventory_context() != expected_pawn)
        {
            throw std::runtime_error("ingredient_benchmark_current_sora_required");
        }
        using Clock = std::chrono::steady_clock;
        constexpr int sample_limit = 12;
        constexpr auto time_budget = std::chrono::milliseconds(100);
        const auto started = Clock::now();
        const auto first = self->counts(expected_pawn);
        if (!first)
        {
            throw std::runtime_error("ingredient_benchmark_stock_unavailable");
        }
        const auto first_us =
            std::chrono::duration<double, std::micro>(Clock::now() - started).count();
        std::vector<double> samples;
        for (int index = 0; index < sample_limit; ++index)
        {
            const auto sample_started = Clock::now();
            const auto values = self->counts(expected_pawn);
            const auto elapsed =
                std::chrono::duration<double, std::micro>(Clock::now() - sample_started).count();
            if (!values || *values != *first)
            {
                throw std::runtime_error("ingredient_benchmark_stock_changed");
            }
            samples.push_back(elapsed);
            if (Clock::now() - started >= time_budget)
            {
                break;
            }
        }
        std::ostringstream json;
        json << "{\"schema\":\"kh3ap.ingredient_benchmark.v1\",\"unchanged\":true,\"field_"
                "ingredients\":51,\"first_snapshot_us\":"
             << first_us << ",\"snapshot_samples_us\":[";
        for (size_t index = 0; index < samples.size(); ++index)
        {
            if (index)
            {
                json << ',';
            }
            json << samples[index];
        }
        json << "],\"counts\":[";
        for (size_t index = 0; index < first->size(); ++index)
        {
            if (index)
            {
                json << ',';
            }
            json << (*first)[index];
        }
        json << "]}";
        return json.str();
    }
#endif
    auto RuntimeIngredientService::journal_revision() const -> uint64_t
    {
        return impl_->journal_revision;
    }
    void RuntimeIngredientService::install_once()
    {
        impl_->install_once();
    }
    auto RuntimeIngredientService::uncollected_markers(
        bool rediscover, std::vector<RuntimeIngredientMarker>& markers) -> void
    {
        impl_->uncollected_markers(rediscover, markers);
    }
    auto RuntimeIngredientService::journal_collected() -> std::optional<std::wstring>
    {
        if (!impl_->scope())
        {
            return std::nullopt;
        }
        std::wstring result;
        for (const auto& key : impl_->acquired)
        {
            if (!result.empty())
            {
                result += L",";
            }
            result += key;
        }
        return result;
    }
} // namespace kh3ap
