#include "kh02_challenge_movie_lease.h"
#include "kh02_challenge_ids.h"
#include "kh3ap/features/kh02/movies/kh02_menu_movie.h"
#include "kh3ap/features/kh02/wardrobe/kh02_wardrobe_menu.h"
#include "kh02_challenge_log.h"
#include "kh02_challenge_input_router.h"
#include "kh3ap/features/ui/ui_sound_service.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <algorithm>
#include <stdexcept>
#include <chrono>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        using namespace kh02menu;
        constexpr float preview_rotation_step_degrees = 15.f;
        constexpr int native_list_focus_up = 0;
        constexpr int native_list_focus_down = 1;
    } // namespace

    struct Kh02ChallengeLog::Impl
    {
        FWeakObjectPtr camp;
        Kh02ChallengeMovieLease movie_lease;
        std::vector<Kh02ChallengeRow> rows;
        std::wstring title = L"Challenges";
        std::function<void(int)> confirm;
        std::function<Kh02CostumeList(int)> costume_list;
        Kh02WardrobeMenu wardrobe;
        std::function<void(bool, float)> costume_preview;
        float preview_angle{25};
        Kh02ChallengeInputRouter input_router;
        RuntimeUiSoundService ui_sound;
        std::array<uint64_t, 3> sound_counts{};
        uint64_t sound_failures{};
        bool pending{}, visible{};
        int selected{};
        std::string error;

        void play_sound(RuntimeUiSound sound)
        {
            if (ui_sound.play(camp.Get(), sound))
            {
                ++sound_counts[static_cast<size_t>(sound)];
            }
            else
            {
                ++sound_failures;
            }
        }

        void move_selection(int delta)
        {
            bool changed = false;
            if (costume_list)
            {
                changed = wardrobe.move(movie_root(), delta);
            }
            else
            {
                const int next = std::clamp(selected + delta, 0, static_cast<int>(rows.size()) - 1);
                changed = next != selected;
                if (changed)
                {
                    // Arrow navigation scrolls only enough to reveal one row.
                    // Page inputs select their destination directly; walking
                    // through intermediate rows plays every selection animation.
                    if (delta == -1 || delta == 1)
                    {
                        const int direction = delta > 0 ? native_list_focus_down : native_list_focus_up;
                        invoke_movie(challenge_list(), L"moveAnchor", {direction, false});
                    }
                    select_row(next);
                }
            }
            if (changed)
            {
                play_sound(RuntimeUiSound::cursor);
            }
        }

        void adjust_horizontal(int delta)
        {
            if (wardrobe.horizontal(movie_root(), delta))
            {
                play_sound(RuntimeUiSound::cursor);
            }
        }

        UObject* movie_root()
        {
            return movie_lease.root();
        }

        UObject* challenge_list()
        {
            return find_movie_member(movie_root(),
                                     costume_list
                                         ? L"campMenu.contents.menuCostume.costumeItemWindow.costumeListAnm.costumeList"
                                         : L"campMenu.contents.menuChallenge.challengeListAnm.challengeList");
        }

        void close()
        {
            if (costume_list)
            {
                wardrobe.cancel();
            }
            if (costume_preview)
            {
                costume_preview(false, preview_angle);
            }
            movie_lease.hide();
            visible = pending = false;
            camp = {};
            input_router.reset();
        }

        void select_row(int index)
        {
            selected = std::clamp(index, 0, static_cast<int>(rows.size()) - 1);
            auto* challenge_list_object = challenge_list();
            if (!is_uobject_valid_fast(challenge_list_object))
            {
                throw std::runtime_error("kh02_challenge_list_missing");
            }
            if (auto index = try_gfx_get_member_value_number(gfx, challenge_list_object, L"index"))
            {
                invoke_movie(movie_root(), L"focusAnchor", {static_cast<int>(*index)});
            }
            try_gfx_set_member_value_int(gfx, challenge_list_object, L"focusIndex", selected);
            invoke_movie(challenge_list_object, L"adjustDisplay", {selected});
            if (!costume_list)
            {
                invoke_movie(movie_root(), L"setChallengeIcon", {selected, true});
            }
            auto* help_text = find_movie_member(movie_root(), L"campMenu.frame.messageAnm.message.textField");
            if (help_text)
            {
                try_gfx_set_member_value_bool(gfx, help_text, L"wordWrap", true);
                try_gfx_set_member_value_bool(gfx, help_text, L"multiline", true);
                try_gfx_set_member_value_number(gfx, help_text, L"height", 125);
                auto description = rows[selected].description;
                for (size_t position = 0; (position = description.find(L'\n', position)) != std::wstring::npos;
                     position += 3)
                {
                    description.replace(position, 1, L" - ");
                }
                if (!rows[selected].progress.empty())
                {
                    description += L"  [" + rows[selected].progress + L"]";
                }
                try_gfx_set_member_value_string(gfx, help_text, L"text", description);
            }
        }

        void show_rows()
        {
            auto* root_object = movie_root();
            if (!is_uobject_valid_fast(root_object) || !challenge_list())
            {
                return;
            }
            std::vector<MovieArgument> arguments;
            arguments.reserve(rows.size() * 7);
            for (size_t i = 0; i < rows.size(); ++i)
            {
                arguments.emplace_back(std::to_wstring(i + 1));
                arguments.emplace_back(rows[i].text);
                arguments.emplace_back(std::wstring{});
                arguments.emplace_back(std::wstring{});
                arguments.emplace_back(0);
                arguments.emplace_back(rows[i].state);
                arguments.emplace_back(rows[i].progress);
            }
            if (!costume_list)
            {
                invoke_movie(root_object, L"setChallengeListData", arguments);
            }
            // UIComponent.focus establishes this movie's own FocusManager owner.
            // focusAnchor alone is ignored until that owner has focus. Native
            // camp stays paused behind the overlay and keeps gameplay ownership.
            invoke_movie(root_object, L"focus", {-1});
            // openContents queues a page opening after the original status-out
            // animation. Our overlay has no visible status page to leave: that
            // delayed callback would restart an already visible page. Start the
            // selected page directly, once, before publishing the overlay.
            invoke_movie(root_object, costume_list ? L"openCostume" : L"openChallenge");
            if (auto* header = find_movie_member(root_object, L"campMenu.frame.framePathAnm"))
            {
                invoke_movie(header, L"gotoAndPlay", {std::wstring(L"in")});
            }
            invoke_movie(root_object, L"openButtonGuide");
            for (auto* path :
                 {L"campMenu.contents.menuStatus", L"campMenu.contents.menuCostume", L"campMenu.contents.menuCustomize",
                  L"campMenu.contents.menuGameHelp", L"campMenu.frame.playTime", L"campMenu.frame.gameMode"})
            {
                if (auto* child = find_movie_member(root_object, path))
                {
                    const bool show_costume = costume_list && std::wstring_view(path).ends_with(L"menuCostume");
                    try_gfx_set_member_value_bool(gfx, child, L"visible", show_costume);
                }
            }
            if (auto* label = find_movie_member(root_object, L"campMenu.frame.framePathAnm.framePath.txFramePathName"))
            {
                try_gfx_set_member_value_string(gfx, label, L"text", costume_list ? L"Wardrobe" : title);
            }
            if (costume_list)
            {
                if (auto* challenges = find_movie_member(root_object, L"campMenu.contents.menuChallenge"))
                {
                    try_gfx_set_member_value_bool(gfx, challenges, L"visible", false);
                }
                wardrobe.open(root_object);
            }
            else
            {
                if (auto* challenges = find_movie_member(root_object, L"campMenu.contents.menuChallenge"))
                {
                    try_gfx_set_member_value_bool(gfx, challenges, L"visible", true);
                }
                select_row(0);
            }
            input_router.reset_vertical_stats();
            if (!costume_list)
            {
                if (auto* guide =
                        find_movie_member(root_object, L"campMenu.frame.btnInfoAnm.btnInfo.btnInfoText.textField"))
                {
                    try_gfx_set_member_value_string(gfx, guide, L"text",
                                                    confirm ? L"Up/Down: Select    Enter / A: Travel    Esc / B: Back"
                                                            : L"Up/Down: Select    PgUp/PgDn: Page    Esc / B: Back");
                }
            }
            movie_lease.hide_native(camp.Get());
            try_gfx_set_member_value_bool(gfx, root_object, L"visible", true);
            visible = true;
            pending = false;
            error.clear();
            play_sound(RuntimeUiSound::decide);
        }

        void tick(UObject* pawn, bool available)
        {
            if (!available || !is_uobject_valid_fast(pawn))
            {
                if (visible || pending)
                {
                    close();
                }
                return;
            }
            if ((visible || pending) && !is_uobject_valid_fast(camp.Get()))
            {
                close();
                return;
            }
            if (!error.empty())
            {
                return;
            }
            try
            {
                if (!movie_lease.prepare(pawn))
                {
                    return;
                }
                if (pending)
                {
                    if (costume_list && !wardrobe.prepare(pawn))
                    {
                        return;
                    }
                    show_rows();
                }
                if (visible)
                {
                    if (costume_preview)
                    {
                        costume_preview(true, preview_angle);
                    }
                    if (costume_list)
                    {
                        wardrobe.tick(movie_root());
                    }
                    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now().time_since_epoch())
                                         .count();
                    if (const int delta = input_router.poll_vertical(now))
                    {
                        move_selection(delta);
                    }
                    if (costume_list)
                    {
                        if (const int delta = input_router.poll_horizontal(now))
                        {
                            adjust_horizontal(delta);
                        }
                    }
                }
            }
            catch (const std::exception& exception)
            {
                error = exception.what();
                close();
            }
        }
    };

    Kh02ChallengeLog::Kh02ChallengeLog() : impl(std::make_unique<Impl>()) {}

    Kh02ChallengeLog::~Kh02ChallengeLog()
    {
        impl->close();
    }

    void Kh02ChallengeLog::tick(UObject* pawn, bool available)
    {
        impl->tick(pawn, available);
    }

    auto Kh02ChallengeLog::open(UObject* camp, std::vector<Kh02ChallengeRow> rows) -> bool
    {
        if (!is_uobject_valid_fast(camp) || rows.size() != kh02_challenge_id::count || !impl->error.empty())
        {
            return false;
        }
        impl->costume_list = {};
        impl->costume_preview = {};
        impl->title = L"Challenges";
        impl->confirm = {};
        impl->camp = make_uobject_weak(camp);
        impl->rows = std::move(rows);
        impl->pending = true;
        return true;
    }

    auto Kh02ChallengeLog::open_selection(UObject* host, std::wstring title, std::vector<Kh02ChallengeRow> rows,
                                          std::function<void(int)> confirm) -> bool
    {
        if (!is_uobject_valid_fast(host) || rows.empty() || rows.size() > kh02_challenge_id::count || !confirm ||
            active() || !impl->error.empty())
        {
            return false;
        }
        impl->costume_list = {};
        impl->costume_preview = {};
        impl->camp = make_uobject_weak(host);
        impl->title = std::move(title);
        impl->rows = std::move(rows);
        impl->confirm = std::move(confirm);
        impl->pending = true;
        return true;
    }

    auto Kh02ChallengeLog::open_costumes(UObject* host, std::function<Kh02CostumeList(int)> list,
                                         std::function<void(int, int)> equip, std::function<std::string()> read,
                                         std::function<void(const std::string&)> write,
                                         std::function<void(bool, float)> preview) -> bool
    {
        if (!is_uobject_valid_fast(host) || !list || !equip || !read || !write || active() || !impl->error.empty())
        {
            return false;
        }
        impl->costume_list = std::move(list);
        impl->wardrobe.configure(impl->costume_list, std::move(equip), std::move(read), std::move(write));
        impl->costume_preview = std::move(preview);
        impl->preview_angle = 25;
        auto data = impl->costume_list(0);
        impl->rows = std::move(data.rows);
        impl->title = data.title;
        impl->camp = make_uobject_weak(host);
        impl->confirm = {};
        impl->pending = true;
        return true;
    }

    void Kh02ChallengeLog::close()
    {
        impl->close();
    }

    auto Kh02ChallengeLog::active() const -> bool
    {
        return impl->visible || impl->pending;
    }

    auto Kh02ChallengeLog::needs_tick() const -> bool
    {
        // Preparing the original movie remains eager. Once prepared and closed,
        // it has no frame work until an overlay is requested or the weak movie
        // expires. Visible overlays still poll controller repeats every frame.
        return active() || (impl->error.empty() && !impl->movie_lease.ready());
    }

    auto Kh02ChallengeLog::route(int controller, std::wstring_view key, int event, bool gamepad) -> bool
    {
        using Action = Kh02ChallengeInputRouter::Action;
        const auto now =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        const Kh02ChallengeInputRouter::Context context{active(), impl->visible, bool(impl->costume_list),
                                                        bool(impl->confirm)};
        return impl->input_router.route(
            controller, key, event, gamepad, now, context,
            [this](Action action, int delta)
            {
                switch (action)
                {
                case Action::InputMode:
                    impl->wardrobe.input_mode(impl->movie_root(), delta != 0);
                    break;
                case Action::Back:
                {
                    const bool close_menu = !impl->costume_list || impl->wardrobe.back(impl->movie_root());
                    impl->play_sound(RuntimeUiSound::cancel);
                    if (close_menu)
                    {
                        close();
                    }
                    break;
                }
                case Action::ConfirmWardrobe:
                    return impl->wardrobe.confirm(impl->movie_root());
                case Action::DecideFeedback:
                    impl->play_sound(RuntimeUiSound::decide);
                    break;
                case Action::RotatePreview:
                    impl->preview_angle += delta * preview_rotation_step_degrees;
                    break;
                case Action::Horizontal:
                    impl->adjust_horizontal(delta);
                    break;
                case Action::ConfirmSelection:
                {
                    const int selected = impl->selected;
                    auto callback = impl->confirm;
                    impl->play_sound(RuntimeUiSound::decide);
                    close();
                    impl->confirm = {};
                    callback(selected);
                    break;
                }
                case Action::Vertical:
                    impl->move_selection(delta);
                    break;
                }
                return false;
            },
            [this](const char* error)
            {
                impl->error = error;
                close();
            });
    }

    auto Kh02ChallengeLog::snapshot() -> std::string
    {
        nlohmann::json out{
            {"requested", impl->movie_lease.load_requested()},
            {"movie", is_uobject_valid_fast(impl->movie_lease.player())},
            {"movie_root_visible", to_utf8(try_gfx_get_member_value(gfx, impl->movie_root(), L"visible"))},
            {"pending", impl->pending},
            {"visible", impl->visible},
            {"selected", impl->selected},
            {"rows", impl->rows.size()},
            {"error", impl->error},
            {"title", to_utf8(impl->title)},
            {"can_confirm", bool(impl->confirm) || bool(impl->costume_list)},
            {"mode", impl->costume_list ? "dressing_room" : "challenges"}};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        for (const auto* timeline_path :
             {L"campMenu.contents.menuChallenge", L"campMenu.contents.menuCostume.costumeCmdAnm",
              L"campMenu.frame.framePathAnm"})
        {
            if (auto* timeline = find_movie_member(impl->movie_root(), timeline_path))
            {
                out["entrance_timelines"][to_utf8(timeline_path)] = {
                    {"frame", try_gfx_get_member_value_number(gfx, timeline, L"currentFrame").value_or(-1)},
                    {"label", to_utf8(try_gfx_get_member_value(gfx, timeline, L"currentLabel"))}};
            }
        }
#endif
        if (impl->costume_list)
        {
            out["wardrobe"] = impl->wardrobe.snapshot();
        }
        out["controller_repeat"] = {{"steps", impl->input_router.vertical_stats().steps},
                                    {"max_gap_ms", impl->input_router.vertical_stats().max_gap_ms}};
        out["horizontal_repeat"] = {{"steps", impl->input_router.horizontal_stats().steps},
                                    {"max_gap_ms", impl->input_router.horizontal_stats().max_gap_ms}};
        out["sound_feedback"] = {{"cursor", impl->sound_counts[static_cast<size_t>(RuntimeUiSound::cursor)]},
                                 {"decide", impl->sound_counts[static_cast<size_t>(RuntimeUiSound::decide)]},
                                 {"cancel", impl->sound_counts[static_cast<size_t>(RuntimeUiSound::cancel)]},
                                 {"failures", impl->sound_failures}};
        if (auto* challenge_list_object = impl->challenge_list())
        {
            out["focus"] = try_gfx_get_member_value_number(gfx, challenge_list_object, L"focusIndex").value_or(-1);
            out["scroll_position"] =
                try_gfx_get_member_value_number(gfx, challenge_list_object, L"scrollPosition").value_or(-1);
            out["visible_rows"] = try_gfx_get_member_value_number(gfx, challenge_list_object, L"rowNum").value_or(-1);
            if (auto* data = invoke_movie(challenge_list_object, L"getListData"))
            {
                double size = -1;
                invoke_movie(data, L"size", {}, &size);
                out["list_count"] = size;
            }
        }
        if (!impl->rows.empty())
        {
            out["selected_progress"] = to_utf8(impl->rows[impl->selected].progress);
        }
        if (!impl->rows.empty())
        {
            out["selected_text"] = to_utf8(impl->rows[impl->selected].text);
        }
        if (!impl->rows.empty())
        {
            out["selected_description"] = to_utf8(impl->rows[impl->selected].description);
        }
        return out.dump();
    }
} // namespace kh3ap
