#include <chrono>
#include <memory>
#include <utility>
#include "kh3ap/app/config.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/colors/color_gummi.h"

#include "kh3ap/features/colors/color_material_ops.h"
#include "kh3ap/features/colors/color_palette.h"
#include "kh3ap/features/colors/color_object_registry.h"
#include "kh3ap/core/events/world_context_events.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/bridge/utilities.h"
#include <sstream>
#endif

namespace kh3ap
{
    namespace
    {
        constexpr auto gummi_hash_initial_state = 1469598103934665603ull;
        constexpr auto gummi_hash_prime = 1099511628211ull;
        constexpr auto gummi_row_type_offset = 0x40;
        constexpr auto gummi_row_map_offset = 0x48;
        constexpr auto gummi_palette_value_hash_shift = 24;
        constexpr auto gummi_hue_bucket_count = 360ull;
        constexpr auto gummi_saturation_hash_shift = 16;
        constexpr auto gummi_hue_jitter_hash_shift = 8;
        constexpr auto gummi_wild_saturation_base = 0.6f;
        constexpr auto gummi_wild_saturation_jitter_span = 0.4f;
        constexpr auto gummi_coordinated_saturation_base = 0.55f;
        constexpr auto gummi_coordinated_saturation_jitter_span = 0.3f;
        constexpr auto gummi_hue_jitter_degrees = 16.0f;
        constexpr auto max_gummi_material_slots = 24;
        constexpr auto max_gummi_slot_log_samples = 3;
        constexpr auto palette_usage_threshold = 0.5f;
        constexpr auto gummi_color_discovery_retry = std::chrono::milliseconds(100);
        constexpr auto native_gummi_row_map_entry_bytes = 24;
        constexpr auto max_gummi_color_rows = 1024;
        constexpr auto max_gummi_color_blocks = 4096;
        constexpr auto gummi_color_diagnostic_sample_limit = 32;
    } // namespace

    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        using ::kh3ap::settings::s_debug_logging;

        constexpr std::wstring_view plan_row_struct = STR("TresGummiShipPlanData");
        constexpr std::wstring_view color_key = STR("gummi");

        // A block's colour comes from the palette: with CT_PaletteUse at 1
        // the material samples the palette texture at row CT2_PaletteNum,
        // and CT_CustomColor is the colour used when the palette is off.
        // Read live on all four masters (SQEX_GM_M_PlasticMat, _StandardMat,
        // _TranslucentMat, SQEX_GM_B_BasicMat). The PTN1_MaskTexture*_Color
        // vectors look like colours but are channel selectors - pure red,
        // green and blue - and are never touched.
        constexpr std::wstring_view palette_use_parameter = STR("CT_PaletteUse");
        constexpr std::wstring_view palette_index_parameter = STR("CT2_PaletteNum");
        constexpr std::wstring_view custom_color_parameter = STR("CT_CustomColor");

        // A departure-menu ship is ~300 single-slot meshes; each slot costs
        // a handful of ProcessEvent calls, so the work is spread out.
        constexpr int slots_per_tick = 32;
        // A ship is painted once its blocks have stopped changing: the
        // actor exists a moment before the game has filled it.
        constexpr std::chrono::milliseconds settle_time{300};

        struct ScriptArray
        {
            const uint8_t* data;
            int32_t count;
            int32_t capacity;
        };

        auto read_struct_array(UObject* object, const wchar_t* name)
            -> std::pair<UScriptStruct*, const ScriptArray*>
        {
            FProperty* const property = object->GetPropertyByNameInChain(name);
            if (!property || property->GetClass().GetName() != STR("ArrayProperty"))
            {
                return {};
            }
            auto* const inner =
                const_cast<FProperty*>(static_cast<FArrayProperty*>(property)->GetInner());
            if (!inner || inner->GetClass().GetName() != STR("StructProperty"))
            {
                return {};
            }
            return {static_cast<FStructProperty*>(inner)->GetStruct(),
                    property->ContainerPtrToValuePtr<ScriptArray>(object)};
        }

