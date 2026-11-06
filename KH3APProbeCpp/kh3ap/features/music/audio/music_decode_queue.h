#pragma once

#include "kh3ap/features/music/audio/music_decode_completion.h"

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace kh3ap
{
    // One active decode and one replaceable pending request per channel. The
    // worker exits when idle; shared state survives cancellation without joining
    // from DLL teardown or retaining a player/channel pointer.
    template <class Result, size_t Channels> class MusicDecodeQueue
    {
    public:
        using Completion = MusicDecodeCompletion<Result>;
        using Loader = std::function<Result(const std::filesystem::path&, std::stop_token)>;
        using Notify = std::function<void()>;

        MusicDecodeQueue(Loader loader, Notify notify)
            : state_(std::make_shared<State>(std::move(loader), std::move(notify)))
        {
        }
        MusicDecodeQueue(const MusicDecodeQueue&) = delete;
        auto operator=(const MusicDecodeQueue&) -> MusicDecodeQueue& = delete;
        ~MusicDecodeQueue()
        {
            const std::lock_guard lock{state_->mutex};
            state_->stopped = true;
            if (state_->active)
            {
                state_->active->cancel();
            }
            for (auto& request : state_->pending)
            {
                if (request)
                {
                    request->completion->cancel();
                    request.reset();
                }
            }
        }

        auto submit(size_t channel, const std::filesystem::path& file)
            -> std::shared_ptr<Completion>
        {
            auto completion = std::make_shared<Completion>();
            const std::lock_guard lock{state_->mutex};
            auto& pending = state_->pending.at(channel);
            if (pending)
            {
                pending->completion->cancel();
            }
            pending = Request{file, completion};
            if (!state_->running)
            {
                // Start under the lock so another submit cannot start a second worker.
                // Construction failure leaves no pending request that can never finish.
                try
                {
                    std::thread worker{[state = state_]
                                       {
                                           drain(state);
                                       }};
                    worker.detach();
                    state_->running = true;
                }
                catch (...)
                {
                    pending.reset();
                    throw;
                }
            }
            return completion;
        }

    private:
        struct Request
        {
            std::filesystem::path file;
            std::shared_ptr<Completion> completion;
        };
        struct State
        {
            State(Loader load, Notify notice) : loader(std::move(load)), notify(std::move(notice))
            {
            }
            std::mutex mutex;
            std::array<std::optional<Request>, Channels> pending{};
            std::shared_ptr<Completion> active;
            Loader loader;
            Notify notify;
            size_t next_channel{};
            bool running{}, stopped{};
        };
        static auto drain(const std::shared_ptr<State>& state) -> void
        {
            for (;;)
            {
                std::optional<Request> request;
                {
                    const std::lock_guard lock{state->mutex};
                    state->active.reset();
                    for (size_t offset = 0; !state->stopped && offset < Channels; ++offset)
                    {
                        const auto index = (state->next_channel + offset) % Channels;
                        auto& pending = state->pending[index];
                        if (!pending)
                        {
                            continue;
                        }
                        if (pending->completion->stop_token().stop_requested())
                        {
                            pending.reset();
                            continue;
                        }
                        request = std::move(pending);
                        pending.reset();
                        state->next_channel = (index + 1) % Channels;
                        state->active = request->completion;
                        break;
                    }
                    if (!request)
                    {
                        state->running = false;
                        return;
                    }
                }
                Result result{};
                try
                {
                    result = state->loader(request->file, request->completion->stop_token());
                }
                catch (...)
                {
                    // Decode failure follows the same completion path as invalid audio.
                }
                request->completion->publish(std::move(result), state->notify);
            }
        }
        std::shared_ptr<State> state_;
    };
} // namespace kh3ap
