// Private Kh02Story compatibility invocation; included inside CompatibilityInvocation.
void parse_movies()
{
    constexpr int maximum_movie_path_characters = 512;
    constexpr int native_string_property_bytes = sizeof(FString);
    if (adapter == Adapter::PrepareVideo || adapter == Adapter::PlayVideo)
    {
        if (cursor < script_end && *cursor == kh02compat::opcode::local_variable)
        {
            ++cursor;
            if (script_end - cursor < kh02compat::vm::reference_bytes || !frame->Locals)
            {
                throw std::runtime_error("kh02_movie_local_operand");
            }
            FProperty* path_property;
            std::memcpy(&path_property, cursor, sizeof(path_property));
            cursor += kh02compat::vm::reference_bytes;
            if (!is_uobject_valid_fast(reinterpret_cast<UObject*>(path_property)) ||
                reinterpret_cast<UObject*>(path_property)->GetOuterPrivate() != frame->Node ||
                path_property->GetClass().GetName() != L"StrProperty" ||
                path_property->GetElementSize() != native_string_property_bytes ||
                path_property->GetOffset_Internal() < 0 ||
                path_property->GetOffset_Internal() + native_string_property_bytes > frame->Node->GetPropertiesSize())
            {
                throw std::runtime_error("kh02_movie_local_layout");
            }

            struct MoviePathBuffer
            {
                const wchar_t* data;
                int32_t count;
                int32_t capacity;
            };

            static_assert(sizeof(MoviePathBuffer) == native_string_property_bytes);
            const auto& path_buffer = *path_property->ContainerPtrToValuePtr<MoviePathBuffer>(frame->Locals);
            if (!path_buffer.data || path_buffer.count < 1 || path_buffer.count > maximum_movie_path_characters ||
                path_buffer.capacity < path_buffer.count || path_buffer.data[path_buffer.count - 1])
            {
                throw std::runtime_error("kh02_movie_local_text");
            }
            movie.assign(path_buffer.data, path_buffer.count - 1);
        }
        else
        {
            movie = literal_string();
        }
        std::replace(movie.begin(), movie.end(), L'\\', L'/');
        bool audited_movie = movie == L"Rebellis02/gear_a.usm" || movie == L"Rebellis02/gear_b.usm" ||
                             movie == L"Rebellis02/gear_c.usm" || movie == L"Rebellis02/gear_d.usm";
        for (const auto& row : kh02movie_metadata::stories)
        {
            audited_movie |= movie == row.logical;
        }
        if (!audited_movie)
        {
            throw std::runtime_error("kh02_compat_movie_not_audited");
        }
        if (adapter == Adapter::PrepareVideo)
        {
            if (cursor >= script_end ||
                (*cursor != kh02compat::opcode::true_constant && *cursor != kh02compat::opcode::false_constant))
            {
                throw std::runtime_error("kh02_compat_movie_pause");
            }
            pause_when_prepared = *cursor++ == kh02compat::opcode::true_constant;
        }
        else
        {
            pause_when_prepared = true;
        }
    }
    if (adapter == Adapter::MatineeForMovie)
    {
        target = local_object();
        if (cursor >= script_end ||
            (*cursor != kh02compat::opcode::true_constant && *cursor != kh02compat::opcode::false_constant))
        {
            throw std::runtime_error("kh02_matinee_movie_mode");
        }
        integer_argument = *cursor++ == kh02compat::opcode::true_constant;
    }
    if (adapter == Adapter::WaitVideoReady || adapter == Adapter::WaitVideoEnd)
    {
        const auto execution_name = latent_info.execution.ToString();
        const auto caller_path = frame->Object->GetPathName();
        const bool gear_camera_call = execution_name == L"ExecuteUbergraph_dw_02_camera_ms030" &&
                                      caller_path.starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_camera_ms030.");
        const bool story_cutscene_call = caller_path.starts_with(L"/Game/Zutscene/Localization/en/") &&
                                         execution_name.starts_with(L"ExecuteUbergraph_cutscene_");
        if (!gear_camera_call && !story_cutscene_call)
        {
            throw std::runtime_error("kh02_compat_movie_latent_identity");
        }
    }
}

void execute_movies()
{
    if (adapter == Adapter::WaitVideoEnd)
    {
        story->movie_bridge.wait_finish(latent_info.callback, latent_info.linkage, latent_info.execution.ToString());
    }
    if (adapter == Adapter::IsVideoPlaying)
    {
        if (return_value)
        {
            *static_cast<uint8_t*>(return_value) = story->movie_bridge.playing();
        }
    }
    if (adapter == Adapter::StopVideo)
    {
        story->movie_bridge.stop();
    }
    if (adapter == Adapter::MatineeForMovie)
    {
        if (!is_uobject_valid_fast(target) || kh02_actor_world(target) != story->gameplay.world.Get() ||
            !frame->Node->GetPathName().starts_with(L"/Game/Zutscene/Localization/en/"))
        {
            throw std::runtime_error("kh02_matinee_movie_owner");
        }
        if (const auto* logical_movie = full_movie_for_scene(target))
        {
            story->cutscenes.select_full_movie(integer_argument ? target : nullptr,
                                               integer_argument ? logical_movie : "");
        }
    }
    if ((adapter == Adapter::PrepareVideo || adapter == Adapter::ResumeVideo || adapter == Adapter::WaitVideoReady) ||
        adapter == Adapter::PlayVideo)
    {
        auto* player_context = story->gameplay.pawn.Get();
        if (!is_uobject_valid_fast(player_context) ||
            static_cast<AActor*>(player_context)->GetWorld() != story->gameplay.world.Get())
        {
            throw std::runtime_error("kh02_compat_movie_context");
        }
        if (adapter == Adapter::PrepareVideo || adapter == Adapter::PlayVideo)
        {
            if (!pause_when_prepared)
            {
                throw std::runtime_error("kh02_compat_movie_prepare_mode");
            }
            story->movie_bridge.prepare(player_context, frame->Object, movie, true);
            if (adapter == Adapter::PlayVideo)
            {
                story->movie_bridge.resume();
            }
        }
        if (adapter == Adapter::ResumeVideo)
        {
            story->movie_bridge.resume();
        }
        if (adapter == Adapter::WaitVideoReady)
        {
            story->movie_bridge.wait(latent_info.callback, latent_info.linkage, latent_info.execution.ToString());
        }
        if (story->movie_calls.size() < movie_history_limit)
        {
            story->movie_calls.push_back({{"adapter", adapter_id()},
                                          {"movie", to_utf8(movie)},
                                          {"native_movie", movie.empty() ? "" : to_utf8(L"main/" + movie)},
                                          {"slot", 0},
                                          {"pause_when_prepared", pause_when_prepared},
                                          {"source", path(frame->Object)}});
        }
    }
}