        // A ship's layout as one number: every reflected field of every
        // block plan (position, shape, rotation, colours), order-independent.
        // A prefab built as shipped matches its plan row; any edit in the
        // editor breaks the match. Verified live on three prefabs and a
        // one-block custom ship, in the departure menu and in flight.
        auto hash_fields(UScriptStruct* type, const uint8_t* data) -> uint64_t
        {
            uint64_t hash = gummi_hash_initial_state;
            for (FProperty* field = type->GetFirstProperty(); field;
                 field = field->HasNext() ? field->GetNextFieldAsProperty() : nullptr)
            {
                const uint8_t* const bytes = field->ContainerPtrToValuePtr<uint8_t>(data);
                for (int32_t index = 0; index < field->GetSize(); ++index)
                {
                    hash = (hash ^ bytes[index]) * gummi_hash_prime;
                }
            }
            return hash;
        }

        auto combine_hashes(std::vector<uint64_t> hashes) -> uint64_t
        {
            std::sort(hashes.begin(), hashes.end());
            uint64_t hash = gummi_hash_initial_state ^ hashes.size();
            for (const uint64_t value : hashes)
            {
                hash = (hash ^ value) * gummi_hash_prime;
            }
            return hash;
        }

        // A built ship keeps each block's plan (FTresGummiShipPartsStruct,
        // the same struct as a plan row's m_body) in planData.
        auto collect_block_plans(UObject* ship, const wchar_t* name, std::vector<uint64_t>& hashes)
            -> bool
        {
            const auto [element, array] = read_struct_array(ship, name);
            FProperty* const plan =
                element ? find_struct_field_by_name(element, STR("planData")) : nullptr;
            if (!array || !plan || plan->GetClass().GetName() != STR("StructProperty"))
            {
                return false;
            }
            UScriptStruct* const plan_type = static_cast<FStructProperty*>(plan)->GetStruct();
            for (int32_t index = 0; index < array->count; ++index)
            {
                const uint8_t* const block =
                    array->data + static_cast<size_t>(index) * element->GetSize();
                hashes.push_back(
                    hash_fields(plan_type, plan->ContainerPtrToValuePtr<uint8_t>(block)));
            }
            return true;
        }

        auto ship_fingerprint(UObject* ship) -> std::optional<uint64_t>
        {
            std::vector<uint64_t> hashes{};
            if (!collect_block_plans(ship, STR("m_BaseGummiData"), hashes) ||
                !collect_block_plans(ship, STR("m_DecoGummiData"), hashes) || hashes.empty())
            {
                return std::nullopt;
            }
            return combine_hashes(std::move(hashes));
        }

        // Records the layout fingerprint of every row of a plan table. Each
        // prefab is its own one-row DataTable under
        // /Game/GumiShip/GumiFactory/BluePrints/real/GumiShipPlan/, so the
        // table name is the prefab's name. Same raw RowMap read as the map
        // overlay: RowStruct at 0x40, RowMap right after it.
        auto read_plan_table(UObject* table, std::unordered_map<uint64_t, std::wstring>& plans)
            -> void
        {
            auto* const row_type =
                static_cast<UScriptStruct*>(read_strong_object_property(table, STR("RowStruct")));
            FProperty* const row_property = table->GetPropertyByNameInChain(STR("RowStruct"));
            if (!row_type || !is_uobject_valid_fast(row_type) ||
                row_type->GetName() != plan_row_struct || !row_property ||
                row_property->GetOffset_Internal() != gummi_row_type_offset)
            {
                return;
            }
            FProperty* const body = find_struct_field_by_name(row_type, STR("m_body"));
            if (!body || body->GetClass().GetName() != STR("ArrayProperty"))
            {
                return;
            }
            auto* const parts =
                const_cast<FProperty*>(static_cast<FArrayProperty*>(body)->GetInner());
            if (!parts || parts->GetClass().GetName() != STR("StructProperty"))
            {
                return;
            }
            UScriptStruct* const parts_type = static_cast<FStructProperty*>(parts)->GetStruct();
            struct Entry
            {
                FName name;
                const uint8_t* data;
                int64_t padding;
            };
            static_assert(sizeof(Entry) == native_gummi_row_map_entry_bytes);
            const auto* const rows = reinterpret_cast<const ScriptArray*>(
                reinterpret_cast<const uint8_t*>(table) + gummi_row_map_offset);
            if (!rows->data || rows->count <= 0 || rows->count > max_gummi_color_rows ||
                rows->capacity < rows->count)
            {
                return;
            }
            for (int32_t index = 0; index < rows->count; ++index)
            {
                const Entry& entry = reinterpret_cast<const Entry*>(rows->data)[index];
                if (!entry.data)
                {
                    continue;
                }
                const auto* const blocks = body->ContainerPtrToValuePtr<ScriptArray>(entry.data);
                if (!blocks->data || blocks->count <= 0 || blocks->count > max_gummi_color_blocks)
                {
                    continue;
                }
                std::vector<uint64_t> hashes{};
                hashes.reserve(static_cast<size_t>(blocks->count));
                for (int32_t block = 0; block < blocks->count; ++block)
                {
                    hashes.push_back(
                        hash_fields(parts_type, blocks->data + static_cast<size_t>(block) *
                                                                   parts_type->GetSize()));
                }
                plans[combine_hashes(std::move(hashes))] = table->GetName();
            }
        }

