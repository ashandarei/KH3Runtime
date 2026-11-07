#include "kh02_battle_level_observer.h"
#include "kh3ap/features/player/player_ids.h"
#include "kh02_enemy_stat_assets.h"
#include "kh02_tide_reference.h"
#include "kh02_enemy_scaling_policy.h"
#include "kh02_enemy_scaling.h"
#include "kh02_enemy_stat_table.h"
#include "kh02_enemy_stats.h"
#include "kh3ap/features/kh02/core/kh02_data_table.h"
#include "kh02_tide_balance.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <limits>
#include <iterator>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr size_t enemy_capture_limit = 256;
        constexpr size_t health_change_history_limit = 64;
        constexpr auto hero_profile_check_interval = std::chrono::milliseconds(250);
        constexpr int sora_player_id = player_id_value(PlayerId::Sora);
        constexpr double maximum_native_stat_rate = 1'000;
        constexpr std::size_t reflected_parameter_limit_bytes = 64;

        using Table = Kh02EnemyStatTable;

        struct BattleLevelCall : Kh02ReflectedCall
        {
            BattleLevelCall(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "kh02_battle_profile", reflected_parameter_limit_bytes)
            {
            }
        };
    } // namespace

    struct Kh02EnemyStats::Impl
    {
        explicit Impl(std::function<Kh02BattleLevelSettings()> resolver) : settings(std::move(resolver)) {}

        using Row = Kh02EnemyStatRow;
        std::vector<Row> rows;
        Kh02EnemyStatAssets assets;
        int level{}, base_hp{};
        int tide_reference_base_hp{};
        bool tide_prepared{};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        uint64_t level_applications{};
        uint64_t unchanged_level_requests{};
#endif
        std::string error;
        std::function<Kh02BattleLevelSettings()> settings;
        FWeakObjectPtr profile_world;
        FWeakObjectPtr profile_hero;
        int native_level{};
        int profile_level{};

        struct EnemyHealth
        {
            FWeakObjectPtr actor;
            Kh02EnemyHealthProfile health;
        };

        std::vector<EnemyHealth> enemy_health;
        nlohmann::json health_changes = nlohmann::json::array();
        std::chrono::steady_clock::time_point next_profile_check{};
        Kh02BattleLevelObserver battle_level{
            {this, [](void* context, int level) { static_cast<Impl*>(context)->apply(level); },
             [](void* context, const char* reason) { static_cast<Impl*>(context)->error = reason; }}};

        ~Impl()
        {
            battle_level.stop();
            for (const auto& r : rows)
            {
                if (auto* o = r.table.Get(); is_uobject_valid_fast(o))
                {
                    try
                    {
                        Table t(o, L"/Script/TresGame.TresChrBaseParamDataTable");
                        if (auto* row = t.find(r.name))
                        {
                            *t.field(L"m_MaxHitPoint", L"IntProperty")->ContainerPtrToValuePtr<int>(row) = r.original;
                            for (const auto& value : r.combat)
                            {
                                *t.field(value.field, L"IntProperty")->ContainerPtrToValuePtr<int>(row) =
                                    value.original;
                            }
                        }
                    }
                    catch (...)
                    {
                    }
                }
            }
            assets.release();
        }

        void prepare_tide_balance(UObject* native_tide)
        {
            kh02_prepare_tide_reference(native_tide, assets.base_table(), rows, tide_reference_base_hp);
            tide_prepared = true;
        }

        void apply(int next)
        {
            // This owner is the only writer of its captured private table rows.
            // A successful application remains valid until another level arrives.
            if (level > 0 && next == level)
            {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                ++unchanged_level_requests;
#endif
                return;
            }
            const auto applied = kh02_apply_enemy_level(next, assets.base_table(), rows, tide_reference_base_hp);
            level = applied.level;
            base_hp = applied.base_hp;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            ++level_applications;
#endif
        }

        int read_level(UObject* context)
        {
            BattleLevelCall call(UObjectGlobals::StaticFindObject<UObject*>(
                                     nullptr, nullptr, L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                                 L"BP_TresGetBattleLevel");
            call.object(L"WorldContextObject", context);
            call.run();
            const auto value = read_function_int_param(call.function, call.parameters.data(), L"ReturnValue");
            const auto observed = kh02_observed_battle_level(value);
            if (!observed)
            {
                throw std::runtime_error("kh02_profile_battle_level_unavailable");
            }
            return *observed;
        }

        static int read_enemy_stat(UObject* actor, const wchar_t* method)
        {
            BattleLevelCall query(actor, method);
            query.run();
            const auto value = read_function_int_param(query.function, query.parameters.data(), L"ReturnValue");
            if (!value || *value < 0 || *value > std::numeric_limits<int>::max())
            {
                throw std::runtime_error("kh02_profile_enemy_stat_readback");
            }
            return static_cast<int>(*value);
        }

        void write_level(UObject* context, int target)
        {
            auto* world = kh02_current_world();
            std::erase_if(enemy_health,
                          [world](const EnemyHealth& entry)
                          {
                              auto* actor = entry.actor.Get();
                              return !is_uobject_valid_fast(actor) || kh02_actor_world(actor) != world;
                          });
            std::vector<size_t> captured;
            auto* enemy_class =
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/TresGame.TresEnemyPawnBase");
            if (is_uobject_valid_fast(enemy_class))
            {
                for (auto* actor :
                     find_live_actors_of_class(kh02_story_player(context), enemy_class, enemy_capture_limit))
                {
                    if (!is_uobject_valid_fast(actor) || kh02_actor_world(actor) != world)
                    {
                        continue;
                    }
                    const int maximum = read_enemy_stat(actor, L"GetMaxHitPoint");
                    if (maximum <= 0)
                    {
                        continue;
                    }
                    auto found = std::find_if(enemy_health.begin(), enemy_health.end(),
                                              [actor](const EnemyHealth& entry) { return entry.actor.Get() == actor; });
                    if (found == enemy_health.end())
                    {
                        enemy_health.push_back({make_uobject_weak(actor), {}});
                        found = std::prev(enemy_health.end());
                    }
                    found->health.observe(read_enemy_stat(actor, L"GetHitPoint"), maximum);
                    captured.push_back(static_cast<size_t>(found - enemy_health.begin()));
                }
            }
            // Prepare imported HP before the native setter broadcasts its level
            // change to existing enemies. Native code owns their stat refresh.
            apply(target);
            BattleLevelCall call(UObjectGlobals::StaticFindObject<UObject*>(
                                     nullptr, nullptr, L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                                 L"BP_TresSetBattleLevel");
            call.object(L"WorldContextObject", context);
            call.integer(L"InBattleLevel", target);
            call.run();
            if (read_level(context) != target)
            {
                throw std::runtime_error("kh02_profile_battle_level_readback");
            }
            for (const auto index : captured)
            {
                auto& entry = enemy_health[index];
                auto* actor = entry.actor.Get();
                if (!is_uobject_valid_fast(actor) || kh02_actor_world(actor) != world)
                {
                    continue;
                }
                const int health = entry.health.rescale(read_enemy_stat(actor, L"GetMaxHitPoint"));
                BattleLevelCall restore(actor, L"SetHitPoint");
                restore.integer(L"InHP", health);
                restore.run();
                if (read_enemy_stat(actor, L"GetHitPoint") != health)
                {
                    throw std::runtime_error("kh02_profile_enemy_health_readback");
                }
                health_changes.push_back({{"actor", to_utf8(actor->GetName())},
                                          {"level", target},
                                          {"maximum", entry.health.maximum},
                                          {"health", health},
                                          {"fraction", entry.health.fraction}});
                if (health_changes.size() > health_change_history_limit)
                {
                    health_changes.erase(health_changes.begin());
                }
            }
        }

        void release_hero(UObject* context)
        {
            if (native_level && profile_level && native_level != profile_level && is_uobject_valid_fast(context) &&
                read_level(context) == profile_level)
            {
                write_level(context, native_level);
            }
            profile_world = {};
            profile_hero = {};
            native_level = 0;
            profile_level = 0;
            enemy_health.clear();
            next_profile_check = {};
        }

        void tick_hero(UObject* hero)
        {
            if (!battle_level.ready() || !is_uobject_valid_fast(hero))
            {
                return;
            }
            auto* world = kh02_actor_world(hero);
            if (!is_uobject_valid_fast(world) || world != kh02_current_story_world())
            {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            if (profile_world.Get() == world && profile_hero.Get() == hero && now < next_profile_check)
            {
                return;
            }
            auto* controller = try_read_reflected_object_property_value(hero, L"Controller");
            if (!is_uobject_valid_fast(controller) ||
                try_read_reflected_object_property_value(controller, L"Pawn") != hero)
            {
                return;
            }
            if (profile_world.Get() != world)
            {
                release_hero(hero);
                profile_world = make_uobject_weak(world);
                native_level = read_level(hero);
            }
            auto* identity = hero->GetPropertyByNameInChain(L"m_PlayerUniqueID");
            if (!identity || identity->GetElementSize() != sizeof(uint8_t))
            {
                throw std::runtime_error("kh02_profile_player_identity");
            }
            const int player_id = *identity->ContainerPtrToValuePtr<uint8_t>(hero);
            std::optional<int64_t> sora_level;
            if (player_id == sora_player_id)
            {
                BattleLevelCall call(hero, L"GetChrLevel");
                call.run();
                sora_level = read_function_int_param(call.function, call.parameters.data(), L"ReturnValue");
            }
            const bool final_boss =
                world->GetPathName() == L"/Game/Maps/APKH02Mirrors/APKH02DestinyIslands.APKH02DestinyIslands";
            const auto target = kh02_hero_battle_level(native_level, player_id, sora_level,
                                                       settings ? settings() : Kh02BattleLevelSettings{}, final_boss);
            if (!target)
            {
                return;
            }
            if (read_level(hero) != *target)
            {
                write_level(hero, *target);
            }
            profile_level = *target;
            profile_hero = make_uobject_weak(hero);
            next_profile_check = now + hero_profile_check_interval;
        }

        bool prepare(UObject* context)
        {
            try
            {
                if (!assets.prepare(context))
                {
                    return false;
                }
                if (rows.empty())
                {
                    for (const auto& table : assets.tables())
                    {
                        auto* o = table.Get();
                        Table t(o, L"/Script/TresGame.TresChrBaseParamDataTable");
                        auto* hp = t.field(L"m_MaxHitPoint", L"IntProperty");
                        auto* rate = t.field(L"m_MaxHPRate", L"FloatProperty");

                        struct Visit
                        {
                            Impl* self;
                            UObject* table;
                            FProperty *hp, *rate;
                        };

                        Visit v{this, o, hp, rate};
                        if (!t.visit(
                                [](unsigned long long name, void* row, void* opaque)
                                {
                                    auto& v = *static_cast<Visit*>(opaque);
                                    const float rate = *v.rate->ContainerPtrToValuePtr<float>(row);
                                    if (!std::isfinite(rate) || rate < 0 || rate > maximum_native_stat_rate)
                                    {
                                        throw std::runtime_error("kh02_stats_authored_rate_range");
                                    }
                                    if (rate > 0)
                                    {
                                        v.self->rows.push_back({make_uobject_weak(v.table), name,
                                                                *v.hp->ContainerPtrToValuePtr<int>(row), rate});
                                    }
                                },
                                &v))
                        {
                            throw std::runtime_error("kh02_stats_row_iteration");
                        }
                    }
                }
                if (!tide_prepared)
                {
                    prepare_tide_balance(assets.native_tide_table());
                }
                auto* library = UObjectGlobals::StaticFindObject<UObject*>(
                    nullptr, nullptr, L"/Script/TresGame.Default__TresGameBlueprintLibrary");
                auto* fn = is_uobject_valid_fast(library) ? library->GetFunctionByNameInChain(L"BP_TresGetBattleLevel")
                                                          : nullptr;
                if (!is_uobject_valid_fast(fn) || fn->GetParmsSize() > reflected_parameter_limit_bytes)
                {
                    throw std::runtime_error("kh02_stats_level_getter");
                }
                std::vector<std::byte> data(fn->GetParmsSize());
                if (!write_object_function_param(fn, data.data(), L"WorldContextObject", context))
                {
                    throw std::runtime_error("kh02_stats_level_context");
                }
                library->ProcessEvent(fn, data.data());
                auto next = read_function_int_param(fn, data.data(), L"ReturnValue");
                if (!next || *next < 0 || *next > kh02_maximum_battle_level)
                {
                    throw std::runtime_error("kh02_stats_level_return");
                }
                // Startup preloads happen at the title screen, before a battle level exists.
                // Re-read at the owned host BeginPlay, before its streamed gameplay begins.
                if (*next)
                {
                    apply(static_cast<int>(*next));
                }
                battle_level.mark_ready();
                tick_hero(kh02_story_player(context));
                error.clear();
                return true;
            }
            catch (const std::exception& e)
            {
                error = e.what();
                throw;
            }
        }

        std::string snapshot() const
        {
            nlohmann::json result{
                {"ready", battle_level.ready()},
                {"battle_level", level},
                {"native_base_hp", base_hp},
                {"aqua_native_battle_level", native_level},
                {"hero_battle_level", profile_level},
                {"health_changes", health_changes},
                {"tide_reference",
                 {{"table", "/Game/DataTable/Enemy/Base/e_dw401_BaseDataTable_s1.e_dw401_BaseDataTable_s1"},
                  {"level", kh02_tide_balance::reference_level},
                  {"base_hp", tide_reference_base_hp}}},
                {"error", error},
                {"rows", nlohmann::json::array()}};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            result["work_counts"] = {{"asset_path_lookups", assets.path_lookups()},
                                     {"asset_cache_hits", assets.cache_hits()},
                                     {"level_applications", level_applications},
                                     {"unchanged_level_requests", unchanged_level_requests}};
#endif
            for (const auto& r : rows)
            {
                FName n;
                static_assert(sizeof(n) == sizeof(r.name));
                std::memcpy(&n, &r.name, sizeof(n));
                result["rows"].push_back(
                    {{"table", is_uobject_valid_fast(r.table.Get()) ? to_utf8(r.table.Get()->GetPathName()) : ""},
                     {"row", to_utf8(n.ToString())},
                     {"original_hp", r.original},
                     {"authored_rate", r.rate},
                     {"adapted_hp",
                      r.reference_hp
                          ? kh02_tide_balance::scaled_hp(base_hp, r.reference_hp, tide_reference_base_hp).value_or(0)
                          : std::max(1, int(double(base_hp) * r.rate))},
                     {"reference_hp", r.reference_hp}});
                if (r.reference_hp)
                {
                    auto& combat = result["rows"].back()["combat"];
                    combat = nlohmann::json::object();
                    for (const auto& value : r.combat)
                    {
                        combat[to_utf8(value.field)] = {{"original", value.original}, {"adapted", value.adapted}};
                    }
                }
            }
            return result.dump();
        }
    };

    Kh02EnemyStats::Kh02EnemyStats(std::function<Kh02BattleLevelSettings()> settings)
        : impl(std::make_unique<Impl>(std::move(settings)))
    {
    }

    Kh02EnemyStats::~Kh02EnemyStats() = default;

    bool Kh02EnemyStats::prepare(UObject* c)
    {
        return impl->prepare(c);
    }

    void Kh02EnemyStats::tick_hero(UObject* hero)
    {
        try
        {
            impl->tick_hero(hero);
        }
        catch (const std::exception& failure)
        {
            impl->error = failure.what();
        }
    }

    void Kh02EnemyStats::release_hero(UObject* context)
    {
        impl->release_hero(context);
    }

    auto Kh02EnemyStats::experience_levels(UObject* hero, int sora_level) -> std::pair<int, int>
    {
        auto* world = kh02_actor_world(hero);
        if (!is_uobject_valid_fast(world) || impl->profile_world.Get() != world)
        {
            throw std::runtime_error("kh02_experience_battle_profile_unbound");
        }
        const bool final_boss =
            world->GetPathName() == L"/Game/Maps/APKH02Mirrors/APKH02DestinyIslands.APKH02DestinyIslands";
        const auto sora_target = kh02_hero_battle_level(impl->native_level, sora_player_id, sora_level,
                                                     impl->settings ? impl->settings() : Kh02BattleLevelSettings{},
                                                     final_boss);
        if (!sora_target)
        {
            throw std::runtime_error("kh02_experience_sora_battle_level");
        }
        return {impl->read_level(hero), *sora_target};
    }

    std::string Kh02EnemyStats::snapshot() const
    {
        return impl->snapshot();
    }

    void Kh02EnemyStats::set_active(bool active)
    {
        if (!active)
        {
            impl->release_hero(kh02_current_world());
        }
        impl->battle_level.set_active(active);
    }
} // namespace kh3ap
