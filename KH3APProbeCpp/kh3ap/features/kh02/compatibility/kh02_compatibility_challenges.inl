// Private Kh02Story compatibility invocation; included inside CompatibilityInvocation.
struct ChallengeContract
{
    static constexpr int source_completion_achievement = 4;
    static constexpr int active = 1;
    static constexpr int complete = 2;
    static constexpr int lamp = 22;
    static constexpr int white_gems = 28;
    static constexpr int thorns = 32;
    static constexpr int tower_spellweaver = 35;
    static constexpr int castle_secret = 37;
    static constexpr int mirror_secret = 39;
    static constexpr int tower_team_finish = 49;
    static constexpr int critical_tide = 50;
    static constexpr int lamp_goal = 20;
    static constexpr int white_gem_goal = 7;
    static constexpr int maximum_counter = 1'000'000;
    static constexpr size_t vector_result_bytes = 3 * sizeof(float);
};

void parse_challenges()
{
    if (adapter == Adapter::BlockAchievement)
    {
        if (!frame->Node->GetPathName().starts_with(L"/Game/Zevels/dw/dw_60/dw_60.") ||
            script_end - cursor < kh02compat::vm::byte_operand_bytes ||
            *cursor++ != kh02compat::opcode::byte_constant ||
            *cursor++ != ChallengeContract::source_completion_achievement)
        {
            throw std::runtime_error("kh02_source_achievement_operand");
        }
    }
    if (adapter == Adapter::TryChallenge)
    {
        const auto caller_path = frame->Node->GetPathName();
        if (!(caller_path.starts_with(L"/Game/Zevels/dw/dw_09/umap/dw_09_enemy.") ||
              caller_path.starts_with(L"/Game/Zevels/dw/dw_10/umap/dw_10_enemy.")) ||
            script_end - cursor < kh02compat::vm::byte_operand_bytes || *cursor++ != kh02compat::opcode::byte_constant)
        {
            throw std::runtime_error("kh02_source_try_challenge_caller");
        }
        integer_argument = *cursor++;
        if (integer_argument != ChallengeContract::tower_team_finish &&
            integer_argument != ChallengeContract::critical_tide)
        {
            throw std::runtime_error("kh02_source_try_challenge_id");
        }
    }
    if (adapter == Adapter::ChallengeStatus || adapter == Adapter::CompleteChallenge ||
        adapter == Adapter::StartChallenge || adapter == Adapter::EnableChallenge ||
        adapter == Adapter::DisableChallenge)
    {
        const auto caller_path = frame->Node->GetPathName();
        bool allowed_caller = false;
        for (int area : {3, 4, 5, 6, 7, 8, 9, 10, 11})
        {
            const auto prefix = L"/Game/Zevels/dw/dw_" + std::wstring(area < 10 ? L"0" : L"") + std::to_wstring(area) +
                                L"/umap/dw_" + std::wstring(area < 10 ? L"0" : L"") + std::to_wstring(area);
            allowed_caller |=
                caller_path.starts_with(prefix + L"_gameflow.") || caller_path.starts_with(prefix + L"_challenge.") ||
                caller_path.starts_with(prefix + L"_enemy.") || caller_path.starts_with(prefix + L"_camera_ms220.") ||
                (area == 8 && caller_path.starts_with(prefix + L"_camera_ms0131."));
        }
        if (script_end - cursor < kh02compat::vm::byte_operand_bytes || *cursor++ != kh02compat::opcode::byte_constant)
        {
            throw std::runtime_error("kh02_mirror_challenge_caller");
        }
        integer_argument = *cursor++;
        if (integer_argument < 0 || integer_argument >= challenge_count)
        {
            throw std::runtime_error("kh02_mirror_challenge_id");
        }
        if (!allowed_caller && !kh02_tracking::castle_challenge_site(adapter_id(), integer_argument, caller_path))
        {
            throw std::runtime_error("kh02_mirror_challenge_caller");
        }
    }
    if (adapter == Adapter::CollectChallengeItem)
    {
        if (!frame->Node->GetPathName().starts_with(L"/Game/Zlueprints/Gimmick/g_SimpleReactorBase.") ||
            script_end - cursor < kh02compat::vm::reference_operand_bytes ||
            *cursor++ != kh02compat::opcode::instance_variable || !is_uobject_valid_fast(frame->Object))
        {
            throw std::runtime_error("kh02_collectible_challenge_caller");
        }
        FProperty* challenge_property;
        std::memcpy(&challenge_property, cursor, sizeof(challenge_property));
        cursor += kh02compat::vm::reference_bytes;
        if (!is_uobject_valid_fast(reinterpret_cast<UObject*>(challenge_property)) ||
            challenge_property->GetName() != L"Challenge" || challenge_property->GetElementSize() != sizeof(uint8_t) ||
            frame->Object->GetPropertyByNameInChain(L"Challenge") != challenge_property)
        {
            throw std::runtime_error("kh02_collectible_challenge_property");
        }
        integer_argument = *challenge_property->ContainerPtrToValuePtr<uint8_t>(frame->Object);
        maximum_argument = integer();
        if (integer_argument < 0 || integer_argument >= challenge_count || maximum_argument != 1 ||
            cursor >= script_end || *cursor++ != kh02compat::opcode::true_constant)
        {
            throw std::runtime_error("kh02_collectible_challenge_count");
        }
    }
    if (adapter == Adapter::LampHit)
    {
        if (script_end - cursor < kh02compat::vm::byte_operand_bytes ||
            *cursor++ != kh02compat::opcode::byte_constant || *cursor++ != ChallengeContract::lamp)
        {
            throw std::runtime_error("kh02_lamp_challenge_item");
        }
        integer_argument = integer();
        if (integer_argument != 1 || cursor >= script_end || *cursor++ != kh02compat::opcode::true_constant)
        {
            throw std::runtime_error("kh02_lamp_challenge_increment");
        }
        if (!frame->Node->GetPathName().starts_with(L"/Game/Zaps/dw/uasset/Blueprints/BP_dw_Pro_GimmickLightBase."))
        {
            throw std::runtime_error("kh02_lamp_challenge_caller");
        }
    }
    if (adapter == Adapter::ThornHit)
    {
        if (!frame->Node->GetPathName().starts_with(L"/Game/Zlueprints/Gimmick/dw/g_dw_Thorn/g_dw_ThornBase.") ||
            script_end - cursor < kh02compat::vm::byte_operand_bytes ||
            *cursor++ != kh02compat::opcode::byte_constant || *cursor++ != ChallengeContract::thorns)
        {
            throw std::runtime_error("kh02_thorn_challenge_caller");
        }
        integer_argument = integer();
        if (integer_argument != 1 || cursor >= script_end || *cursor++ != kh02compat::opcode::true_constant)
        {
            throw std::runtime_error("kh02_thorn_challenge_increment");
        }
    }
    if (adapter == Adapter::BossChallenge)
    {
        if (script_end - cursor < kh02compat::vm::byte_operand_bytes ||
            *cursor++ != kh02compat::opcode::byte_constant || *cursor++ != ChallengeContract::tower_spellweaver ||
            !frame->Node->GetPathName().starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_enemy_09."))
        {
            throw std::runtime_error("kh02_boss_challenge_operand");
        }
    }
    if (adapter == Adapter::BlockVictoryBonus)
    {
        const auto caller_path = frame->Node->GetPathName();
        const bool tower = caller_path.starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_enemy_09.");
        const bool mirror = caller_path.starts_with(L"/Game/Zevels/dw/dw_07/umap/dw_07_enemy.");
        const bool forest = caller_path.starts_with(L"/Game/Zevels/dw/dw_08/umap/dw_08_enemy.");
        const bool depths = caller_path.starts_with(L"/Game/Zevels/dw/dw_09/umap/dw_09_enemy.");
        const bool islands = caller_path.starts_with(L"/Game/Zevels/dw/dw_10/umap/dw_10_enemy.");
        if ((!tower && !mirror && !forest && !depths && !islands) ||
            script_end - cursor < 1 + kh02compat::vm::name_operand_bytes || *cursor++ != kh02compat::opcode::self)
        {
            throw std::runtime_error("kh02_boss_bonus_caller");
        }
        const auto bonus_name = operands.name("kh02_boss_bonus_caller").ToString();
        if ((tower && bonus_name != L"BattleBonus_DW_dw_02_DevilTower") ||
            (mirror && bonus_name != L"BattleBonus_DW_dw_07_MirrorAqua_03" &&
             bonus_name != L"BattleBonus_DW_dw_07_MirrorAqua_04"))
        {
            throw std::runtime_error("kh02_boss_bonus_key");
        }
        if ((depths && bonus_name != L"BattleBonus_DW_dw_09_DevilTower") ||
            (islands && bonus_name != L"BattleBonus_DW_dw_10_DevilWave"))
        {
            throw std::runtime_error("kh02_ending_bonus_key");
        }
        if (forest && bonus_name != L"BattleBonus_DW_dw_08_Darkside" && bonus_name != L"BattleBonus_DW_dw_08_Darkball")
        {
            throw std::runtime_error("kh02_forest_bonus_key");
        }
    }
}

void execute_challenges()
{
    if (adapter == Adapter::EnableChallenge || adapter == Adapter::DisableChallenge)
    {
        story->challenge_events[integer_argument] = adapter == Adapter::EnableChallenge;
        story->challenge_tracker.bind(story->gameplay.pawn.Get(), story->gameplay.world.Get(), story->progress_session.revision());
        story->challenge_tracker.event(integer_argument, adapter == Adapter::EnableChallenge,
                                       frame->Node->GetPathName());
    }
    if (adapter == Adapter::TryChallenge)
    {
        const bool completed = story->challenge_tracker.team_finish(integer_argument);
        if (return_value)
        {
            *static_cast<uint8_t*>(return_value) = completed;
        }
        story->publish_mirror_progress();
    }
    if (adapter == Adapter::ChallengeStatus)
    {
        if (return_value)
        {
            *static_cast<uint8_t*>(return_value) = static_cast<uint8_t>(story->within_challenges[integer_argument]);
        }
    }
    if (adapter == Adapter::CompleteChallenge || adapter == Adapter::StartChallenge)
    {
        const int next_state =
            adapter == Adapter::CompleteChallenge ? ChallengeContract::complete : ChallengeContract::active;
        const bool state_changed = story->within_challenges[integer_argument] < next_state;
        if (adapter == Adapter::CompleteChallenge)
        {
            // The source starts this objective only on Critical. KH3
            // owns the difficulty, so verify its live value at victory.
            if (integer_argument == ChallengeContract::critical_tide)
            {
                if (!frame->Node->GetPathName().starts_with(L"/Game/Zevels/dw/dw_10/umap/dw_10_enemy."))
                {
                    throw std::runtime_error("The Critical Demon Tide objective requires its original victory site.");
                }
                ReflectedCall difficulty(
                    kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                    L"BP_GetGameLevel");
                auto* difficulty_result = difficulty.param(L"ReturnValue");
                if (!difficulty_result || difficulty_result->GetClass().GetName() != L"ByteProperty" ||
                    difficulty_result->GetElementSize() != sizeof(uint8_t))
                {
                    throw std::runtime_error("The current difficulty query has an unsupported result.");
                }
                difficulty.run();
                kh02_tracking::critical_tide_victory(
                    story->within_challenges,
                    *difficulty_result->ContainerPtrToValuePtr<uint8_t>(difficulty.parameters.data()));
            }
            else
            {
                story->within_challenges[integer_argument] =
                    std::max(story->within_challenges[integer_argument], next_state);
            }
        }
        else
        {
            story->within_challenges[integer_argument] =
                std::max(story->within_challenges[integer_argument], next_state);
        }
        if (return_value)
        {
            *static_cast<uint8_t*>(return_value) =
                state_changed && story->within_challenges[integer_argument] == next_state;
        }
    }
    if (adapter == Adapter::CollectChallengeItem)
    {
        auto& count = story->within_challenge_counts[integer_argument];
        count = std::min(ChallengeContract::maximum_counter, count + maximum_argument);
        // Original TresChallenge row 029 requires seven white gems.
        if (integer_argument == ChallengeContract::white_gems && count >= ChallengeContract::white_gem_goal ||
            integer_argument == ChallengeContract::castle_secret ||
            integer_argument == ChallengeContract::mirror_secret)
        {
            story->within_challenges[integer_argument] = ChallengeContract::complete;
        }
        if (return_value)
        {
            std::memset(return_value, 0, ChallengeContract::vector_result_bytes);
        }
    }
    if (adapter == Adapter::CompleteChallenge || adapter == Adapter::StartChallenge ||
        adapter == Adapter::CollectChallengeItem || adapter == Adapter::EnableChallenge ||
        adapter == Adapter::DisableChallenge)
    {
        story->publish_mirror_progress();
    }
    if (adapter == Adapter::LampHit)
    {
        ++story->lamp_challenge_count;
        auto& count = story->within_challenge_counts[ChallengeContract::lamp];
        count = std::min(count + 1, ChallengeContract::lamp_goal);
        story->within_challenges[ChallengeContract::lamp] =
            count >= ChallengeContract::lamp_goal ? ChallengeContract::complete : ChallengeContract::active;
        story->publish_mirror_progress();
        if (return_value)
        {
            std::memset(return_value, 0, ChallengeContract::vector_result_bytes);
        }
    }
    if (adapter == Adapter::ThornHit)
    {
        kh02_tracking::add(story->within_challenges, story->within_challenge_counts, ChallengeContract::thorns);
        story->publish_mirror_progress();
        if (return_value)
        {
            std::memset(return_value, 0, ChallengeContract::vector_result_bytes);
        }
    }
    if (adapter == Adapter::BossChallenge)
    {
        ++story->encounter_proof["original_challenge35_requests"].get_ref<Json::number_unsigned_t&>();
        if (return_value)
        {
            *static_cast<uint8_t*>(return_value) = false;
        }
    }
    if (adapter == Adapter::BlockVictoryBonus)
    {
        story->encounter_proof["source_victory_bonus_blocked"] = true;
    }
}