        // The level being played, e.g. "wm_01" or "gm_01".
        auto current_world(FWeakObjectPtr& engine_cache) -> UObject*
        {
            UObject* engine = engine_cache.Get();
            if (!is_uobject_valid_fast(engine))
            {
                engine = UObjectGlobals::StaticFindObject<UObject*>(
                    nullptr, nullptr, STR("/Engine/Transient.TresGameEngine_0"));
                if (!is_uobject_valid_fast(engine))
                {
                    return nullptr;
                }
                engine_cache = make_uobject_weak(engine);
            }
            UObject* const viewport = read_strong_object_property(engine, STR("GameViewport"));
            if (!viewport || !is_uobject_valid_fast(viewport))
            {
                return nullptr;
            }
            UObject* const world = read_strong_object_property(viewport, STR("World"));
            return world && is_uobject_valid_fast(world) ? world : nullptr;
        }

        // The seed's colour for one of a prefab's palette colours. Keyed by
        // the palette index rather than by block, so every block that shared
        // a colour still shares one, and the same prefab comes out the same
        // in the departure menu and in flight.
        //
        // The palette itself is a texture, so the original colour is not
        // known here; brightness is drawn from three levels instead, so a
        // ship keeps some dark trim and some light panels.
        auto palette_index_color(std::wstring_view seed, std::wstring_view plan,
                                 RuntimeColorHarmonyMode mode, float palette_index) -> DerivedColor
        {
            const std::wstring key =
                std::format(STR("palette{}"), static_cast<int>(std::lround(palette_index)));
            const uint64_t hash = hash_color_inputs(seed, plan, key);
            constexpr std::array<float, 3> values{0.35f, 0.7f, 0.95f};
            const float value = values[(hash >> gummi_palette_value_hash_shift) % values.size()];
            if (mode == RuntimeColorHarmonyMode::Wild)
            {
                return hsv_to_rgb(static_cast<float>(hash % gummi_hue_bucket_count),
                                  gummi_wild_saturation_base +
                                      jitter(hash, gummi_saturation_hash_shift,
                                             gummi_wild_saturation_jitter_span),
                                  value);
            }
            // A base hue for the ship, and each colour on it one of a
            // split-complementary set around it.
            constexpr std::array<float, 4> offsets{0.0f, 35.0f, 180.0f, 215.0f};
            const float base = static_cast<float>(hash_color_inputs(seed, plan, STR("base")) %
                                                  gummi_hue_bucket_count);
            const float hue =
                wrap_hue(base + offsets[hash % offsets.size()] +
                         jitter(hash, gummi_hue_jitter_hash_shift, gummi_hue_jitter_degrees));
            return hsv_to_rgb(hue,
                              gummi_coordinated_saturation_base +
                                  jitter(hash, gummi_saturation_hash_shift,
                                         gummi_coordinated_saturation_jitter_span),
                              value);
        }

        // The game's own values on one block material, read before the
        // first write: a re-roll draws again from the palette index rather
        // than from our output, and a restore puts these back.
        struct Original
        {
            FWeakObjectPtr instance{};
            float palette_use{};
            float palette_index{};
            std::array<float, 4> custom{};
        };

        enum class ShipStatus : uint8_t
        {
            Settling, // blocks still being filled in
            Painting,
            Done,
            Custom, // matches no prefab; left alone
        };

