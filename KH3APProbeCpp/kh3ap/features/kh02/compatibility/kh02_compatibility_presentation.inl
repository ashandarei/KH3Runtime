// Private Kh02Story compatibility invocation; included inside CompatibilityInvocation.
static constexpr int skipped_notice_window_handle = -2;

void parse_presentation()
{
    constexpr int notice_enum_operand_count = 2;
    // Navigation event aliases intentionally accept no arguments and perform no
    // source navigation initialization; the host owns its KH3 navigation data.
    // The common dispatcher still validates EndFunctionParms for these calls.
    if (adapter == Adapter::OpenCounter)
    {
        integer_argument = integer();
        maximum_argument = integer();
    }
    if (adapter == Adapter::SetCounter)
    {
        integer_argument = integer();
    }
    if (adapter == Adapter::CloseWindow)
    {
        integer_argument = local_integer();
    }
    if (adapter == Adapter::BlockMagic)
    {
        if (cursor >= script_end ||
            (*cursor != kh02compat::opcode::true_constant && *cursor != kh02compat::opcode::false_constant))
        {
            throw std::runtime_error("kh02_magic_bool_operand");
        }
        ++cursor;
    }
    if (adapter == Adapter::SkipHelp)
    {
        const auto execution = latent_info.execution.ToString();
        const auto caller_path = frame->Object->GetPathName();
        if (execution != L"ExecuteUbergraph_dw_02_gameflow" ||
            !caller_path.starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_gameflow."))
        {
            throw std::runtime_error("kh02_help_caller");
        }
        if (script_end - cursor < kh02compat::vm::byte_operand_bytes || *cursor++ != kh02compat::opcode::byte_constant)
        {
            throw std::runtime_error("kh02_help_byte_operand");
        }
        integer_argument = *cursor++;
    }
    if (adapter == Adapter::SkipNotice)
    {
        const auto execution = latent_info.execution.ToString();
        const auto caller_path = frame->Object->GetPathName();
        if (execution != L"ExecuteUbergraph_dw_02_gameflow" ||
            !caller_path.starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_gameflow.") ||
            (latent_info.linkage != 1304 && latent_info.linkage != 6439))
        {
            throw std::runtime_error("kh02_notice_caller");
        }
        for (int operand_index = 0; operand_index < notice_enum_operand_count; ++operand_index)
        {
            if (script_end - cursor < kh02compat::vm::byte_operand_bytes ||
                *cursor++ != kh02compat::opcode::byte_constant || *cursor++ != 0)
            {
                throw std::runtime_error("kh02_notice_enum_operand");
            }
        }
        for (const auto* expected_schema_path :
             {L"/Script/TresGame.TresLocText", L"/Script/TresGame.TresUIWindowSetting"})
        {
            if (script_end - cursor < kh02compat::vm::reference_operand_bytes ||
                *cursor++ != kh02compat::opcode::local_variable || !frame->Locals)
            {
                throw std::runtime_error("kh02_notice_local_operand");
            }
            FProperty* local_property;
            std::memcpy(&local_property, cursor, sizeof(local_property));
            cursor += kh02compat::vm::reference_bytes;
            auto* property_object = reinterpret_cast<UObject*>(local_property);
            if (!is_uobject_valid_fast(property_object) || property_object->GetOuterPrivate() != frame->Node ||
                !local_property->IsA<FStructProperty>() ||
                !is_uobject_valid_fast(static_cast<FStructProperty*>(local_property)->GetStruct()) ||
                static_cast<FStructProperty*>(local_property)->GetStruct()->GetPathName() != expected_schema_path ||
                local_property->GetOffset_Internal() < 0 ||
                local_property->GetOffset_Internal() + local_property->GetElementSize() >
                    frame->Node->GetPropertiesSize())
            {
                throw std::runtime_error("kh02_notice_local_layout");
            }
        }
    }
}

void execute_presentation()
{
    if (adapter == Adapter::OpenCounter)
    {
        story->counter_requested = true;
        story->counter_value = integer_argument;
        story->counter_maximum = maximum_argument;
        story->hud.counter(integer_argument, maximum_argument, true);
    }
    if (adapter == Adapter::SetCounter)
    {
        story->counter_value = integer_argument;
        story->hud.counter(integer_argument, story->counter_maximum, true);
    }
    if (adapter == Adapter::CloseCounter)
    {
        story->counter_requested = false;
        story->hud.counter(story->counter_value, story->counter_maximum, false);
    }
    if (adapter == Adapter::CloseWindow)
    {
        if (integer_argument == skipped_notice_window_handle &&
            frame->Node->GetPathName().starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_gameflow."))
        {
            return;
        }
        auto* player_context = story->gameplay.pawn.Get();
        if (integer_argument < 0 || !is_uobject_valid_fast(player_context) ||
            static_cast<AActor*>(player_context)->GetWorld() != story->gameplay.world.Get())
        {
            throw std::runtime_error("kh02_compat_close_context");
        }
        ReflectedCall close(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                            L"TresUI_CloseWindow");
        close.integer(L"WindowHandle", integer_argument);
        close.object(L"WorldContextObject", player_context);
        auto* latent = close.param(L"LatentInfo");
        constexpr int close_latent_offset = 16;
        constexpr size_t close_parameters_bytes = close_latent_offset + kh02compat::vm::native_latent_bytes;
        constexpr int32_t close_latent_uuid = 0x4b483032;
        if (!latent || latent->GetClass().GetName() != L"StructProperty" ||
            latent->GetOffset_Internal() != close_latent_offset ||
            latent->GetElementSize() != kh02compat::vm::native_latent_bytes ||
            close.function->GetParmsSize() != close_parameters_bytes)
        {
            throw std::runtime_error("kh02_compat_close_latent_layout");
        }
        const Latent close_latent_info{-1, close_latent_uuid, FName{}, player_context};
        std::memcpy(latent->ContainerPtrToValuePtr<void>(close.parameters.data()), &close_latent_info,
                    sizeof(close_latent_info));
        close.run();
    }
    if (adapter == Adapter::BlockMagic)
    {
        ++story->magic_changes_blocked;
    }
    if (adapter == Adapter::SkipHelp || adapter == Adapter::SkipNotice)
    {
        story->help_callbacks.enqueue(latent_info.callback, story->gameplay.world.Get(), latent_info.execution,
                                      latent_info.linkage, story->map_session.generation());
        if (adapter == Adapter::SkipHelp)
        {
            ++story->help_windows_skipped;
        }
        if (adapter == Adapter::SkipNotice)
        {
            if (!return_value)
            {
                throw std::runtime_error("kh02_notice_return_missing");
            }
            *static_cast<int32_t*>(return_value) = skipped_notice_window_handle;
            ++story->notices_skipped;
        }
    }
}
