// Diagnostic-only composition adapters. Domain handlers borrow explicit services;
// gameplay never dispatches string commands.
auto flow_command(UObject* pawn, UObject* world, Kh02DiagnosticAction action) -> std::optional<std::string>
{
    if (kh02_diagnostic_is_movie(action))
    {
        Kh02MovieDiagnosticActions::execute(
            pawn, world, action,
            {gameplay, cutscenes, movie_bridge, native_movie, movie_fixture, native_movie_fixture, movie_calls});
        return flow_snapshot(pawn).dump();
    }
    return Kh02CastleDiagnosticActions::execute(
        pawn, world, action,
        {entry, gameplay, castle, map_session, challenge_log, movie_bridge, source_flags, timed_inputs,
         within_challenges, story_targets, mesh_buffers, localization, native_before, compat_error, compat_warning,
         encounter_proof, movie_fixture, [this] { install_compat(); }, [this, pawn] { return flow_snapshot(pawn); }});
}

auto diagnostic_command(UObject* pawn, std::wstring_view action) -> std::optional<std::string>
{
    using Action = Kh02DiagnosticAction;
    using Group = Kh02DiagnosticGroup;
    const auto command = kh02_decode_diagnostic_action(action);
    if (command.group == Group::Invalid)
    {
        throw std::runtime_error("kh02_story_action");
    }
    if (!is_uobject_valid_fast(pawn))
    {
        pawn = opening_scene.source_owner();
    }
    auto* world = is_uobject_valid_fast(pawn) ? static_cast<AActor*>(pawn)->GetWorld() : nullptr;
    if (command.action == Action::FlowTestExperience)
    {
        if (!Kh02Story::map_overlay_ready(pawn) || !runtime_sora_control_ready(pawn) ||
            !Kh02Story::aqua_experience(pawn))
        {
            throw std::runtime_error("kh02_experience_requires_playable_aqua");
        }
        ReflectedCall paused(kh02_require_loaded_object(L"/Script/Engine.Default__GameplayStatics"), L"IsGamePaused");
        paused.object(L"WorldContextObject", pawn);
        paused.run();
        if (paused.boolean_result())
        {
            throw std::runtime_error("kh02_experience_requires_unpaused_game");
        }
        const auto before = *Kh02Story::aqua_experience(pawn);
        award_kh02_aqua_experience(pawn, 5000);
        return Json{{"kind", "native_aqua_experience_award"},
                    {"before", before},
                    {"after", *Kh02Story::aqua_experience(pawn)},
                    {"base_award", 5000}}
            .dump();
    }
    if (command.action == Action::FlowInventoryHealthFixture && is_uobject_valid_fast(world) &&
        (world->GetPathName() == L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01" ||
         world->GetPathName() == L"/Game/Maps/GardenOfAssemblage/umap/GoA_02.GoA_02") &&
        pawn->GetClassPrivate()->GetName() == L"p_ex001_Pawn_C")
    {
        auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
        const auto read_stat = [pawn](const wchar_t* method)
        {
            ReflectedCall query(pawn, method);
            query.run();
            return read_function_int_param(query.function, query.parameters.data(), L"ReturnValue").value_or(-1);
        };
        ReflectedCall paused(kh02_require_loaded_object(L"/Script/Engine.Default__GameplayStatics"), L"IsGamePaused");
        paused.object(L"WorldContextObject", pawn);
        paused.run();
        const auto maximum = read_stat(L"GetMaxHitPoint");
        if (!is_uobject_valid_fast(controller) ||
            try_read_reflected_object_property_value(controller, L"Pawn") != pawn || paused.boolean_result() ||
            read_stat(L"GetStateID") != 1 || read_stat(L"GetHitPoint") != 0 || maximum < 4 || maximum > 100'000)
        {
            throw std::runtime_error("kh02_health_recovery_requires_possessed_idle_zero_hp_garden_sora");
        }
        ReflectedCall health(pawn, L"SetHitPoint");
        health.integer(L"InHP", static_cast<int>(maximum / 2));
        health.run();
        const auto observed = read_stat(L"GetHitPoint");
        if (observed != maximum / 2)
        {
            throw std::runtime_error("kh02_health_recovery_readback");
        }
        return Json{{"kind", "idle_garden_health_fixture"}, {"hp_before", 0}, {"hp", observed}, {"max_hp", maximum}}
            .dump();
    }
    if (command.group == Group::Reset)
    {
        return reset_progress(pawn, world, command.action == Action::PlaytestInspect);
    }
    if (command.group == Group::Mirror)
    {
        return Kh02MirrorDiagnosticActions::execute(pawn, world, command.action,
                                                    {mirror, source_flags, timed_inputs, postboss.matinee_binding(),
                                                     [this, pawn] { return within_snapshot(pawn); }});
    }
    if (is_uobject_valid_fast(world) && mirror_room(world->GetPathName()) && mirror.initialized &&
        kh02_diagnostic_allows_mirror_flow(command.action))
    {
        return flow_command(pawn, world, command.action);
    }
    if (!is_uobject_valid_fast(world) ||
        world->GetPathName() != L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown")
    {
        throw std::runtime_error("kh02_story_requires_castle_host");
    }
    if (command.group == Group::Flow)
    {
        return flow_command(pawn, world, command.action);
    }
    return Kh02OpeningDiagnosticActions::execute(
        pawn, command.action, {opening_scene, event_aqua, opening_context(), [this] { return snapshot(); }});
}
