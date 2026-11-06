#include "kh3ap/features/kh02/story/kh02_entry_session.h"
#include <cassert>
#include <chrono>
#include <stdexcept>
#include <string>

using Session = kh3ap::Kh02EntrySession;
using namespace std::chrono_literals;

namespace
{
    struct Fixture
    {
        Session session;
        Session::Clock::time_point now{};
        Session::Environment environment{true, false, true, false};
        Session::SceneObservation scene{};
        bool scene_ready = true;
        bool cleanup_ready = true;
        bool chain_finished = true;
        bool open_cover_during_preparation{};
        std::chrono::seconds start_delay{};
        int preparations{};
        int starts{};
        int cleanups{};
        int music_releases{};
        int gameplay_preparations{};

        void begin(bool first_intro = true)
        {
            session.reset_for_world(now);
            session.first_castle_intro = first_intro;
            session.configure(true, false);
        }

        void tick()
        {
            session.tick(environment,
                         {[&] { ++gameplay_preparations; },
                          [&]
                          {
                              ++preparations;
                              if (open_cover_during_preparation)
                              {
                                  environment.arrival_opening = true;
                                  environment.arrival_blocks_control = false;
                              }
                              return scene_ready;
                          },
                          [&]
                          {
                              ++starts;
                              now += start_delay;
                          },
                          [&]
                          {
                              ++cleanups;
                              return cleanup_ready;
                          },
                          [&] { return scene; }, [&] { return chain_finished; }, [&] { ++music_releases; },
                          [&] { return now; }, [&] { return environment.arrival_opening; },
                          [&] { return environment.arrival_blocks_control; }},
                         now);
        }

        void expect_timeout(const char* reason)
        {
            try
            {
                tick();
                assert(false);
            }
            catch (const std::runtime_error& error)
            {
                assert(std::string(error.what()) == reason);
                session.fail(error.what());
            }
            assert(session.done());
            assert(session.phase() == Session::Phase::error);
        }
    };
} // namespace

int main()
{
    // Disabled and retired sessions cannot dispatch any native operation.
    {
        Fixture fixture;
        fixture.tick();
        assert(fixture.preparations == 0);
        fixture.begin();
        fixture.session.retire();
        fixture.tick();
        assert(fixture.preparations == 0);
        assert(fixture.starts == 0);
        assert(fixture.session.phase() == Session::Phase::failed);
    }

    // Assets may prewarm behind the cover while input remains blocked. The
    // scene cannot start until both the cover and playable control are ready.
    {
        Fixture fixture;
        fixture.begin();
        fixture.environment = {false, true, false, true};
        fixture.tick();
        assert(fixture.preparations > 0);
        assert(fixture.starts == 0);
        fixture.environment = {false, true, true, false};
        fixture.tick();
        assert(fixture.starts == 0);
        fixture.environment.control_ready = true;
        fixture.tick();
        assert(fixture.starts == 1);
        assert(fixture.session.phase() == Session::Phase::playing);
    }

    // Observed playback alone cannot finish an opening: its original script
    // and followup chain must retire, then native view restoration must finish.
    {
        Fixture fixture;
        fixture.begin();
        fixture.tick();
        fixture.scene = {true, true, true, 12.f};
        fixture.tick();
        fixture.scene = {false, false, true, 0.f};
        fixture.tick();
        assert(fixture.cleanups == 0);
        fixture.scene.script_alive = false;
        fixture.chain_finished = false;
        fixture.tick();
        assert(fixture.cleanups == 0);
        fixture.chain_finished = true;
        fixture.cleanup_ready = false;
        fixture.tick();
        assert(fixture.session.phase() == Session::Phase::restoring_view);
        assert(!fixture.session.done());
        assert(fixture.session.maximum_position() == 12.f);
        assert(fixture.music_releases == 1);
        fixture.cleanup_ready = true;
        fixture.tick();
        assert(fixture.session.done());
        assert(fixture.session.phase() == Session::Phase::completed);
        fixture.tick();
        assert(fixture.starts == 1);
        assert(fixture.music_releases == 1);
    }

    // Native preparation can synchronously open the arrival cover. Read that
    // result after the operation, and start the playback deadline after native
    // startup rather than charging asset/startup work to movie duration.
    {
        Fixture fixture;
        fixture.begin();
        fixture.environment = {true, true, false, true};
        fixture.open_cover_during_preparation = true;
        fixture.start_delay = 100s;
        fixture.tick();
        assert(fixture.starts == 1);
        fixture.now += 1s;
        fixture.tick();
        assert(fixture.session.phase() == Session::Phase::playing);
        assert(!fixture.session.done());
    }

    // Revisits retain the settle window and use the shorter playback deadline.
    {
        Fixture fixture;
        fixture.begin(false);
        fixture.now += 1s;
        fixture.tick();
        assert(fixture.starts == 0);
        fixture.now += 1s;
        fixture.tick();
        assert(fixture.starts == 1);
        fixture.now += 31s;
        fixture.expect_timeout("story_entry_playback_timeout");
    }

    // Gameplay preparation and loading have independent bounded waits.
    {
        Fixture fixture;
        fixture.session.begin_preparation(fixture.now);
        fixture.tick();
        assert(fixture.gameplay_preparations == 1);
        assert(fixture.starts == 0);
        fixture.now += 91s;
        fixture.expect_timeout("kh02_entry_gameplay_timeout");
    }
    {
        Fixture fixture;
        fixture.begin();
        fixture.scene_ready = false;
        fixture.tick();
        fixture.now += 91s;
        fixture.expect_timeout("story_entry_load_timeout");
    }

    // The full opening gets its authored long window. Cancellation in that
    // window cannot accidentally start another scene or complete the story.
    {
        Fixture fixture;
        fixture.session.begin_preparation(fixture.now);
        fixture.session.first_castle_intro = true;
        fixture.session.initialized = true;
        fixture.session.preparation_finished();
        fixture.tick();
        fixture.now += 31s;
        fixture.tick();
        assert(!fixture.session.done());
        fixture.session.retire();
        fixture.now += 300s;
        fixture.tick();
        assert(fixture.starts == 1);
        assert(fixture.cleanups == 0);
        assert(fixture.session.phase() == Session::Phase::failed);
    }
}
