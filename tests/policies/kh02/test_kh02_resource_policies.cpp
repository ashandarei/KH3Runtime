#include "kh3ap/features/kh02/cutscenes/kh02_subtitle_timeline.h"
#include "kh3ap/features/kh02/movies/kh02_movie_frame_queue.h"
#include "kh3ap/features/kh02/movies/kh02_movie_request_plan.h"
#include "kh3ap/features/kh02/ui/kh02_information_operand.h"
#include <array>
#include <cassert>
#include <limits>

int main()
{
    using namespace kh3ap;
    const std::array cues{Kh02SubtitleCue{1.f, 2.f, L"first"}, Kh02SubtitleCue{2.f, 2.f, L"second"}};
    assert(kh02_subtitle_at(cues, 0.f) == -1);
    assert(kh02_subtitle_at(cues, 1.f) == 0);
    assert(kh02_subtitle_at(cues, 2.f) == 1);
    assert(kh02_subtitle_at(cues, 3.f) == 1);
    assert(kh02_subtitle_at(cues, 4.f) == 1);
    assert(kh02_subtitle_at(cues, 5.f) == -1);
    assert(kh02_subtitle_at(cues, std::numeric_limits<float>::quiet_NaN()) == -1);
    assert(kh02_subtitle_culture_matches(L"ja-JP", L"ja"));
    assert(kh02_subtitle_culture_matches(L"ja", L"ja"));
    assert(!kh02_subtitle_culture_matches(L"ja", L"ja-JP"));
    assert(!kh02_subtitle_culture_matches(L"jaJP", L"ja"));
    assert(!kh02_subtitle_culture_matches(L"en-US", L"ja"));
    assert(!kh02_subtitle_culture_matches(L"en", L""));

    Kh02MovieFrameQueue frames;
    assert(!frames.free_slot());
    frames.prepare(4);
    assert(!frames.due(0, -1));
    assert(!frames.latest(-1));
    for (int number = 1; number <= 8; ++number)
    {
        auto* slot = frames.free_slot();
        assert(slot && slot->pixels.size() == 4);
        slot->pixels[0] = static_cast<uint8_t>(number);
        frames.accept(*slot, number);
    }
    assert(!frames.free_slot());
    assert(frames.received() == 8);
    assert(!frames.due(0, -1));
    assert(frames.due(5, -1)->number == 5);
    frames.retire(5);
    assert(frames.free_slot());
    assert(!frames.due(5, 5));
    assert(frames.latest(5)->pixels[0] == 8);
    frames.retire(8);
    assert(!frames.latest(8));
    frames.clear();
    assert(!frames.free_slot());
    assert(frames.received() == 8);
    frames.reset_received();
    assert(frames.received() == -1);

    for (const auto& row : kh02movie_metadata::stories)
    {
        const auto plan = kh02_movie_request_plan(row.logical);
        assert(plan.story == &row);
        assert(plan.file == row.file);
    }
    const auto gear = kh02_movie_request_plan(L"Rebellis02\\gear_b.usm");
    assert(kh02_movie_request_plan(L"Rebellis02/ja/cs_dw029_mv.usm", "ja").file == "ja/cs_dw029_mv.usm");
    assert(kh02_movie_request_plan(L"Rebellis02/movie/dw026_movie.usm", "ja").file == "movie/dw026_movie.usm");
    assert(!gear.story);
    assert(gear.logical == L"Rebellis02/gear_b.usm");
    assert(gear.file == "gear_b.usm");
    for (const auto* path : {L"Rebellis02/gear_e.usm", L"Rebellis02/../gear_a.usm", L"other/gear_a.usm"})
    {
        bool rejected = false;
        try
        {
            kh02_movie_request_plan(path);
        }
        catch (const std::runtime_error& error)
        {
            rejected = std::string_view(error.what()) == "movie_bridge_movie_not_audited";
        }
        assert(rejected);
    }

    std::array<uint8_t, kh02_information_operand_bytes> operand{};
    operand[0] = kh02compat::opcode::local_variable;
    const uintptr_t property = 0x12345678;
    std::memcpy(operand.data() + 1, &property, sizeof(property));
    operand[kh02_information_boolean_offset] = kh02compat::opcode::true_constant;
    operand[kh02_information_end_offset] = kh02compat::opcode::end_parameters;
    const auto decoded = kh02_information_operand(operand);
    assert(decoded && decoded->local_property == property && decoded->auto_close);
    operand[kh02_information_boolean_offset] = kh02compat::opcode::false_constant;
    assert(!kh02_information_operand(operand)->auto_close);
    for (std::size_t size = 0; size < operand.size(); ++size)
    {
        assert(!kh02_information_operand(std::span(operand).first(size)));
    }
    for (const auto offset : {std::size_t{0}, kh02_information_boolean_offset, kh02_information_end_offset})
    {
        const auto previous = operand[offset];
        operand[offset] = 0xff;
        assert(!kh02_information_operand(operand));
        operand[offset] = previous;
    }
}
