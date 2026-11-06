#include "shared/kh02/kh02_movie_shared.h"
#include <array>
#include <cassert>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

namespace
{
    using namespace kh02movie;

    template <typename Operation> void rejects_packet(Operation operation)
    {
        bool rejected{};
        try
        {
            operation();
        }
        catch (const std::runtime_error& error)
        {
            rejected = std::string_view(error.what()) == "movie_shared_audio_packet";
        }
        assert(rejected);
    }

    auto packet_at(Header& header, uint32_t index) -> AudioSlot*
    {
        auto* bytes = reinterpret_cast<unsigned char*>(&header);
        return reinterpret_cast<AudioSlot*>(bytes + video_mapping_bytes + index * sizeof(AudioSlot));
    }
} // namespace

int main()
{
    using namespace kh02movie;
    SharedFrames consumer;
    consumer.create(GetCurrentProcessId());
    SharedFrames producer;
    producer.open(consumer.name(), GetCurrentProcessId());
    auto& header = producer.header();
    header.audio_channels = audio_max_channels;
    header.audio_sample_rate = 48000;
    std::array<std::array<float, audio_packet_frames>, audio_max_channels> planar_data{};
    std::array<float*, audio_max_channels> planar{};
    for (uint32_t channel = 0; channel < audio_max_channels; ++channel)
    {
        planar[channel] = planar_data[channel].data();
        for (uint32_t frame = 0; frame < audio_packet_frames; ++frame)
        {
            planar_data[channel][frame] = static_cast<float>(channel * audio_packet_frames + frame);
        }
    }
    std::array<float, audio_packet_frames * audio_max_channels> received{};
    uint32_t count{};
    assert(!consumer.discard_audio());
    for (uint32_t packet = 0; packet < audio_slot_count; ++packet)
    {
        assert(producer.publish_audio(audio_max_channels, audio_packet_frames, planar.data()));
    }
    for (uint32_t packet = 0; packet < audio_slot_count - 1; ++packet)
    {
        assert(consumer.discard_audio());
    }
    assert(consumer.consume_audio(received, count));
    assert(count == audio_packet_frames);
    for (uint32_t channel = 0; channel < audio_max_channels; ++channel)
    {
        assert(received[channel] == planar_data[channel][0]);
        assert(received[(audio_packet_frames - 1) * audio_max_channels + channel] ==
               planar_data[channel][audio_packet_frames - 1]);
    }
    assert(read(header.audio_published) == read(header.audio_consumed));
    assert(!consumer.discard_audio());

    // Copy and discard share packet validation and release malformed slots
    // without accepting them or changing the consumed sequence.
    for (const bool discard : {false, true})
    {
        for (const auto [channels, frames] :
             std::array<std::pair<int, uint32_t>, 4>{{{0, 1},
                                                      {audio_max_channels + 1, 1},
                                                      {audio_max_channels, 0},
                                                      {audio_max_channels, audio_packet_frames + 1}}})
        {
            header.audio_channels = audio_max_channels;
            assert(producer.publish_audio(audio_max_channels, 1, planar.data()));
            const LONG before = read(header.audio_consumed);
            auto* packet = packet_at(header, static_cast<uint32_t>(before) % audio_slot_count);
            header.audio_channels = channels;
            packet->frames = frames;
            rejects_packet(
                [&]
                {
                    if (discard)
                    {
                        consumer.discard_audio();
                    }
                    else
                    {
                        consumer.consume_audio(received, count);
                    }
                });
            assert(read(packet->state) == 0);
            assert(read(header.audio_consumed) == before);
            header.audio_channels = audio_max_channels;
            InterlockedExchange(&header.audio_consumed, read(header.audio_published));
        }
    }

    // Copy still rejects undersized destinations. Discard has no destination,
    // and must consume an otherwise valid packet regardless of its PCM values.
    assert(producer.publish_audio(audio_max_channels, 2, planar.data()));
    rejects_packet([&] { consumer.consume_audio(std::span<float>(received.data(), 1), count); });
    InterlockedExchange(&header.audio_consumed, read(header.audio_published));
    planar_data[0][0] = std::numeric_limits<float>::quiet_NaN();
    assert(producer.publish_audio(audio_max_channels, 1, planar.data()));
    assert(consumer.discard_audio());
    planar_data[0][0] = 0;

    assert(producer.publish_audio(audio_max_channels, 1, planar.data()));
    const LONG before_bad_header = read(header.audio_consumed);
    auto* ready_packet = packet_at(header, static_cast<uint32_t>(before_bad_header) % audio_slot_count);
    header.version = 1;
    bool invalid_header{};
    try
    {
        consumer.discard_audio();
    }
    catch (const std::runtime_error&)
    {
        invalid_header = true;
    }
    assert(invalid_header && read(ready_packet->state) == 2);
    assert(read(header.audio_consumed) == before_bad_header);
    header.version = 2;
    assert(consumer.discard_audio());

    // Mix zero-copy discards and copies through thousands of ring wraps while
    // the separate producer reuses only slots released by the consumer.
    constexpr int packets = 4000;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::thread writer(
        [&]
        {
            for (int index = 0; index < packets; ++index)
            {
                while (read(header.audio_published) - read(header.audio_consumed) >= audio_slot_count - 1)
                {
                    assert(std::chrono::steady_clock::now() < deadline);
                    std::this_thread::yield();
                }
                planar_data[0][0] = static_cast<float>(index);
                assert(producer.publish_audio(audio_max_channels, 1, planar.data()));
            }
        });
    for (int index = 0; index < packets; ++index)
    {
        while (index % 2 == 0 ? !consumer.discard_audio() : !consumer.consume_audio(received, count))
        {
            assert(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
        if (index % 2)
        {
            assert(count == 1 && received[0] == static_cast<float>(index));
        }
    }
    writer.join();
    assert(read(header.audio_error) == 0);
    assert(read(header.audio_published) == read(header.audio_consumed));
    assert(!consumer.discard_audio());
}
