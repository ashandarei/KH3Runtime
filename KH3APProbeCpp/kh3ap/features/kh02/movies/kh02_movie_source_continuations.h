#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <functional>
#include <string>
#include <string_view>

namespace kh3ap
{
    class Kh02MovieSourceContinuations
    {
      public:
        struct Completion
        {
            RC::Unreal::FWeakObjectPtr script;
            std::wstring function;
            int linkage{-1};
            bool gear_event{};
            void dispatch(std::string_view movie, const std::function<bool()>& same_generation) const;
        };

        void bind(RC::Unreal::UObject* script, bool gear_event);
        void release_source();

        auto source() const -> RC::Unreal::UObject*
        {
            return script.Get();
        }

        void reset_waits();
        void reset_request();
        void retire();
        void wait(RC::Unreal::UObject* caller, int linkage, const std::wstring& function, bool story);
        void wait_finish(RC::Unreal::UObject* caller, int linkage, const std::wstring& function, bool story,
                         bool completed, RC::Unreal::UObject* pawn, RC::Unreal::UObject* world);

        auto ready_pending() const -> bool
        {
            return !ready_sent && ready_linkage >= 0;
        }

        void dispatch_ready();

        auto completion() const -> Completion
        {
            return {script, finish_function, finish_linkage, gear_finish};
        }

        auto ready_callback_sent() const -> bool
        {
            return ready_sent;
        }

        auto has_gear_finish() const -> bool
        {
            return gear_finish;
        }

        auto completed_wait_count() const -> int
        {
            return completed_waits;
        }

        auto terminal_wait_count() const -> int
        {
            return terminal_finish_waits;
        }

      private:
        RC::Unreal::FWeakObjectPtr script;
        std::wstring ready_function, finish_function;
        int ready_linkage{-1}, finish_linkage{-1};
        bool ready_sent{}, gear_finish{};
        int completed_waits{}, terminal_finish_waits{};
    };
} // namespace kh3ap
