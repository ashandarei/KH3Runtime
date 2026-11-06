// Private Kh02Story compatibility invocation; included inside CompatibilityInvocation.
void parse_audio()
{
    constexpr float maximum_volume_fade_seconds = 60.f;
    if (adapter == Adapter::PlayBgm)
    {
        sound = object();
        music_seek_seconds = number();
        music_fade_seconds = number();
        if (!is_uobject_valid_fast(sound) || !sound->GetPathName().starts_with(L"/Game/Zound/BGM/"))
        {
            throw std::runtime_error("kh02_source_music_asset");
        }
    }
    if (adapter == Adapter::IsBgmSuspended)
    {
        sound = object();
        if (!is_uobject_valid_fast(sound) || !sound->GetPathName().starts_with(L"/Game/Zound/BGM/"))
        {
            throw std::runtime_error("kh02_suspended_bgm_asset");
        }
    }
    if (adapter == Adapter::FadeOutBgm)
    {
        music_fade_seconds = number();
        sound = object();
        if (sound && (!is_uobject_valid_fast(sound) || !sound->GetPathName().starts_with(L"/Game/Zound/BGM/")))
        {
            throw std::runtime_error("kh02_source_music_asset");
        }
    }
    if (adapter == Adapter::SetBgmVolume)
    {
        if (!frame->Node->GetPathName().starts_with(L"/Game/Zevels/dw/dw_02/dw_02."))
        {
            throw std::runtime_error("kh02_bgm_volume_caller");
        }
        music_volume = variable_number();
        music_fade_seconds = number();
        if (!std::isfinite(music_volume) || music_volume < 0 || music_volume > 1 ||
            !std::isfinite(music_fade_seconds) || music_fade_seconds < 0 ||
            music_fade_seconds > maximum_volume_fade_seconds)
        {
            throw std::runtime_error("kh02_bgm_volume_range");
        }
    }
}

void execute_audio_native()
{
    if (adapter == Adapter::IsBgmSuspended)
    {
        const bool suspended = story->music.is_suspended(sound);
        if (return_value)
        {
            *static_cast<uint8_t*>(return_value) = suspended;
        }
    }
    if (adapter == Adapter::SetBgmVolume)
    {
        story->music.set_volume(music_volume, music_fade_seconds);
    }
    if (adapter == Adapter::PlayBgm)
    {
        story->play_source_music(frame->Object, sound, music_seek_seconds, music_fade_seconds);
    }
    if (adapter == Adapter::FadeOutBgm)
    {
        story->music.stop_source(frame->Object, sound, music_fade_seconds);
    }
}

void execute_audio()
{
    // Complete operand decoding and these receiver/ownership checks before the
    // optional execution boundary. A stale caller must never operate KH3 music.
    auto* caller_world = kh02_actor_world(frame->Object);
    if (!is_uobject_valid_fast(native_owner) ||
        native_owner->GetClassPrivate()->GetPathName() != L"/Script/TresGame.TresSoundStatics")
    {
        throw std::runtime_error("kh02_audio_receiver");
    }
    if (!is_uobject_valid_fast(caller_world) || caller_world != kh02_current_story_world())
    {
        throw std::runtime_error("kh02_audio_caller_world");
    }
    if (adapter == Adapter::IsBgmSuspended && return_value)
    {
        // An unavailable optional query must not leave a stale VM return byte.
        *static_cast<uint8_t*>(return_value) = false;
    }
    try_kh02_music_execution([&] { execute_audio_native(); }, [&](const char* warning)
                             { story->compat_warning = "adapter" + std::to_string(adapter_id()) + ":" + warning; });
}
