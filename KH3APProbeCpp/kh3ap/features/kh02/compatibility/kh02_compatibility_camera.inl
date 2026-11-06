// Private Kh02Story compatibility invocation; included inside CompatibilityInvocation.
void parse_camera()
{
    if (adapter == Adapter::RooftopState)
    {
        auto* receiver_world = kh02_actor_world(native_owner);
        const auto receiver_class_name =
            is_uobject_valid_fast(native_owner) ? native_owner->GetClassPrivate()->GetName() : L"";
        if (!is_uobject_valid_fast(receiver_world) || receiver_world != story->gameplay.world.Get() ||
            receiver_world->GetPathName() != L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown" ||
            kh02_actor_world(frame->Object) != receiver_world ||
            !frame->Node->GetPathName().starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_camera_ms200.") ||
            native_owner != story->gameplay.pawn.Get() ||
            (receiver_class_name != L"p_ex001_Pawn_C" && receiver_class_name != L"p_ex002_Pawn_C"))
        {
            throw std::runtime_error("kh02_rooftop_state_caller");
        }
    }
    if (adapter == Adapter::LookStart || adapter == Adapter::LookEnd)
    {
        const auto caller_path = frame->Node->GetPathName();
        auto* receiver_world = kh02_actor_world(native_owner);
        const bool forest = is_uobject_valid_fast(receiver_world) && receiver_world == story->mirror.world.Get() &&
                            receiver_world->GetPathName() == L"/Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest" &&
                            (caller_path.starts_with(L"/Game/Zevels/dw/dw_08/umap/dw_08_camera_ms0110e.") ||
                             caller_path.starts_with(L"/Game/Zevels/dw/dw_08/umap/dw_08_camera_ms0111."));
        const bool stars =
            is_uobject_valid_fast(receiver_world) && receiver_world == story->gameplay.world.Get() &&
            receiver_world->GetPathName() == L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown" &&
            (caller_path.starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_camera_ms201.") ||
             caller_path.starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_camera_ms200."));
        const bool depths = is_uobject_valid_fast(receiver_world) && receiver_world == story->mirror.world.Get() &&
                            receiver_world->GetPathName() == L"/Game/Maps/APKH02Mirrors/APKH02Depths.APKH02Depths" &&
                            caller_path.starts_with(L"/Game/Zevels/dw/dw_09/umap/dw_09_camera_ms150e.");
        const bool mirror_challenge =
            is_uobject_valid_fast(receiver_world) && receiver_world == story->mirror.world.Get() &&
            receiver_world->GetPathName() == L"/Game/Maps/APKH02Mirrors/APKH02MirrorChallenge.APKH02MirrorChallenge" &&
            caller_path.starts_with(L"/Game/Zevels/dw/dw_11/umap/dw_11_camera_ms240.");
        const auto receiver_class_name =
            is_uobject_valid_fast(native_owner) ? native_owner->GetClassPrivate()->GetName() : L"";
        const bool audited_player_look =
            (forest || depths || mirror_challenge) && kh02_actor_world(frame->Object) == receiver_world &&
            (receiver_class_name == L"p_ex001_Pawn_C" || receiver_class_name == L"p_ex002_Pawn_C");
        if (!(stars && (receiver_class_name == L"p_ex001_Pawn_C" || receiver_class_name == L"p_ex002_Pawn_C") ||
              audited_player_look))
        {
            throw std::runtime_error("kh02_player_look_caller");
        }
        // These two source cameras target the local playable hero. Sora
        // has no KH0.2 head-IK function. Preserve Aqua's original call
        // when her receiver exposes it; the authored camera and graph
        // continue to own all transforms, timing, and progression.
        forward_aqua_look = audited_player_look && receiver_class_name == L"p_ex002_Pawn_C";
        look_blend = number();
        if (!std::isfinite(look_blend) || look_blend < 0 || look_blend > 1)
        {
            throw std::runtime_error("kh02_forest_look_blend");
        }
        if (adapter == Adapter::LookStart)
        {
            target = local_object();
            const auto bone_name = operands.name("kh02_forest_look_bone");
            if (bone_name.ToString() != L"None")
            {
                throw std::runtime_error("kh02_forest_look_bone_name");
            }
        }
    }
    if (adapter == Adapter::ConfigureCamera)
    {
        target = local_object();
        controller = local_object();
    }
}

void execute_camera()
{
    if (adapter == Adapter::RooftopState)
    {
        ReflectedCall query(native_owner, L"GetStateID");
        if (query.function->GetParmsSize() != sizeof(uint8_t) || !return_value)
        {
            throw std::runtime_error("kh02_rooftop_state_signature");
        }
        query.run();
        const auto native_state = read_function_int_param(query.function, query.parameters.data(), L"ReturnValue");
        if (!native_state || *native_state < 0 || *native_state > UINT8_MAX)
        {
            throw std::runtime_error("kh02_rooftop_state_result");
        }
        auto* state_component = try_read_reflected_object_property_value(native_owner, L"MyStateComp");
        auto* current_state = is_uobject_valid_fast(state_component)
                                  ? try_read_reflected_object_property_value(state_component, L"m_pCurrentState")
                                  : nullptr;
        // The source rooftop walk waits for cinematic state 57. KH3 uses a
        // different enum number. Match the native state class for both heroes
        // without changing either hero's actual state.
        const bool cinematic =
            is_uobject_valid_fast(current_state) &&
            current_state->GetClassPrivate()->GetPathName() == L"/Script/TresGame.TresCharState_Cinematic";
        *static_cast<uint8_t*>(return_value) = cinematic ? 57 : static_cast<uint8_t>(*native_state);
    }
    if (adapter == Adapter::LookStart || adapter == Adapter::LookEnd)
    {
        if (forward_aqua_look)
        {
            if (adapter == Adapter::LookStart && target && kh02_actor_world(target) != kh02_actor_world(native_owner))
            {
                throw std::runtime_error("kh02_aqua_look_target_world");
            }
            try
            {
                constexpr uint16_t look_start_parameters_bytes = 24;
                constexpr uint16_t look_end_parameters_bytes = sizeof(float);
                const auto* method = adapter == Adapter::LookStart ? L"StartIKinemaLookAt" : L"EndIKinemaLookAt";
                auto* function = native_owner->GetFunctionByNameInChain(method);
                if (!is_uobject_valid_fast(function))
                {
                    throw std::runtime_error("kh02_aqua_look_function_missing");
                }
                ReflectedCall look(native_owner, method);
                const auto expected_parameter_bytes =
                    adapter == Adapter::LookStart ? look_start_parameters_bytes : look_end_parameters_bytes;
                if (look.function->GetParmsSize() != expected_parameter_bytes)
                {
                    throw std::runtime_error("kh02_aqua_look_signature");
                }
                if (adapter == Adapter::LookStart)
                {
                    look.number(L"BlendIn", look_blend);
                    look.object(L"TargetActor", target);
                    look.name(L"SockeName", L"None");
                }
                else
                {
                    look.number(L"BlendOut", look_blend);
                }
                look.run();
            }
            catch (const std::exception& error)
            {
                // A cosmetic method unavailable on KH3's Aqua must
                // not truncate the source graph after a valid call.
                story->compat_warning = "adapter" + std::to_string(adapter_id()) + ":" + error.what();
            }
        }
    }
    if (adapter == Adapter::ConfigureCamera)
    {
        // Source camera completion can run during a character handoff,
        // before the story tick replaces its cached pawn.
        auto* player_context = kh02_story_player(story->gameplay.pawn.Get());
        if (!is_uobject_valid_fast(player_context) || !is_uobject_valid_fast(native_owner) ||
            kh02_actor_world(player_context) != story->gameplay.world.Get() ||
            kh02_actor_world(frame->Object) != story->gameplay.world.Get() ||
            kh02_actor_world(native_owner) != story->gameplay.world.Get())
        {
            throw std::runtime_error("kh02_compat_camera_context");
        }
        ReflectedCall setting(native_owner, L"BP_SettingTresCamera");
        constexpr size_t setting_parameters_bytes = 3 * sizeof(UObject*);
        if (setting.function->GetParmsSize() != setting_parameters_bytes)
        {
            throw std::runtime_error("kh02_compat_camera_signature");
        }
        setting.object(L"TargetActor", target);
        setting.object(L"PlayerController", controller);
        setting.object(L"WorldContextObject", player_context);
        setting.run();
    }
}
