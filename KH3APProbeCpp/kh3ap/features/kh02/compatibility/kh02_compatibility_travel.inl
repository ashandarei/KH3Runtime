// Private Kh02Story compatibility invocation; included inside CompatibilityInvocation.
void parse_travel()
{
    if (adapter == Adapter::StoryTravel)
    {
        auto name_literal = [&]() { return operands.name("kh02_travel_operand").ToString(); };
        const auto destination = name_literal();
        const auto start_tag = name_literal();
        if (destination != L"/Game/Zevels/dw/dw_59/dw_59" || start_tag != L"dw_59_Lv_Start_01" ||
            script_end - cursor < kh02compat::vm::byte_operand_bytes ||
            *cursor++ != kh02compat::opcode::byte_constant || *cursor++ != 0 ||
            !frame->Node->GetPathName().starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_gameflow."))
        {
            throw std::runtime_error("kh02_travel_source");
        }
    }
    if (adapter == Adapter::MirrorJump)
    {
        auto literal = [&]() { return operands.name("kh02_mirror_jump_operand").ToString(); };
        const auto destination = literal();
        const auto destination_tag = literal();
        if (script_end - cursor < kh02compat::vm::byte_operand_bytes || *cursor++ != kh02compat::opcode::byte_constant)
        {
            throw std::runtime_error("kh02_mirror_jump_fade");
        }
        const int fade_kind = *cursor++;
        auto* source_world = kh02_actor_world(frame->Object);
        if (story->mirror_travel.stage(source_world, frame->Node->GetPathName(), destination, destination_tag,
                                       fade_kind, story->source_flags.value(L"gameflow_REBELLIS_CLEAR")))
        {
            // This is the original special battle's successful return site.
            kh02_tracking::complete(story->within_challenges, kh02_challenge_id::special_battle_area);
            story->publish_mirror_progress();
        }
    }
    if (adapter == Adapter::ApplyMapSet)
    {
        const auto execution = latent_info.execution.ToString();
        const auto caller_path = frame->Object->GetPathName();
        if (execution != L"ExecuteUbergraph_dw_02_gameflow" && execution != L"ExecuteUbergraph_cutscene_dw02_dw012" &&
            execution != L"ExecuteUbergraph_dw_03_gameflow" && execution != L"ExecuteUbergraph_dw_05" &&
            execution != L"ExecuteUbergraph_dw_05_gameflow" && execution != L"ExecuteUbergraph_dw_08" &&
            execution != L"ExecuteUbergraph_dw_08_gameflow" && execution != L"ExecuteUbergraph_dw_09" &&
            execution != L"ExecuteUbergraph_dw_09_gameflow")
        {
            throw std::runtime_error("kh02_compat_mapset_latent_identity");
        }
        auto name_literal = [&]() { return operands.name("kh02_compat_mapset_name_operand").ToString(); };
        mapset_asset = object();
        mapset_command = name_literal();
        const bool mirror = is_uobject_valid_fast(mapset_asset) &&
                            mapset_asset->GetPathName() == L"/Game/Zevels/dw/dw_03/mapset/ms_dw_03.ms_dw_03" &&
                            execution == L"ExecuteUbergraph_dw_03_gameflow";
        const bool corridor_start =
            execution == L"ExecuteUbergraph_dw_05" && caller_path.starts_with(L"/Game/Zevels/dw/dw_05/dw_05.");
        // Original Corridor flips use the same mapset from three
        // gameflow latent sites: Reverse -> 2204/677, Normal -> 4846.
        const bool corridor_flow =
            execution == L"ExecuteUbergraph_dw_05_gameflow" &&
            caller_path.starts_with(L"/Game/Zevels/dw/dw_05/umap/dw_05_gameflow.") &&
            ((mapset_command == L"dw_05_Reverse" && (latent_info.linkage == 2204 || latent_info.linkage == 677)) ||
             (mapset_command == L"dw_05_Normal" && latent_info.linkage == 4846));
        const bool corridor = is_uobject_valid_fast(mapset_asset) &&
                              mapset_asset->GetPathName() == L"/Game/Zevels/dw/dw_05/mapset/ms_dw_05.ms_dw_05" &&
                              (corridor_start || corridor_flow);
        bool forest = false;
        if (is_uobject_valid_fast(mapset_asset) &&
            mapset_asset->GetPathName() == L"/Game/Zevels/dw/dw_08/mapset/ms_dw_08.ms_dw_08" &&
            (caller_path.starts_with(L"/Game/Zevels/dw/dw_08/dw_08.") ||
             caller_path.starts_with(L"/Game/Zevels/dw/dw_08/umap/dw_08_gameflow.")))
        {
            for (const auto& site : kh02forest_metadata::mapsets)
            {
                forest |= execution == site.execution && latent_info.linkage == site.linkage &&
                          FName(mapset_command.c_str(), FNAME_Add) == FName(site.command, FNAME_Add);
            }
        }
        bool ending = false;
        if (is_uobject_valid_fast(mapset_asset) &&
            mapset_asset->GetPathName() == L"/Game/Zevels/dw/dw_09/mapset/ms_dw_09.ms_dw_09" &&
            (caller_path.starts_with(L"/Game/Zevels/dw/dw_09/dw_09.") ||
             caller_path.starts_with(L"/Game/Zevels/dw/dw_09/umap/dw_09_gameflow.")))
        {
            for (const auto& site : kh02ending_metadata::mapsets)
            {
                ending |= execution == site.execution && latent_info.linkage == site.linkage &&
                          FName(mapset_command.c_str(), FNAME_Add) == FName(site.command, FNAME_Add);
            }
        }
        if (!is_uobject_valid_fast(mapset_asset) ||
            (!mirror && !corridor && !forest && !ending &&
             mapset_asset->GetPathName() != L"/Game/Zevels/dw/dw_02/mapset/ms_dw_02.ms_dw_02") ||
            mapset_asset->GetClassPrivate()->GetPathName() != L"/Script/TresGame.TresMapSet")
        {
            throw std::runtime_error("kh02_compat_mapset_operand_asset:" + path(mapset_asset) + ":" +
                                     to_utf8(mapset_command) + ":" + std::to_string(latent_info.linkage));
        }
        if (forest || ending)
        {
        } // Exact command/continuation pair validated above.
        else if (corridor)
        {
            if (mapset_command != L"dw_05_Normal" && mapset_command != L"dw_05_Reverse")
            {
                throw std::runtime_error("kh02_corridor_mapset_command");
            }
        }
        else if (mirror)
        {
            if (mapset_command != L"INIT" && mapset_command != L"dw_03_Saloon" && mapset_command != L"BattleArea01" &&
                mapset_command != L"BattleArea02" && mapset_command != L"BattleArea03" &&
                mapset_command != L"BattleArea04")
            {
                throw std::runtime_error("kh02_mirror_mapset_command");
            }
        }
        else if (mapset_command != L"dw02_CS_12_Start" && mapset_command != L"dw02_CS_12_End" &&
                 mapset_command != L"dw02_Get_Style" && mapset_command != L"dw02_Get_Slide" &&
                 mapset_command != L"dw02_Get_Shoot" && mapset_command != L"dw02_Get_Flight" &&
                 mapset_command != L"dw02_DevilTower")
        {
            throw std::runtime_error("kh02_compat_mapset_command_not_audited:" + to_utf8(mapset_command));
        }
    }
}

void execute_travel()
{
    if (adapter == Adapter::LoadingReady)
    {
        if (!frame->Node->GetPathName().starts_with(L"/Game/Zevels/dw/"))
        {
            throw std::runtime_error("kh02_loading_close_caller");
        }
        story->map_session.source_loading_ready(frame->Object);
    }
    if (adapter == Adapter::MirrorJump)
    {
        story->mirror_travel.commit();
    }
    if (adapter == Adapter::ApplyMapSet)
    {
        auto* mapset = mapset_asset;
        if (!is_uobject_valid_fast(mapset) ||
            mapset->GetClassPrivate()->GetPathName() != L"/Script/TresGame.TresMapSet")
        {
            throw std::runtime_error("kh02_compat_mapset_asset");
        }
        if (mapset_command == L"dw02_CS_12_Start")
        {
            story->bind_collapse();
        }
        if (mapset_command == L"dw02_DevilTower")
        {
            kh02_prepare_castle_boss_streams(story->gameplay.world.Get(), story->source_flags, story->encounter_proof);
        }
        ReflectedCall apply(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                            L"TresApplyMapSet");
        apply.object(L"WorldContextObject", frame->Object);
        apply.object(L"MapSet", mapset);
        apply.name(L"cmdName", mapset_command.c_str());
        apply.boolean(L"isOneFrameExecute", false);
        auto* latent = apply.param(L"LatentInfo");
        constexpr int mapset_latent_offset = sizeof(UObject*);
        if (!latent || latent->GetElementSize() != kh02compat::vm::native_latent_bytes ||
            latent->GetOffset_Internal() != mapset_latent_offset)
        {
            throw std::runtime_error("kh02_compat_mapset_latent_layout");
        }
        std::memcpy(latent->ContainerPtrToValuePtr<void>(apply.parameters.data()), &latent_info, sizeof(latent_info));
        apply.run();
        ++story->mapset_calls;
        story->last_mapset = {{"asset", path(mapset)},
                              {"command", to_utf8(mapset_command)},
                              {"source", path(frame->Object)},
                              {"execution", to_utf8(latent_info.execution.ToString())},
                              {"linkage", latent_info.linkage}};
    }
    if (adapter == Adapter::StoryTravel)
    {
        story->postboss.state.request_exit(story->source_flags.value(L"gameflow_DW"),
                                           story->source_flags.value(L"gameflow_DW_GEAR_GETCOUNT"));
        story->encounter_proof["source_exit_requested"] = true;
        // Defer travel until this source VM stack and its natural
        // cutscene/skip cleanup have returned to the game thread.
    }
}
