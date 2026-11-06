#include "shared/kh02/kh02_movie_shared.h"
#include "kh02/story_movie_metadata.h"
#include <array>
#include <cassert>
#include <stdexcept>
#include <thread>
#include <string_view>

int main()
{
    using namespace kh02movie;
    int localized_soundtracks = 0;
    for (const auto& movie : kh02movie_metadata::stories)
    {
        const std::wstring_view logical(movie.logical);
        if (logical.starts_with(L"Rebellis02/ja/"))
        {
            // Authored logical names retain ja; the audited installed English
            // media lives in en, including scenes with separate Matinee audio.
            assert(std::string_view(movie.file).starts_with("en/"));
        }
        if (logical == L"Rebellis02/ja/cs_dw033_mv.usm" || logical == L"Rebellis02/ja/cs_dw034_mv.usm" ||
            logical == L"Rebellis02/ja/cs_dw025a_mv.usm" || logical == L"Rebellis02/ja/cs_dw025b_mv.usm")
        {
            // The ending uses synchronized movie dialogue/effects. Its English
            // movie soundtrack must remain audible through the complete scene.
            assert(!movie.matinee_audio);
            assert(std::string_view(movie.embedded_voice) == "en");
        }
        else if (logical.starts_with(L"Rebellis02/ja/") && !logical.ends_with(L"endroll.usm"))
        {
            assert(movie.matinee_audio);
            ++localized_soundtracks;
        }
        else
        {
            assert(!movie.matinee_audio);
        }
    }
    assert(localized_soundtracks == 9);
    SharedFrames consumer;
    consumer.create(GetCurrentProcessId());
    SharedFrames producer;
    producer.open(consumer.name(), GetCurrentProcessId());
    auto& header = producer.header();
    header.audio_channels = 2;
    header.audio_sample_rate = 48000;
    std::array<float, audio_packet_frames + 13> left{}, right{};
    for (size_t index = 0; index < left.size(); ++index)
    {
        left[index] = static_cast<float>(index);
        right[index] = -left[index];
    }
    float* planar[] = {left.data(), right.data()};
    assert(producer.publish_audio(2, static_cast<int>(left.size()), planar));
    std::array<float, audio_packet_frames * 2> received{};
    uint32_t count{};
    assert(consumer.consume_audio(received, count));
    assert(count == audio_packet_frames);
    for (uint32_t index = 0; index < count; ++index)
    {
        assert(received[2 * index] == left[index]);
        assert(received[2 * index + 1] == right[index]);
    }
    assert(consumer.consume_audio(received, count));
    assert(count == 13);
    assert(received[0] == left[audio_packet_frames]);
    assert(!consumer.consume_audio(received, count));

    // Enough wraps to exercise slot reuse while copying on a different thread.
    constexpr int packets = 10000;
    std::thread source(
        [&]
        {
            for (int index = 0; index < packets; ++index)
            {
                while (read(header.audio_published) - read(header.audio_consumed) >= audio_slot_count - 1)
                {
                    std::this_thread::yield();
                }
                left[0] = static_cast<float>(index);
                right[0] = -left[0];
                assert(producer.publish_audio(2, 1, planar));
            }
        });
    for (int index = 0; index < packets; ++index)
    {
        while (!consumer.consume_audio(received, count))
        {
            std::this_thread::yield();
        }
        assert(count == 1);
        assert(received[0] == static_cast<float>(index));
        assert(received[1] == -static_cast<float>(index));
    }
    source.join();
    assert(read(header.audio_error) == 0);

    for (uint32_t index = 0; index < audio_slot_count; ++index)
    {
        assert(producer.publish_audio(2, 1, planar));
    }
    // A stalled receiver fails closed without blocking or overwriting old PCM.
    assert(!producer.publish_audio(2, 1, planar));
    assert(read(header.audio_error) == 2);
    for (uint32_t index = 0; index < audio_slot_count; ++index)
    {
        assert(consumer.consume_audio(received, count));
    }
    assert(!producer.publish_audio(3, 1, planar));
    assert(read(header.audio_error) == 1);
    header.version = 1;
    bool rejected = false;
    try
    {
        consumer.header();
    }
    catch (const std::runtime_error&)
    {
        rejected = true;
    }
    assert(rejected);
}
