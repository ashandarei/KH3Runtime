#pragma once
#include "kh02_map_session_state.h"
#include <map>
#include <string>
#include <utility>

namespace kh3ap
{
    // Retains weak identities only. Native validity/class lookup stays at the
    // observation site; callers invalidate authority before releasing presentation.
    template <class WeakReference> class Kh02SourceScriptRegistry
    {
      public:
        using Generation = Kh02MapSessionState::Generation;

        explicit Kh02SourceScriptRegistry(Kh02MapSessionState& state) : session_state(state) {}

        Kh02SourceScriptRegistry(const Kh02SourceScriptRegistry&) = delete;
        auto operator=(const Kh02SourceScriptRegistry&) -> Kh02SourceScriptRegistry& = delete;

        auto world() const -> const WeakReference&
        {
            return session_world;
        }

        void replace_world(WeakReference world)
        {
            session_world = std::move(world);
            invalidate();
        }

        template <class Matches> auto replacing(const std::wstring& identity, Matches&& current_matches) const -> bool
        {
            const auto previous = scripts.find(identity);
            return previous != scripts.end() && !current_matches(previous->second);
        }

        template <class Matches>
        auto accepts_script(const std::wstring& identity, Matches&& current_matches) const -> bool
        {
            const auto previous = scripts.find(identity);
            return previous == scripts.end() || current_matches(previous->second);
        }

        void record(const std::wstring& identity, WeakReference script)
        {
            scripts[identity] = std::move(script);
        }

        void invalidate()
        {
            scripts.clear();
            session_state.replace_source();
        }

        auto size() const -> std::size_t
        {
            return scripts.size();
        }

        auto generation() const -> Generation
        {
            return session_state.generation();
        }

        auto accepts(Generation generation) const -> bool
        {
            return session_state.accepts(generation);
        }

        auto accepts_continuation(Generation generation, bool owns_world) const -> bool
        {
            return session_state.accepts_continuation(generation, owns_world);
        }

      private:
        Kh02MapSessionState& session_state;
        WeakReference session_world{};
        std::map<std::wstring, WeakReference> scripts;
    };
} // namespace kh3ap
