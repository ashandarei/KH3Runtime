#include "kh02_package_adapter.h"
#include "kh02_package_format.h"
#include <kh02/asset_compatibility.h>
#include <nlohmann/json.hpp>
#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>
#include <cmath>
#include <map>
#include <optional>
#include <set>

namespace kh02asset
{
    namespace
    {
        using namespace package;
        using Json = nlohmann::json;
        using Renames = std::map<std::string, std::string>;

        auto rules() -> const Json&
        {
            static const auto value = Json::parse(compatibility_rules);
            return value;
        }

        auto digest(std::span<const uint8_t> bytes) -> std::string
        {
            std::array<uint8_t, 32> hash{};
            require(bytes.size() <= std::numeric_limits<ULONG>::max(), "kh02_package_hash_size");
            require(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                               const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()),
                               hash.data(), static_cast<ULONG>(hash.size())) >= 0,
                    "kh02_package_hash_failed");
            constexpr char hex[] = "0123456789abcdef";
            std::string result;
            result.reserve(hash.size() * 2);
            for (const auto byte : hash)
            {
                result.push_back(hex[byte >> 4]);
                result.push_back(hex[byte & 15]);
            }
            return result;
        }

        void guard(std::span<const uint8_t> bytes, const Json& expected)
        {
            require(expected.is_string() ||
                        (expected.is_array() && !expected.empty() && expected.size() <= 4),
                    "kh02_package_compatibility_hash_type");
            const auto actual = digest(bytes);
            const auto matches = [&](const Json& value)
            {
                require(value.is_string() && value.get_ref<const std::string&>().size() == 64,
                        "kh02_package_compatibility_hash_value");
                return actual == value.get_ref<const std::string&>();
            };
            const bool accepted = expected.is_array()
                                      ? std::any_of(expected.begin(), expected.end(), matches)
                                      : matches(expected);
            if (!accepted)
            {
                throw std::runtime_error("kh02_package_compatibility_hash actual=" + actual +
                                         " expected=" + expected.dump());
            }
        }

        auto find_rule(std::string_view group, std::string_view key) -> const Json*
        {
            const auto& collection = rules().at(std::string(group));
            const auto found = collection.find(std::string(key));
            return found == collection.end() ? nullptr : &*found;
        }

        auto relocate(std::string value, bool shared_parent = true) -> std::string
        {
            const auto identity = value.substr(0, value.find('.'));
            const auto& shared = rules().at("shared_assets");
            if (shared_parent && std::find(shared.begin(), shared.end(), identity) != shared.end())
            {
                return value;
            }
            for (std::size_t i = 0; i + 6 < value.size(); ++i)
            {
                if (value[i] == '/' && (value[i + 1] == 'G' || value[i + 1] == 'g') &&
                    (value[i + 2] == 'A' || value[i + 2] == 'a') &&
                    (value[i + 3] == 'M' || value[i + 3] == 'm') &&
                    (value[i + 4] == 'E' || value[i + 4] == 'e') && value[i + 5] == '/' &&
                    value[i + 6] != '/')
                {
                    value.replace(i, 7, "/Game/Z");
                    i += 6;
                }
            }
            return value;
        }

        struct Context
        {
            std::string id;
            std::span<const uint8_t> original_header, original_exports;
            Header info;
            AdaptedPackage result;
            Renames native;
            std::map<int32_t, std::string> local;
            std::set<int32_t> function_locals;

            Context(std::string_view name, std::span<const uint8_t> header,
                    std::span<const uint8_t> exports)
                : id(name), original_header(header), original_exports(exports), info(parse(header)),
                  result{{header.begin(), header.end()}, {exports.begin(), exports.end()}}
            {
                require(exports.size() <= maximum_export_bytes, "kh02_package_export_limit");
                for (std::size_t i = 0; i < info.export_count; ++i)
                {
                    const auto at = info.export_start + i * export_stride;
                    const auto owner = get<int32_t>(original_header, at + 12);
                    const auto name = get<int32_t>(original_header, at + 16);
                    info.name(name);
                    require(owner >= -static_cast<int32_t>(info.imports.size()) &&
                                owner <= static_cast<int32_t>(info.export_count),
                            "kh02_package_export_owner");
                    if (owner <= 0)
                    {
                        continue;
                    }
                    const auto type = get<int32_t>(original_header,
                                                   info.export_start + (owner - 1) * export_stride);
                    if (type < 0)
                    {
                        require(-static_cast<int64_t>(type) <=
                                    static_cast<int64_t>(info.imports.size()),
                                "kh02_package_export_class");
                        if (info.name(info.imports[-type - 1].name) == "Function")
                        {
                            function_locals.insert(name);
                        }
                    }
                }
            }

            void import_value(std::size_t index, std::size_t field, int32_t value)
            {
                put(result.header, info.import_start + index * import_stride + field, value);
            }

            void guard_pair(const Json& expected) const
            {
                guard(original_header, expected.at(0));
                guard(original_exports, expected.at(1));
            }

            auto function_local(int32_t name) const -> bool
            {
                return function_locals.contains(name);
            }

            void engine_class(std::string_view old_type, std::string_view replacement)
            {
                native[std::string(old_type)] = replacement;
                native["Default__" + std::string(old_type)] =
                    "Default__" + std::string(replacement);
                const auto engine = info.import_index("/Script/Engine");
                const auto engine_name = info.name_index("/Script/Engine");
                const auto path = "/Script/TresGame." + std::string(old_type);
                const auto default_path = "/Script/TresGame.Default__" + std::string(old_type);
                for (std::size_t i = 0; i < info.imports.size(); ++i)
                {
                    const auto& entry = info.imports[i];
                    if (entry.path == path || entry.path == default_path)
                    {
                        import_value(i, 16, engine);
                    }
                    if (info.name(entry.kind) == old_type)
                    {
                        import_value(i, 0, engine_name);
                    }
                }
            }

            void nav_data()
            {
                native["TresUINaviMapTextureData"] = "DataAsset";
                native["Default__TresUINaviMapTextureData"] = "Default__DataAsset";
                const auto engine_name = info.name_index("/Script/Engine");
                for (std::size_t i = 0; i < info.imports.size(); ++i)
                {
                    if (info.imports[i].path == "/Script/TresGame")
                    {
                        import_value(i, 20, engine_name);
                    }
                    if (info.name(info.imports[i].kind) == "TresUINaviMapTextureData")
                    {
                        import_value(i, 0, engine_name);
                    }
                }
            }

            void challenge_return(bool reject_readers)
            {
                const auto target = info.import_index("/Script/TresGame.TresChallengeReturnState");
                if (reject_readers)
                {
                    require(std::none_of(info.imports.begin(), info.imports.end(),
                                         [&](const Import& entry)
                                         {
                                             return entry.outer == target;
                                         }),
                            "kh02_package_challenge_return_read");
                }
                import_value(-target - 1, 16, info.import_index("/Script/CoreUObject"));
            }

            void challenge_library()
            {
                native["TresChallengeStatics"] = "TresGameBlueprintLibrary";
                native["Default__TresChallengeStatics"] = "Default__TresGameBlueprintLibrary";
            }
        };

        auto vm_call(uint8_t opcode, int32_t target, uint8_t terminator) -> Bytes
        {
            Bytes result{opcode};
            append(result, target);
            result.push_back(terminator);
            return result;
        }

        void look_at(Context& context)
        {
            for (const auto function : {"StartIKinemaLookAt", "EndIKinemaLookAt"})
            {
                Bytes signature{0x1b};
                append(signature, context.info.name_index(function));
                append<int32_t>(signature, 0);
                const auto calls = occurrences(context.original_exports, signature);
                require(!calls.empty(), "kh02_package_look_at_call_missing");
                for (const auto at : calls)
                {
                    const auto operand = at + signature.size();
                    const auto size = std::string_view(function).starts_with("Start") ? 20U : 6U;
                    range(context.original_exports, operand, size);
                    const auto args = context.original_exports.subspan(operand, size);
                    const auto blend = get<float>(args, 1);
                    require(args.front() == 0x1e && args.back() == 0x16 && std::isfinite(blend) &&
                                blend >= 0 && blend <= 1,
                            "kh02_package_look_at_operand");
                    if (size == 20)
                    {
                        require(args[5] == 0x01 && args[10] == 0x21,
                                "kh02_package_look_at_start_shape");
                        const auto target = get<int32_t>(args, 6);
                        require(target > 0 &&
                                    static_cast<std::size_t>(target) <= context.info.export_count,
                                "kh02_package_look_at_target");
                        const auto type =
                            get<int32_t>(context.original_header,
                                         context.info.export_start + (target - 1) * export_stride);
                        require(type < 0 &&
                                    -static_cast<int64_t>(type) <=
                                        static_cast<int64_t>(context.info.imports.size()) &&
                                    context.info.imports[-type - 1].path ==
                                        "/Script/CoreUObject.ObjectProperty",
                                "kh02_package_look_at_target_type");
                        require(context.info.name(get<int32_t>(args, 11)) == "None" &&
                                    get<int32_t>(args, 15) == 0,
                                "kh02_package_look_at_bone");
                    }
                }
            }
            context.native["StartIKinemaLookAt"] = "KH02_LookStart";
            context.native["EndIKinemaLookAt"] = "KH02_LookEnd";
        }

        void semantic_header_rules(Context& context)
        {
            auto& native = context.native;
            const auto& info = context.info;
            const auto& id = context.id;
            if (const auto* guarded = find_rule("guards", id))
            {
                context.guard_pair(*guarded);
            }
            const auto* mirror = find_rule("mirror", id);
            if (mirror)
            {
                guard(context.original_header, mirror->at("header"));
                guard(context.original_exports, mirror->at("exports"));
                if (mirror->at("challenge").get<bool>())
                {
                    context.challenge_library();
                    native.insert({{"GetChallengeState", "KH02CS"},
                                   {"Clear", "KH02CC"},
                                   {"TryClear", "KH02Try"},
                                   {"Start", "KH02S"},
                                   {"EventStart", "KH02COn"},
                                   {"EventEnd", "KH02COff"},
                                   {"CallFunc_GetChallengeState_ReturnValue", "KH02CSResult"}});
                    for (const auto number : {"07", "08", "09", "10"})
                    {
                        if (id ==
                            "Levels/dw/dw_" + std::string(number) + "/umap/dw_" + number + "_enemy")
                        {
                            native["BP_TresGetVictoryBonus"] = "KH02_NoBonus";
                        }
                    }
                    if (id == "Blueprints/Gimmick/dw/g_dw_Thorn/g_dw_ThornBase")
                    {
                        native["IncrementItem"] = "KH02_ThornHit";
                        native["TresChallengeReturnState"] = "Vector";
                        context.challenge_return(true);
                    }
                    for (int32_t i = 0; i < static_cast<int32_t>(info.names.size()); ++i)
                    {
                        if (info.names[i].ascii.starts_with("CallFunc_") &&
                            context.function_local(i))
                        {
                            context.local[i] = "KH02Local" + std::to_string(i);
                        }
                    }
                }
                if (mirror->at("prepare_volume").get<bool>())
                {
                    context.engine_class("TresPrepareMapVolume", "TriggerVolume");
                }
                if (mirror->at("nav_data").get<bool>())
                {
                    context.nav_data();
                }
                if (mirror->value("player_look_at", false))
                {
                    look_at(context);
                }
                if (id == "Levels/dw/dw_02/umap/dw_02_camera_ms200")
                {
                    const auto index =
                        info.import_index("/Script/TresGame.TresCharPawnBase.GetStateID");
                    require(
                        occurrences(context.original_exports, vm_call(0x1c, index, 0x16)).size() ==
                            3,
                        "kh02_package_rooftop_state_calls");
                    native["GetStateID"] = "KH02_RoofState";
                }
                if (mirror->value("fieldvoice_component", false))
                {
                    context.engine_class("TresFieldVoiceComponent", "ActorComponent");
                    native.erase("Default__TresFieldVoiceComponent");
                    native["m_pFieldVoiceComponent"] = "Default__ActorComponent";
                    const auto target = info.import_index(
                        "/Script/TresGame.Default__TresFieldVoiceVolume.m_pFieldVoiceComponent");
                    context.import_value(-target - 1, 16, info.import_index("/Script/Engine"));
                }
            }
            if (!mirror || !mirror->value("player_look_at", false))
            {
                for (int32_t i = 0; i < static_cast<int32_t>(info.names.size()); ++i)
                {
                    if (info.names[i].ascii == "StartIKinemaLookAt" ||
                        info.names[i].ascii == "EndIKinemaLookAt")
                    {
                        Bytes signature{0x1b};
                        append(signature, i);
                        append<int32_t>(signature, 0);
                        require(occurrences(context.original_exports, signature).empty(),
                                "kh02_package_unaudited_look_at");
                    }
                }
            }
            if (const auto* navigation = find_rule("navigation_levels", id))
            {
                context.guard_pair(*navigation);
                native = {{"ReceiveBeginPlay", "KH02_NavBegin"}, {"ReceiveTick", "KH02_NavTick"}};
                context.engine_class("TresPrepareMapVolume", "TriggerVolume");
                if (id == "Levels/dw/dw_03/dw_03")
                {
                    native["cutscene_dw03_dw020_Evt_dw_03_gameflow"] = "KH02_NavScene";
                    native["cutscene_dw03_dw020_Evt_dw_03_gameflow_skip"] = "KH02_NavSkip";
                }
            }
            if (const auto* data = find_rule("navigation_data", id))
            {
                guard(context.original_header, *data);
                native.clear();
                context.nav_data();
            }
            if (id == "Levels/dw/dw_02/umap/dw_02_enemy_09")
            {
                native = {{"TryClear", "KH02BC"},
                          {"CallFunc_TryClear_ReturnValue", "KH02_BossResult"},
                          {"BP_TresGetVictoryBonus", "KH02_NoBonus"}};
                context.challenge_library();
            }
            if (id == "Levels/dw/dw_03/umap/dw_03_gameflow" ||
                id == "Levels/dw/dw_03/umap/dw_03_challenge")
            {
                native = {{"GetChallengeState", "KH02CS"},
                          {"Clear", "KH02CC"},
                          {"TryClear", "KH02Try"},
                          {"Start", "KH02CStart"},
                          {"CallFunc_GetChallengeState_ReturnValue", "KH02CSResult"},
                          {"CallFunc_GetChallengeState_ReturnValue2", "KH02CSResult2"},
                          {"CallFunc_GetChallengeState_ReturnValue3", "KH02CSResult3"}};
                context.challenge_library();
            }
            if (id == "Blueprints/Gimmick/g_SimpleReactorBase" ||
                id == "Maps/dw/uasset/Blueprints/BP_dw_Pro_GimmickLightBase")
            {
                native = {{"IncrementItem", id == "Blueprints/Gimmick/g_SimpleReactorBase"
                                                ? "KH02_CItem"
                                                : "KH02_LampHit"},
                          {"TresChallengeReturnState", "Vector"}};
                context.challenge_library();
                context.challenge_return(id == "Blueprints/Gimmick/g_SimpleReactorBase");
            }
            if (info.has_import("/Script/TresGame.TresNpcActionDefinitionMagic"))
            {
                require(id == "Blueprints/Npc/n_dw003/BT/n_dw003_Action1_Curaga",
                        "kh02_package_npc_magic");
                native = {{"TresNpcActionDefinitionMagic", "TresNpcAttackDefinitionMagic"},
                          {"Default__TresNpcActionDefinitionMagic",
                           "Default__TresNpcAttackDefinitionMagic"},
                          {"m_ActionAnimData", "m_AttackAnimData"}};
            }
            if (id == "Levels/dw/dw_05/dw_05")
            {
                context.local[info.name_index("CallFunc_BP_TresGetBattleLevel_ReturnValue")] =
                    "KH02LevelResult";
            }
            if (id == "Effects/enemy/e_dw407/par/ve_dw407_lsr_bem0")
            {
                const auto mesh = info.import_index("/Game/Effects/trescom/mdl/omd_grd_100x01_0");
                const auto name = info.imports[-mesh - 1].name;
                for (const auto& entry : info.imports)
                {
                    require(info.name(entry.kind) != "Package" || entry.name != name ||
                                entry.path == "/Game/Effects/trescom/mdl/omd_grd_100x01_0",
                            "kh02_package_shared_mesh_name");
                }
                context.local[name] = info.name(name);
            }
            if (id.starts_with("Blueprints/Enemy/") &&
                info.has_import("/Script/TresGame.TresChrDataTableSet"))
            {
                Bytes expected;
                for (const auto name : {"m_DataType", "ByteProperty"})
                {
                    append(expected, info.name_index(name));
                    append<int32_t>(expected, 0);
                }
                append<int32_t>(expected, 8);
                append<int32_t>(expected, 0);
                append(expected, info.name_index("ETresChrDataTableSetType"));
                append<int32_t>(expected, 0);
                append<uint8_t>(expected, 0);
                append(expected, info.name_index("CDTS_TYPE_ENEMY"));
                append<int32_t>(expected, 0);
                require(context.original_exports.size() >= expected.size() &&
                            std::equal(expected.begin(), expected.end(),
                                       context.original_exports.begin()),
                        "kh02_package_enemy_table_tag");
                Bytes byte_name;
                append(byte_name, info.name_index("ByteProperty"));
                append<int32_t>(byte_name, 0);
                require(occurrences(context.original_exports, byte_name).size() == 1,
                        "kh02_package_enemy_table_type");
                native = {{"ByteProperty", "EnumProperty"}};
            }
        }

        auto rename(const Context& context, std::size_t index) -> std::string
        {
            const auto& value = context.info.names[index].ascii;
            auto result =
                context.local.contains(static_cast<int32_t>(index))
                    ? context.local.at(static_cast<int32_t>(index))
                : context.native.contains(value)
                    ? context.native.at(value)
                    : relocate(value, (value != "/Game/" + context.id ||
                                       context.id.starts_with("Shader/KHS_set/postprocess/")) &&
                                          context.id != "Shader/Material_set/NonStateMaterial_set/"
                                                        "SQEX_BaseEyelashes_NonState");
            static const Renames global = {{"GetTresCameraManager", "PlayerCameraManager"},
                                           {"TresUI_CloseWindow", "KH02_CloseWindow"},
                                           {"TresUI_PrepareVideo", "KH02_PrepareVideo"},
                                           {"TresUI_ResumeVideo", "KH02_ResumeVideo"},
                                           {"TresUI_WaitForVideoPrepare", "KH02_WaitVideoReady"},
                                           {"TresUI_WaitForVideoFinish", "KH02_WaitVideoEnd"},
                                           {"TresUI_StopVideo", "KH02_StopVideo"},
                                           {"TresUI_PlayVideo", "KH02_PlayVideo"},
                                           {"TresUI_IsPlayingVideo", "KH02_IsVideoPlaying"},
                                           {"TresSetMatineeForMovie", "KH02_MatineeForMovie"},
                                           {"TresApplyMapSet", "KH02_ApplyMapSet"}};
            if (const auto found = global.find(value); found != global.end())
            {
                result = found->second;
            }
            const auto& id = context.id;
            if (id.starts_with("Levels/dw/") && value == "TresUI_CloseLoadingScreen")
            {
                result = "KH02_LoadReady";
            }
            bool map_jump = id == "Levels/dw/dw_59/dw_59" || id == "Levels/dw/dw_60/dw_60";
            for (int area = 3; area <= 11; ++area)
            {
                const auto number = (area < 10 ? "0" : "") + std::to_string(area);
                map_jump |= id == "Levels/dw/dw_" + number + "/umap/dw_" + number + "_gameflow";
            }
            if (map_jump && value == "TresMapJump")
            {
                result = "KH02_MJump";
            }
            if (id == "Levels/dw/dw_60/dw_60" && value == "TresUnlockAchievement")
            {
                result = "KH02_NoAchievement";
            }
            if (id == "Levels/dw/dw_02/umap/dw_02_gameflow")
            {
                static const Renames intro = {{"BP_SetAbilityMagic", "KH02_NoMagic"},
                                              {"TresUI_OpenGameHelp", "KH02_SkipHelp"},
                                              {"TresUI_OpenMessage", "KH02_SkipNotice"},
                                              {"TresMapJump", "KH02_StoryTravel"}};
                if (const auto found = intro.find(value); found != intro.end())
                {
                    result = found->second;
                }
            }
            return result;
        }

        void rewrite_header(Context& context)
        {
            const auto& info = context.info;
            constexpr std::string_view camera =
                "/Script/TresGame.TresPlayerControllerBase.GetTresCameraManager";
            int32_t controller{};
            if (info.has_import(camera))
            {
                if (info.has_import("/Script/Engine.PlayerController"))
                {
                    controller = info.import_index("/Script/Engine.PlayerController");
                }
                else
                {
                    controller = info.import_index("/Script/TresGame.TresPlayerControllerBase");
                    for (const auto& entry : info.imports)
                    {
                        require(entry.outer != controller ||
                                    info.name(entry.name) == "GetTresCameraManager",
                                "kh02_package_controller_members");
                    }
                    context.local[info.name_index("TresPlayerControllerBase")] = "PlayerController";
                    context.import_value(-controller - 1, 16, info.import_index("/Script/Engine"));
                }
            }
            std::map<std::string, int32_t> kind_names;
            std::map<std::size_t, std::string> corrections;
            const auto& types = rules().at("property_types");
            for (std::size_t i = 0; i < info.imports.size(); ++i)
            {
                const auto& entry = info.imports[i];
                const auto found = types.find(entry.path);
                if (found == types.end() || *found == info.name(entry.kind))
                {
                    continue;
                }
                const auto kind = found->get<std::string>();
                require(info.name(entry.kind) == "ByteProperty" && kind == "EnumProperty",
                        "kh02_package_property_kind");
                corrections[i] = kind;
                if (kind_names.contains(kind))
                {
                    continue;
                }
                int32_t available = -1;
                for (int32_t n = 0; n < static_cast<int32_t>(info.names.size()); ++n)
                {
                    if (info.names[n].ascii == kind)
                    {
                        available = n;
                        break;
                    }
                }
                if (available < 0)
                {
                    for (int32_t n = 0; n < static_cast<int32_t>(info.names.size()); ++n)
                    {
                        if (info.names[n].ascii.starts_with("CallFunc_") &&
                            !context.local.contains(n) && context.function_local(n))
                        {
                            available = n;
                            context.local[n] = kind;
                            break;
                        }
                    }
                }
                require(available >= 0, "kh02_package_property_name_space");
                kind_names[kind] = available;
            }
            Bytes names;
            for (std::size_t i = 0; i < info.names.size(); ++i)
            {
                const auto& name = info.names[i];
                const auto changed = rename(context, i);
                if (changed == name.ascii)
                {
                    names.insert(names.end(), context.original_header.begin() + name.start,
                                 context.original_header.begin() + name.end);
                    continue;
                }
                require(name_hashes(name.ascii, name.wide) ==
                            std::array<uint16_t, 2>{name.insensitive, name.sensitive},
                        "kh02_package_source_name_hash");
                const auto count = static_cast<int32_t>(changed.size() + 1);
                append(names, name.wide ? -count : count);
                for (const auto character : changed)
                {
                    if (name.wide)
                    {
                        append<uint16_t>(names, static_cast<uint8_t>(character));
                    }
                    else
                    {
                        append<uint8_t>(names, static_cast<uint8_t>(character));
                    }
                }
                if (name.wide)
                {
                    append<uint16_t>(names, 0);
                }
                else
                {
                    append<uint8_t>(names, 0);
                }
                const auto hashes = name_hashes(changed, name.wide);
                append(names, hashes[0]);
                append(names, hashes[1]);
            }
            require(names.size() <= info.name_end - info.name_start,
                    "kh02_package_name_table_growth");
            std::fill(context.result.header.begin() + info.name_start,
                      context.result.header.begin() + info.name_end, 0);
            std::copy(names.begin(), names.end(), context.result.header.begin() + info.name_start);
            for (const auto& [i, kind] : corrections)
            {
                context.import_value(i, 8, kind_names.at(kind));
                context.import_value(i, 12, 0);
            }
            for (std::size_t i = 0; i < info.imports.size(); ++i)
            {
                if (info.imports[i].path == camera)
                {
                    context.import_value(i, 8, info.name_index("ObjectProperty"));
                    context.import_value(i, 12, 0);
                    context.import_value(i, 16, controller);
                }
                if (info.imports[i].path != "/Script/"
                                            "SQEXSEAD.Default__SQEXSEADAutoSeComponent."
                                            "SQEXSEADAutoSeComponentCallbackDefault0")
                {
                    continue;
                }
                int32_t native_class{};
                for (std::size_t e = 0; e < info.export_count; ++e)
                {
                    const auto at = info.export_start + e * export_stride;
                    if (get<int32_t>(context.original_header, at + 8) !=
                        -1 - static_cast<int32_t>(i))
                    {
                        continue;
                    }
                    const auto type = get<int32_t>(context.original_header, at);
                    require(type < 0 &&
                                -static_cast<int64_t>(type) <=
                                    static_cast<int64_t>(info.imports.size()) &&
                                info.imports[-type - 1].path ==
                                    "/Script/SQEXSEAD.SQEXSEADAutoSeComponentCallbackDefault",
                            "kh02_package_audio_template_class");
                    require(!native_class || native_class == type,
                            "kh02_package_audio_template_users");
                    const auto size = get<int64_t>(context.original_header, at + 28);
                    const auto offset = get<int64_t>(context.original_header, at + 36);
                    require(size == 12 &&
                                offset >= static_cast<int64_t>(context.original_header.size()),
                            "kh02_package_audio_template_size");
                    const auto start =
                        static_cast<std::size_t>(offset - context.original_header.size());
                    range(context.original_exports, start, 12);
                    require(get<int32_t>(context.original_exports, start) ==
                                    info.name_index("None") &&
                                get<int32_t>(context.original_exports, start + 4) == 0 &&
                                get<int32_t>(context.original_exports, start + 8) == 0,
                            "kh02_package_audio_template_overrides");
                    native_class = type;
                    put<int32_t>(context.result.header, at + 8, 0);
                }
                require(native_class < 0, "kh02_package_audio_template_unused");
                const auto source = info.import_start + (-native_class - 1) * import_stride;
                const auto target = info.import_start + i * import_stride;
                std::copy_n(context.result.header.begin() + source, import_stride,
                            context.result.header.begin() + target);
            }
            const auto verified = parse(context.result.header);
            const auto& aliases = rules().at("native_aliases");
            for (std::size_t i = 0; i < verified.imports.size(); ++i)
            {
                const auto& entry = verified.imports[i];
                if (verified.name(entry.kind) == "Function" && entry.path.starts_with("/Script/") &&
                    (verified.name(entry.name).starts_with("KH02") ||
                     entry.path != info.imports[i].path))
                {
                    require(std::find(aliases.begin(), aliases.end(), entry.path) != aliases.end(),
                            "kh02_package_unregistered_native_alias");
                }
            }
        }

        void rewrite_exports(Context& context)
        {
            const auto& info = context.info;
            const auto source = context.original_exports;
            auto& result = context.result.exports;
            const auto unique = [&](std::span<const uint8_t> needle)
            {
                const auto matches = occurrences(source, needle);
                require(matches.size() == 1, "kh02_package_property_match");
                return matches.front();
            };
            if (context.id == "Effects/level/dw/dwcom/gimmick/item/par/vg_dwex_cha_lst_par20")
            {
                guard(source, "cadaa09b87744d54ca4d5c3755af6b9928d05d09a6d993963dab23de7cac1fb2");
                auto field = property_tag(info, "GenerateSkeletalMesh", "ObjectProperty", 4);
                const auto field_size = field.size();
                append(field, info.import_index("/Game/Maps/dw/gimmick/g_dw_TreasureBox_s/mdl/"
                                                "g_dw_TreasureBox_s.g_dw_TreasureBox_s"));
                const auto at = unique(field);
                put(result, at, info.name_index("GenerateMesh"));
                put(result, at + field_size,
                    info.import_index("/Game/Effects/level/dw/dwcom/gimmick/item/mdl/"
                                      "hmd_dw03_box_00.hmd_dw03_box_00"));
            }
            if (context.id == "Blueprints/Gimmick/dw/g_dw_Thorn/g_dw_ThornB")
            {
                guard(source, "f9a0aa67a949f9a984d3ad208154776f3c3aef52e1f743990795eb8e6c44b66b");
                // Resolve the virtual call's FName from the local header. Only
                // this verified graph's integer continuation changes.
                Bytes signature{0x1b};
                append(signature, info.name_index("ExecuteUbergraph_g_dw_ThornB"));
                append<int32_t>(signature, 0);
                signature.push_back(0x1d);
                std::optional<std::size_t> callback;
                for (std::size_t i = 0; i < info.export_count; ++i)
                {
                    const auto entry = info.export_start + i * export_stride;
                    if (info.name(get<int32_t>(context.original_header, entry + 16)) !=
                        "ReceiveNotifyProjectileAttackHitBpEvent")
                    {
                        continue;
                    }
                    const auto offset = get<int64_t>(context.original_header, entry + 36);
                    const auto size = get<int64_t>(context.original_header, entry + 28);
                    require(offset >= static_cast<int64_t>(context.original_header.size()) &&
                                size >= 0,
                            "kh02_package_thorn_function_range");
                    const auto start =
                        static_cast<std::size_t>(offset - context.original_header.size());
                    range(source, start, static_cast<std::size_t>(size));
                    const auto matches = occurrences(
                        source.subspan(start, static_cast<std::size_t>(size)), signature);
                    require(!callback && matches.size() == 1, "kh02_package_thorn_function_call");
                    callback = start + matches.front();
                }
                require(callback.has_value(), "kh02_package_thorn_callback_missing");
                const auto at = *callback;
                require(get<uint8_t>(source, at + 14) == 0x16 &&
                            get<uint8_t>(source, at + 15) == 0x04 &&
                            get<uint8_t>(source, at + 16) == 0x0b &&
                            get<uint8_t>(source, at + 17) == 0x53,
                        "kh02_package_thorn_continuation_shape");
                put<int32_t>(result, at + 10, 407);
            }
            if (const auto* lighting = find_rule("cutscene_lighting", context.id))
            {
                guard(source, lighting->at("exports_sha256"));
                const auto start = lighting->at("export_offset").get<std::size_t>();
                const auto size = lighting->at("export_size").get<std::size_t>();
                range(source, start, size);
                const auto blob = source.subspan(start, size);
                guard(blob, lighting->at("export_sha256"));
                const auto in = property_tag(info, "InVal", "FloatProperty", 4);
                const auto out = property_tag(info, "OutVal", "FloatProperty", 4);
                std::size_t changed{};
                for (const auto at : occurrences(blob, in))
                {
                    const auto next = at + in.size() + sizeof(float);
                    range(blob, next, out.size() + sizeof(float));
                    require(std::equal(out.begin(), out.end(), blob.begin() + next),
                            "kh02_package_light_key_shape");
                    require(std::isfinite(get<float>(blob, at + in.size())) &&
                                std::isfinite(get<float>(blob, next + out.size())),
                            "kh02_package_light_key_value");
                    put<float>(result, start + next + out.size(), 0);
                    ++changed;
                }
                require(changed == lighting->at("key_count").get<std::size_t>(),
                        "kh02_package_light_key_count");
            }
            for (const auto group : {"effects_lighting", "navigation"})
            {
                if (const auto* rule = find_rule(group, context.id))
                {
                    guard(source, rule->at("exports_sha256"));
                    for (const auto& span : rule->at("ranges"))
                    {
                        const auto at = span.at("offset").get<std::size_t>();
                        const auto size = span.at("size").get<std::size_t>();
                        range(source, at, size);
                        guard(source.subspan(at, size), span.at("sha256"));
                        for (const auto& edit : span.at("floats"))
                        {
                            const auto offset = edit.at(0).get<std::size_t>();
                            range(source.subspan(at, size), offset, sizeof(float));
                            put(result, at + offset, edit.at(1).get<float>());
                        }
                    }
                }
            }
            if (const auto* environment = find_rule("environments", context.id))
            {
                guard(source, *environment);
                const auto field = property_tag(info, "MapBuildData", "ObjectProperty", 4);
                const auto at = unique(field) + field.size();
                const auto reference = get<int32_t>(source, at);
                require(reference >= -static_cast<int32_t>(info.imports.size()) &&
                            reference <= static_cast<int32_t>(info.export_count),
                        "kh02_package_environment_reference");
                put<int32_t>(result, at, 0);
            }
            const auto path_character = [](uint8_t value)
            {
                return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
                       (value >= '0' && value <= '9') || value == '_' || value == '.' ||
                       value == '/' || value == '-';
            };
            for (std::size_t at = 4; at + 6 < source.size(); ++at)
            {
                if (source[at] != '/' || (source[at + 1] != 'G' && source[at + 1] != 'g') ||
                    (source[at + 2] != 'A' && source[at + 2] != 'a') ||
                    (source[at + 3] != 'M' && source[at + 3] != 'm') ||
                    (source[at + 4] != 'E' && source[at + 4] != 'e') || source[at + 5] != '/')
                {
                    continue;
                }
                const auto count = get<int32_t>(source, at - 4);
                if (count <= 0 || count > 2048 ||
                    static_cast<std::size_t>(count) > source.size() - at ||
                    source[at + count - 1] != 0)
                {
                    continue;
                }
                // Match the same bounded FString path grammar as the catalog
                // authoring rules; unrelated binary sequences are untouched.
                std::size_t end = at + 6;
                while (end < source.size() && path_character(source[end]))
                {
                    ++end;
                }
                if (end == at + 6)
                {
                    continue;
                }
                std::string value(reinterpret_cast<const char*>(source.data() + at), count - 1);
                const auto changed = relocate(value);
                require(changed.size() == value.size(), "kh02_package_string_size");
                std::copy(changed.begin(), changed.end(), result.begin() + at);
                at = end - 1;
            }
            for (std::size_t i = 0; i < info.imports.size(); ++i)
            {
                if (info.imports[i].path ==
                    "/Script/TresGame.TresPlayerControllerBase.GetTresCameraManager")
                {
                    const auto index = -1 - static_cast<int32_t>(i);
                    const auto matches = occurrences(source, vm_call(0x1c, index, 0x16));
                    require(!matches.empty(), "kh02_package_camera_call_missing");
                    const auto replacement = vm_call(0x01, index, 0x0b);
                    for (const auto at : matches)
                    {
                        std::copy(replacement.begin(), replacement.end(), result.begin() + at);
                    }
                }
            }
        }
    } // namespace

    auto adapt_package(std::string_view id, std::span<const uint8_t> header,
                       std::span<const uint8_t> exports) -> AdaptedPackage
    {
        if (id == "Maps/dw/uasset/Textures/T_dw_TitleBG")
        {
            return {{header.begin(), header.end()}, {exports.begin(), exports.end()}};
        }
        Context context(id, header, exports);
        semantic_header_rules(context);
        rewrite_header(context);
        rewrite_exports(context);
        return std::move(context.result);
    }
} // namespace kh02asset