        struct Ship
        {
            FWeakObjectPtr actor{};
            ShipStatus status{ShipStatus::Settling};
            std::optional<uint64_t> fingerprint{};
            std::chrono::steady_clock::time_point fingerprint_since{};
            std::wstring plan{};
            std::vector<std::pair<FWeakObjectPtr, int>> work{};
            size_t next{};
            int painted_slots{};
            size_t plans_seen{};
        };
    } // namespace

    struct RuntimeColorGummi::Impl
    {
        FWeakObjectPtr engine{};
        FWeakObjectPtr world{};
        bool active{};
        uint64_t object_generation{~uint64_t{0}}, context_generation{~uint64_t{0}};
        bool discovery_pending{true};
        uint64_t discovery_updates{};
        std::chrono::steady_clock::time_point discovery_retry{};
        FWeakObjectPtr manager{};

        std::unordered_map<uint64_t, std::wstring> plans{};
        std::unordered_map<UObject*, FWeakObjectPtr> checked_tables{};

        std::vector<Ship> ships{};
        // Keyed by the dynamic instance: the game's colours, read before
        // the first write, so a re-roll remaps from them rather than from
        // our own output, and a restore can put them back.
        std::unordered_map<UObject*, Original> originals{};

        auto reset_level() -> void
        {
            manager = {};
            ships.clear();
            std::erase_if(originals,
                          [](const auto& entry)
                          {
                              return !is_uobject_valid_fast(entry.second.instance.Get());
                          });
            std::erase_if(checked_tables,
                          [](const auto& entry)
                          {
                              return !is_uobject_valid_fast(entry.second.Get());
                          });
        }

        auto track(UObject* actor) -> void
        {
            if (!actor || !is_uobject_valid_fast(actor))
            {
                return;
            }
            if (std::any_of(ships.begin(), ships.end(),
                            [actor](const Ship& ship)
                            {
                                return ship.actor.Get() == actor;
                            }))
            {
                return;
            }
            Ship ship{};
            ship.actor = make_uobject_weak(actor);
            if (!ship.actor.Get())
            {
                return;
            }
            ships.push_back(std::move(ship));
        }

        // Constructor/GC and native context events drive discovery. A pending
        // PostLoad/weak assignment retries only until the queued objects are ready.
        auto refresh_objects(std::chrono::steady_clock::time_point now) -> void
        {
            const auto objects = color_objects::gummi_generation();
            const auto context = world_context_revision.load(std::memory_order_acquire);
            if (objects == object_generation && context == context_generation &&
                (!discovery_pending || now < discovery_retry))
            {
                return;
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            RuntimeHotPathProfileScope profile{L"colors:gummi_discovery"};
#endif
            ++discovery_updates;
            object_generation = objects;
            context_generation = context;
            const auto snapshot = color_objects::snapshot(true);
            discovery_pending = snapshot.unfinished;
            discovery_retry = now + gummi_color_discovery_retry;
            UObject* const live = current_world(engine);
            if (live != world.Get())
            {
                world = make_uobject_weak(live);
                reset_level();
            }
            const auto name = live ? live->GetName() : std::wstring{};
            active = world.Get() && (name.starts_with(STR("wm_")) || name.starts_with(STR("gm_")));
            // A missing viewport world waits for its native context-ready event.
            // Only objects still in PostLoad keep the readiness retry alive.
            if (!active)
            {
                return;
            }
            for (auto* table : snapshot.tables)
            {
                auto& checked = checked_tables[table];
                if (checked.Get() == table)
                {
                    continue;
                }
                checked = make_uobject_weak(table);
                read_plan_table(table, plans);
            }
            manager = {};
            for (auto* editor : snapshot.editors)
            {
                manager = make_uobject_weak(editor);
            }
            for (auto* ship : snapshot.ships)
            {
                track(ship);
            }
        }

        // The editor's ship, which is never painted.
        auto editor_ship() -> UObject*
        {
            UObject* const owner = manager.Get();
            return owner && is_uobject_valid_fast(owner)
                       ? read_strong_object_property(owner, STR("m_pGummiShipActor"))
                       : nullptr;
        }

        auto settle(Ship& ship, UObject* actor, std::chrono::steady_clock::time_point now) -> void
        {
            const std::optional<uint64_t> fingerprint = ship_fingerprint(actor);
            if (fingerprint != ship.fingerprint)
            {
                ship.fingerprint = fingerprint;
                ship.fingerprint_since = now;
                return;
            }
            if (!fingerprint || now - ship.fingerprint_since < settle_time)
            {
                return;
            }
            const auto match = plans.find(*fingerprint);
            if (match == plans.end())
            {
                // A later streamed table may provide this prefab's plan.
                // Remember the catalogue size so a later discovery retries
                // this ship without rehashing every custom ship each frame.
                if (!plans.empty())
                {
                    ship.status = ShipStatus::Custom;
                    ship.plans_seen = plans.size();
                    Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Color gummi ship left as "
                                                       "built (not a prefab) ship={}\n"),
                                                   actor->GetName());
                }
                return;
            }
            ship.plan = match->second;
            ship.work.clear();
            ship.next = 0;
            ship.painted_slots = 0;
            if (UClass* const mesh_class = resolve_mesh_component_class())
            {
                for (UObject* const component : collect_mesh_components(actor, mesh_class))
                {
                    const int slots = std::min(read_num_materials(component).value_or(0),
                                               max_gummi_material_slots);
                    for (int slot = 0; slot < slots; ++slot)
                    {
                        FWeakObjectPtr weak{};
                        weak = make_uobject_weak(component);
                        ship.work.emplace_back(weak, slot);
                    }
                }
            }
            ship.status = ShipStatus::Painting;
        }

        auto paint_slot(Ship& ship, UObject* component, int slot, std::wstring_view seed,
                        RuntimeColorHarmonyMode mode) -> void
        {
            UObject* const instance = create_dynamic_material_instance(component, slot);
            if (!instance || !is_uobject_valid_fast(instance))
            {
                return;
            }
            auto known = originals.find(instance);
            if (known != originals.end() && known->second.instance.Get() != instance)
            {
                // UObject addresses can be recycled after GC. The old
                // material's palette must never be inherited by a new one.
                originals.erase(known);
                known = originals.end();
            }
            if (known == originals.end())
            {
                const auto palette_use =
                    read_scalar_parameter_from_mid(instance, palette_use_parameter);
                const auto palette_index =
                    read_scalar_parameter_from_mid(instance, palette_index_parameter);
                const auto custom =
                    read_vector_parameter_from_mid(instance, custom_color_parameter);
                // Only a block drawn from the palette has a colour of ours
                // to give it; anything else is left as the game made it.
                if (!palette_use || !palette_index || !custom ||
                    *palette_use < palette_usage_threshold)
                {
                    return;
                }
                Original original{};
                original.instance = make_uobject_weak(instance);
                if (!original.instance.Get())
                {
                    return;
                }
                original.palette_use = *palette_use;
                original.palette_index = *palette_index;
                original.custom = *custom;
                known = originals.emplace(instance, original).first;
                if ((s_debug_logging || KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS) &&
                    ship.painted_slots < max_gummi_slot_log_samples)
                {
                    Output::send<LogLevel::Normal>(
                        STR("[KH3APProbeCpp] Color gummi slot ship={} component={} slot={} "
                            "palette_index={:.0f} custom=({:.2f},{:.2f},{:.2f})\n"),
                        ship.plan, component->GetName(), slot, *palette_index, (*custom)[0],
                        (*custom)[1], (*custom)[2]);
                }
            }
            const Original& original = known->second;
            set_vector_parameter_on_mid(
                instance, custom_color_parameter,
                palette_index_color(seed, ship.plan, mode, original.palette_index), color_key,
                original.custom[3]);
            set_scalar_parameter_on_mid(instance, palette_use_parameter, 0.0f, color_key);
            ++ship.painted_slots;
        }

        auto tick(std::wstring_view seed, RuntimeColorHarmonyMode mode,
                  std::chrono::steady_clock::time_point now) -> void
        {
            refresh_objects(now);
            if (!active)
            {
                return;
            }
            UObject* const editing = editor_ship();
            std::erase_if(ships,
                          [](const Ship& ship)
                          {
                              return !is_uobject_valid_fast(ship.actor.Get());
                          });

            int budget = slots_per_tick;
            for (Ship& ship : ships)
            {
                UObject* const actor = ship.actor.Get();
                if (actor == editing)
                {
                    continue;
                }
                if (ship.status == ShipStatus::Custom && ship.plans_seen != plans.size())
                {
                    ship.status = ShipStatus::Settling;
                }
                if (ship.status == ShipStatus::Settling)
                {
                    settle(ship, actor, now);
                }
                if (ship.status != ShipStatus::Painting)
                {
                    continue;
                }
                while (budget > 0 && ship.next < ship.work.size())
                {
                    const auto& [weak, slot] = ship.work[ship.next++];
                    UObject* const component = weak.Get();
                    if (component && is_uobject_valid_fast(component))
                    {
                        paint_slot(ship, component, slot, seed, mode);
                        --budget;
                    }
                }
                if (ship.next >= ship.work.size())
                {
                    ship.status = ShipStatus::Done;
                    Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Color gummi ship painted "
                                                       "ship={} plan={} slots={} of={} seed={}\n"),
                                                   actor->GetName(), ship.plan, ship.painted_slots,
                                                   ship.work.size(), seed);
                }
                if (budget <= 0)
                {
                    break;
                }
            }
        }

        auto restore_all() -> int
        {
            int restored = 0;
            for (const auto& entry : originals)
            {
                const Original& original = entry.second;
                UObject* const instance = original.instance.Get();
                if (!is_uobject_valid_fast(instance))
                {
                    continue;
                }
                set_vector_parameter_on_mid(
                    instance, custom_color_parameter,
                    DerivedColor{original.custom[0], original.custom[1], original.custom[2]},
                    color_key, original.custom[3]);
                set_scalar_parameter_on_mid(instance, palette_use_parameter, original.palette_use,
                                            color_key);
                ++restored;
            }
            for (Ship& ship : ships)
            {
                ship.status = ShipStatus::Settling;
                ship.fingerprint.reset();
            }
            return restored;
        }

        auto repaint_all() -> void
        {
            for (Ship& ship : ships)
            {
                if (ship.status == ShipStatus::Done || ship.status == ShipStatus::Painting)
                {
                    ship.next = 0;
                    ship.painted_slots = 0;
                    ship.status = ShipStatus::Painting;
                }
            }
        }
    };

    RuntimeColorGummi::RuntimeColorGummi() : impl_(std::make_unique<Impl>())
    {
    }

    RuntimeColorGummi::~RuntimeColorGummi() = default;

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto RuntimeColorGummi::test_snapshot() const -> std::string
    {
        std::ostringstream out;
        out << std::boolalpha << "{\"active\":" << impl_->active
            << ",\"plans\":" << impl_->plans.size()
            << ",\"object_generation\":" << impl_->object_generation
            << ",\"discovery_pending\":" << impl_->discovery_pending
            << ",\"discovery_updates\":" << impl_->discovery_updates << ",\"ships\":[";
        bool comma = false;
        for (const Ship& ship : impl_->ships)
        {
            UObject* const actor = ship.actor.Get();
            if (!is_uobject_valid_fast(actor))
            {
                continue;
            }
            if (comma)
            {
                out << ',';
            }
            comma = true;
            out << "{\"actor\":\"" << json_escape(actor->GetFullName()) << "\",\"plan\":\""
                << json_escape(ship.plan) << "\",\"status\":" << static_cast<int>(ship.status)
                << ",\"painted\":" << ship.painted_slots << '}';
        }
        out << "],\"materials\":[";
        comma = false;
        size_t samples = 0;
        for (const auto& entry : impl_->originals)
        {
            const auto& original = entry.second;
            UObject* const material = original.instance.Get();
            if (!is_uobject_valid_fast(material))
            {
                continue;
            }
            const auto use = read_scalar_parameter_from_mid(material, palette_use_parameter);
            const auto color = read_vector_parameter_from_mid(material, custom_color_parameter);
            if (!use || !color)
            {
                continue;
            }
            if (comma)
            {
                out << ',';
            }
            comma = true;
            out << "{\"material\":\"" << json_escape(material->GetFullName())
                << "\",\"palette_use\":" << *use << ",\"original_use\":" << original.palette_use
                << ",\"color\":[";
            for (size_t i = 0; i < color->size(); ++i)
            {
                if (i)
                {
                    out << ',';
                }
                out << (*color)[i];
            }
            out << "],\"original_color\":[";
            for (size_t i = 0; i < original.custom.size(); ++i)
            {
                if (i)
                {
                    out << ',';
                }
                out << original.custom[i];
            }
            out << "]}";
            if (++samples == gummi_color_diagnostic_sample_limit)
            {
                break;
            }
        }
        out << "]}";
        return out.str();
    }
#endif

    auto RuntimeColorGummi::tick(std::wstring_view seed, RuntimeColorHarmonyMode mode,
                                 std::chrono::steady_clock::time_point now) -> void
    {
        impl_->tick(seed, mode, now);
    }

    auto RuntimeColorGummi::restore_all() -> int
    {
        return impl_->restore_all();
    }

    auto RuntimeColorGummi::repaint_all() -> void
    {
        impl_->repaint_all();
    }

    auto RuntimeColorGummi::on_map_jump() -> void
    {
        impl_->world = {};
        impl_->reset_level();
        impl_->object_generation = ~uint64_t{0};
        impl_->discovery_pending = true;
    }
} // namespace kh3ap
