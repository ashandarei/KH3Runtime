#include "shared/kh02/kh02_movie_shared.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <stdexcept>
#include <iostream>
#include <thread>

template <class F> void rejects(F operation)
{
    bool rejected = false;
    try
    {
        operation();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    assert(rejected);
}

int main()
{
    using namespace kh02movie;
    SharedFrames consumer;
    consumer.create(GetCurrentProcessId());
    SharedFrames producer;
    producer.open(consumer.name(), GetCurrentProcessId());
    rejects([&] { producer.open(consumer.name(), GetCurrentProcessId()); });
    rejects([&] { consumer.create(GetCurrentProcessId()); });
    rejects(
        [&]
        {
            SharedFrames wrong;
            wrong.open(consumer.name(), GetCurrentProcessId() + 1);
        });
    rejects(
        [&]
        {
            SharedFrames wrong;
            wrong.open("http://127.0.0.1:80/", GetCurrentProcessId());
        });
    InterlockedExchange(&producer.header().total_frames, 120);
    InterlockedExchange(&producer.header().fps_numerator, 30);
    InterlockedExchange(&producer.header().fps_denominator, 1);
    producer.state(State::ready);
    assert(consumer.state() == State::ready);
    consumer.signal(Command::start);
    assert(producer.command() == Command::start);

    std::atomic<bool> complete{};
    std::thread writer(
        [&]
        {
            std::vector<uint8_t> pixels(frame_bytes);
            for (int frame = 0; frame < 120; ++frame)
            {
                std::fill(pixels.begin(), pixels.end(), static_cast<uint8_t>(frame));
                while (!producer.publish(frame, pixels))
                {
                    std::this_thread::yield();
                }
            }
            producer.state(State::ended);
            complete = true;
        });
    int last = -1;
    int samples = 0;
    std::vector<uint8_t> pixels;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!complete || last < 119)
    {
        int number = -1;
        if (consumer.consume(last, number, pixels))
        {
            assert(number > last);
            assert(pixels.size() == frame_bytes);
            assert(std::all_of(pixels.begin(), pixels.end(), [number](uint8_t value) { return value == number; }));
            last = number;
            ++samples;
        }
        assert(std::chrono::steady_clock::now() < deadline);
    }
    writer.join();
    assert(samples > 1 && last == 119);
    std::chrono::steady_clock::duration copy_time{};
    for (int iteration = 0; iteration < 60; ++iteration)
    {
        for (int frame = 0; frame < 3; ++frame)
        {
            std::fill(pixels.begin(), pixels.end(), static_cast<uint8_t>(frame));
            assert(producer.publish(frame, pixels));
        }
        const auto began = std::chrono::steady_clock::now();
        int number = -1;
        assert(consumer.consume(-1, number, pixels));
        copy_time += std::chrono::steady_clock::now() - began;
        assert(number == 2);
        assert(std::all_of(pixels.begin(), pixels.end(), [](uint8_t value) { return value == 2; }));
    }
    std::cout << "{\"backlog_samples\":60,\"average_consume_us\":"
              << std::chrono::duration<double, std::micro>(copy_time).count() / 60 << "}\n";
    rejects([&] { producer.publish(120, pixels); });
    rejects([&] { producer.publish(0, std::span<const uint8_t>(pixels.data(), 1)); });
    auto& header = consumer.header();
    auto* mapping = reinterpret_cast<unsigned char*>(&header);
    auto frame_slot = [&](unsigned index)
    { return reinterpret_cast<Slot*>(mapping + sizeof(Header) + index * (sizeof(Slot) + frame_bytes)); };
    for (int frame = 0; frame < 3; ++frame)
    {
        assert(producer.publish(frame, pixels));
    }
    frame_slot(1)->bytes = 1;
    rejects(
        [&]
        {
            int number = -1;
            consumer.consume(-1, number, pixels);
        });
    assert(read(frame_slot(0)->state) == 0);
    assert(read(frame_slot(1)->state) == 0);
    assert(read(frame_slot(2)->state) == 2);
    int recovered = -1;
    assert(consumer.consume(-1, recovered, pixels));
    assert(recovered == 2);
    header.image_pitch = 1;
    rejects([&] { producer.header(); });
    rejects([&] { consumer.header(); });
    header.image_pitch = pitch;
    assert(consumer.header().image_pitch == pitch);
    InterlockedExchange(&header.command, 999);
    rejects([&] { producer.command(); });
    consumer.signal(Command::cancel);
    assert(producer.command() == Command::cancel);
    InterlockedExchange(&header.state, 999);
    rejects([&] { consumer.state(); });
    producer.state(State::canceled);
    assert(consumer.state() == State::canceled);
}
