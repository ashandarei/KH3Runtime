// Private invocation owns only one synchronous VM call. It never retains frame/property pointers.
struct CompatibilityInvocation
{
    using Adapter = kh02compat::Adapter;
    using Domain = kh02compat::Domain;
    Impl* story;
    UObject* native_owner;
    FFrame_50_AndBelow* frame;
    void* return_value;
    Adapter adapter;
    uint8_t* cursor;
    uint8_t* script_end;
    kh02compat::EngineOperands operands;
    UObject* sound = nullptr;
    float music_volume = 0;
    float music_seek_seconds = 0;
    float music_fade_seconds = 0;
    float look_blend = 0;
    int integer_argument = 0;
    int maximum_argument = 0;
    UObject* target = nullptr;
    UObject* controller = nullptr;
    std::wstring movie;
    std::wstring mapset_command;
    bool pause_when_prepared = false;
    bool forward_aqua_look = false;
    UObject* mapset_asset = nullptr;

    using Latent = kh02compat::EngineOperands::Latent;
    Latent latent_info{};
    static constexpr int challenge_count = kh02_challenge_id::count;
    static constexpr size_t movie_history_limit = 64;

    CompatibilityInvocation(Impl* story, UObject* owner, FFrame_50_AndBelow* frame, void* result, Adapter adapter_kind)
        : story(story), native_owner(owner), frame(frame), return_value(result), adapter(adapter_kind),
          cursor(frame->Code), script_end(frame->Node->GetScript().GetData() + frame->Node->GetScript().Num()),
          operands(*frame, cursor, script_end)
    {
    }

    auto adapter_id() const -> int
    {
        return static_cast<int>(adapter);
    }

    auto integer() -> int32_t
    {
        return operands.integer();
    }

    auto local_integer() -> int32_t
    {
        return operands.local_integer();
    }

    auto number() -> float
    {
        return operands.number();
    }

    auto variable_number() -> float
    {
        return operands.variable_number();
    }

    auto literal_string() -> std::wstring
    {
        return operands.literal_string();
    }

    auto object() -> UObject*
    {
        return operands.object();
    }

    auto local_object() -> UObject*
    {
        return operands.local_object();
    }

    void parse_latent()
    {
        latent_info = operands.latent_self();
    }

#include "kh02_compatibility_camera.inl"
#include "kh02_compatibility_audio.inl"
#include "kh02_compatibility_movies.inl"
#include "kh02_compatibility_challenges.inl"
#include "kh02_compatibility_travel.inl"
#include "kh02_compatibility_presentation.inl"

    void invoke()
    {
        if (kh02compat::has_latent(adapter))
        {
            parse_latent();
        }
        switch (kh02compat::domain_of(adapter))
        {
        case Domain::camera:
            parse_camera();
            break;
        case Domain::audio:
            parse_audio();
            break;
        case Domain::movies:
            parse_movies();
            break;
        case Domain::challenges:
            parse_challenges();
            break;
        case Domain::travel:
            parse_travel();
            break;
        case Domain::presentation:
            parse_presentation();
            break;
        }
        operands.end_parameters();
        frame->Code = cursor;
        ++story->compat_calls;
        switch (kh02compat::domain_of(adapter))
        {
        case Domain::camera:
            execute_camera();
            break;
        case Domain::audio:
            execute_audio();
            break;
        case Domain::movies:
            execute_movies();
            break;
        case Domain::challenges:
            execute_challenges();
            break;
        case Domain::travel:
            execute_travel();
            break;
        case Domain::presentation:
            execute_presentation();
            break;
        }
    }
};
