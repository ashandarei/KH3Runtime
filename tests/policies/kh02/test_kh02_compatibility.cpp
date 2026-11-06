#include "kh3ap/features/kh02/compatibility/kh02_compatibility_reader.h"
#include "kh3ap/features/kh02/movies/kh02_optional_audio.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace kh3ap::kh02compat;

template <class Callback> void rejects(Callback callback)
{
    bool rejected = false;
    try
    {
        callback();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    assert(rejected);
}

template <class Value> auto scalar(uint8_t operation, Value value) -> std::array<uint8_t, 1 + sizeof(Value)>
{
    std::array<uint8_t, 1 + sizeof(Value)> bytes{};
    bytes[0] = operation;
    std::memcpy(bytes.data() + 1, &value, sizeof(value));
    return bytes;
}

int main()
{
    // Every stable registration belongs to exactly one executable domain.
    std::array<size_t, 6> counts{};
    for (size_t index = 0; index < adapters.size(); ++index)
    {
        assert(valid_adapter(static_cast<int>(index)));
        const auto& descriptor = adapters[index];
        assert(static_cast<size_t>(descriptor.adapter) == index);
        assert(!descriptor.name.empty());
        ++counts[static_cast<size_t>(domain_of(descriptor.adapter))];
    }
    assert((counts == std::array<size_t, 6>{4, 4, 8, 12, 4, 8}));
    assert(!valid_adapter(-1));
    assert(!valid_adapter(static_cast<int>(adapters.size())));
    rejects([] { domain_of(static_cast<Adapter>(255)); });
    assert(has_latent(Adapter::ApplyMapSet));
    assert(has_latent(Adapter::WaitVideoReady));
    assert(has_latent(Adapter::WaitVideoEnd));
    assert(has_latent(Adapter::SkipHelp));
    assert(has_latent(Adapter::SkipNotice));
    assert(!has_latent(Adapter::PrepareVideo));
    assert(!has_latent(Adapter::MirrorJump));

    auto integer = scalar(opcode::integer_constant, int32_t{-741});
    for (size_t length = 0; length < integer.size(); ++length)
    {
        auto* cursor = integer.data();
        BytecodeReader reader(cursor, cursor + length);
        rejects([&] { reader.integer("integer"); });
        assert(cursor == integer.data());
    }
    auto* cursor = integer.data();
    BytecodeReader reader(cursor, cursor + integer.size());
    assert(reader.integer("integer") == -741);
    assert(cursor == integer.data() + integer.size());
    rejects([&] { reader.expect(opcode::end_parameters, "end"); });

    auto floating = scalar(opcode::float_constant, 0.75f);
    cursor = floating.data();
    BytecodeReader wrong_type(cursor, cursor + floating.size());
    rejects([&] { wrong_type.integer("integer"); });
    assert(cursor == floating.data());
    assert(wrong_type.number("float") == 0.75f);

    const std::string movie = "Rebellis02/movie/dw026_movie.usm";
    std::vector<uint8_t> bytes{opcode::string_constant};
    bytes.insert(bytes.end(), movie.begin(), movie.end());
    bytes.push_back(0);
    bytes.push_back(opcode::end_parameters);
    cursor = bytes.data();
    BytecodeReader strings(cursor, cursor + bytes.size());
    assert(strings.literal_string() == std::wstring(movie.begin(), movie.end()));
    strings.expect(opcode::end_parameters, "end");
    assert(cursor == bytes.data() + bytes.size());

    for (size_t length = 1; length < movie.size() + 2; ++length)
    {
        cursor = bytes.data();
        BytecodeReader truncated(cursor, cursor + length);
        rejects([&] { truncated.literal_string(); });
    }
    bytes = {opcode::string_constant, 0xff, 0};
    cursor = bytes.data();
    BytecodeReader invalid_ascii(cursor, cursor + bytes.size());
    rejects([&] { invalid_ascii.literal_string(); });
    bytes.assign(BytecodeReader::literal_limit + 3, 'a');
    bytes.front() = opcode::string_constant;
    bytes.back() = 0;
    cursor = bytes.data();
    BytecodeReader oversized(cursor, cursor + bytes.size());
    rejects([&] { oversized.literal_string(); });

    bytes = {opcode::end_parameters, opcode::return_expression, opcode::nothing, opcode::end_script};
    cursor = bytes.data();
    BytecodeReader boundary(cursor, cursor + bytes.size());
    boundary.expect(opcode::end_parameters, "end");
    assert(*cursor == opcode::return_expression);
    rejects([&] { boundary.expect(opcode::end_parameters, "end"); });
    assert(*cursor == opcode::return_expression);

    // A device-only failure drops optional audio, but payload/ownership errors
    // cannot cross the same boundary as a warning merely because they mention audio.
    std::string warning;
    bool audio_active = true;
    kh3ap::try_kh02_audio_output([] { throw kh3ap::Kh02AudioOutputError("device_lost"); },
                                 [&](const char* reason)
                                 {
                                     warning = reason;
                                     audio_active = false;
                                 });
    assert(!audio_active);
    assert(warning == "device_lost");
    warning.clear();
    rejects(
        [&]
        {
            kh3ap::try_kh02_audio_output([] { throw std::runtime_error("movie_shared_audio_packet"); },
                                         [&](const char* reason) { warning = reason; });
        });
    assert(warning.empty());

    // Decode all operands before optional native music execution. A malformed
    // operand never runs the operation or its warning handler; a native music
    // execution failure after valid decoding still permits the graph to continue.
    bool native_called = false;
    bool continued = false;
    const auto invoke_music = [&](size_t available)
    {
        auto* music_cursor = floating.data();
        BytecodeReader music_operands(music_cursor, music_cursor + available);
        music_operands.number("music_operand");
        kh3ap::try_kh02_music_execution(
            [&]
            {
                native_called = true;
                throw std::runtime_error("music_slot_unavailable");
            },
            [&](const char* reason) { warning = reason; });
        continued = true;
    };
    rejects([&] { invoke_music(floating.size() - 1); });
    assert(!native_called && !continued && warning.empty());
    invoke_music(floating.size());
    assert(native_called && continued && warning == "music_slot_unavailable");

    // Switching off a failed audio clock must preserve the latest queued video,
    // especially the real final frame already removed from the shared ring.
    struct QueuedVideoFrame
    {
        int number;
    };

    std::array<QueuedVideoFrame, 4> queued{{{134}, {-1}, {131}, {133}}};
    auto* final_frame = kh3ap::latest_kh02_video_frame(std::span(queued), 130);
    assert(final_frame == &queued.front() && final_frame->number == 134);
    assert(!kh3ap::latest_kh02_video_frame(std::span(queued), 134));
    assert(!kh3ap::latest_kh02_video_frame(std::span<QueuedVideoFrame>{}, 130));
}
