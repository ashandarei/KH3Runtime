#include "cutscene_skip_service.h"
#include "cutscene_skip_transaction.h"
#include "cutscene_skip_view.h"
#include "native_cutscene_skip.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/bridge/utilities.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <chrono>
#include <sstream>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    namespace
    {
        using Clock = std::chrono::steady_clock;
        auto milliseconds() -> int64_t
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       Clock::now().time_since_epoch())
                .count();
        }
    } // namespace

    struct RuntimeCutsceneSkipService::Impl
    {
        explicit Impl(RuntimeCutsceneSkipConfig config) : config_(std::move(config))
        {
        }
        RuntimeCutsceneSkipConfig config_;
        CutsceneSkipView view_;
        CutsceneSkipTransaction transaction_;
        FWeakObjectPtr actor_, world_;
        std::wstring scene_name_;
        uint64_t revision_{};
        bool scene_open_{};
        int64_t next_check_{};

        bool enabled() const
        {
            return config_.skip_enabled_resolver && config_.skip_enabled_resolver();
        }

        void record(std::wstring_view action, std::wstring_view reason, uint64_t revision,
                    std::wstring_view scene)
        {
            Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] NativeCutsceneSkip action={} "
                                               "reason={} revision={} scene={}\n"),
                                           action, reason, revision, scene);
            const auto dir =
                config_.bridge_dir_resolver ? config_.bridge_dir_resolver() : std::nullopt;
            if (!dir)
            {
                return;
            }
            std::ostringstream out;
            out << "{\"schema\":\"kh3ap.cutscene_auto_skip.v2\",\"at\":\"" << now_iso8601_utc()
                << "\",\"build_stamp\":\"" << json_escape(config_.build_stamp) << "\",\"action\":\""
                << json_escape(action) << "\",\"result\":\"" << json_escape(reason)
                << "\",\"scene_revision\":" << revision << ",\"context\":\"" << json_escape(scene)
                << "\",\"route\":\"native_ui_manager_skip\",\"input_injected\":false,\"pause_"
                   "opened\":false}\n";
            append_text_file(*dir / L"cutscene_auto_skip_events.jsonl", out.str());
        }

        void observe(UObject* source, UObject* world, int type)
        {
            if (type == 0)
            {
                view_.arm(source);
            }
            // The native pause type is the eligibility signal. Encounter
            // camera scenes (for example he_02_camera_ms001) live under
            // /Game/Levels, not /Game/Cutscene, but expose the same native Skip.
            if (type != 1)
            {
                // Keep the old weak identity until the pending request observes
                // its transition. A later start on the same owner is a new scene.
                if (actor_.Get() == source || type == 0)
                {
                    scene_open_ = false;
                }
                return;
            }
            if (scene_open_ && actor_.Get() == source && world_.Get() == world)
            {
                return;
            }
            if (transaction_.active())
            {
                record(transaction_.requested() ? L"transition" : L"cancelled", L"next_scene",
                       transaction_.scene(), scene_name_);
                transaction_.cancel();
            }
            actor_ = make_uobject_weak(source);
            world_ = make_uobject_weak(world);
            scene_name_ = source->GetPathName();
            scene_open_ = true;
            ++revision_;
            // Identity observation is also useful when automatic skipping is off.
            // This preserves the diagnostic manual-skip chain's event signal.
            if (!enabled() || actor_.Get() != source || world_.Get() != world)
            {
                return;
            }
            transaction_.begin(revision_, milliseconds());
            next_check_ = 0;
            record(L"queued", L"scene_started", revision_, scene_name_);
        }

        void tick()
        {
            view_.tick();
            if (!transaction_.active())
            {
                return;
            }
            const auto now = milliseconds();
            if (now < next_check_)
            {
                return;
            }
            next_check_ = now + 50;
            auto* world = world_.Get();
            const bool same_world = is_uobject_valid_fast(world) && world == kh02_current_world();
            const bool alive = is_uobject_valid_fast(actor_.Get());
            const auto action =
                transaction_.advance(now, enabled(), same_world, alive,
                                     same_world && native_cutscene_pause_type(world) == 1,
                                     same_world && alive && Kh02Story::defers_cutscene_skip(actor_.Get(), world));
            if (action == CutsceneSkipTransaction::Action::Transition)
            {
                scene_open_ = false;
                record(L"transition", L"native_scene_transition_observed", transaction_.scene(),
                       scene_name_);
                return;
            }
            if (action == CutsceneSkipTransaction::Action::Cancel)
            {
                record(L"cancelled", L"expired_or_scene_no_longer_eligible", transaction_.scene(),
                       scene_name_);
                return;
            }
            if (action != CutsceneSkipTransaction::Action::Request)
            {
                return;
            }
            const auto revision = transaction_.scene();
            const auto name = scene_name_;
            // Set before dispatch: callbacks can synchronously start another
            // scene. Never update that new transaction with this one's result.
            transaction_.mark_requested();
            NativeCutsceneSkipResult result;
            try
            {
                result = request_native_cutscene_skip(world);
            }
            catch (const std::exception&)
            {
                if (transaction_.scene() == revision)
                {
                    transaction_.cancel();
                }
                record(L"cancelled", L"native_layout_validation_failed", revision, name);
                return;
            }
            if (result == NativeCutsceneSkipResult::Requested)
            {
                record(L"requested", L"native_fade_and_skip_requested", revision, name);
            }
            else if (result == NativeCutsceneSkipResult::Busy)
            {
                if (transaction_.scene() == revision)
                {
                    transaction_.defer_unissued_request();
                }
            }
            else
            {
                // Unsupported or unavailable coordinators fail closed.
                if (transaction_.scene() == revision)
                {
                    transaction_.cancel();
                }
                record(L"cancelled",
                       result == NativeCutsceneSkipResult::Unsupported
                           ? L"unsupported_native_profile"
                           : L"native_skip_unavailable",
                       revision, name);
            }
        }
    };

    RuntimeCutsceneSkipService::RuntimeCutsceneSkipService(RuntimeCutsceneSkipConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }
    RuntimeCutsceneSkipService::~RuntimeCutsceneSkipService() = default;
    RuntimeCutsceneSkipService::RuntimeCutsceneSkipService(RuntimeCutsceneSkipService&&) noexcept =
        default;
    auto RuntimeCutsceneSkipService::operator=(RuntimeCutsceneSkipService&&) noexcept
        -> RuntimeCutsceneSkipService& = default;

    auto RuntimeCutsceneSkipService::scene_identity() const -> std::wstring
    {
        return impl_->scene_name_ + L"#" + std::to_wstring(impl_->revision_);
    }

    auto RuntimeCutsceneSkipService::on_pause_menu_type(UFunction*, void*, UObject* context,
                                                        std::wstring_view, HookPhase phase)
        -> std::optional<int64_t>
    {
        if (phase != HookPhase::Post || !is_uobject_valid_fast(context))
        {
            return std::nullopt;
        }
        auto* world = kh02_actor_world(context);
        const auto type = native_cutscene_pause_type(world);
        if (type < 0 || type > 255)
        {
            return std::nullopt;
        }
        // Only capture here; never recursively advance a Blueprint from its
        // TresSetPauseMenuType post callback. Tick runs on the game thread.
        impl_->observe(context, world, type);
        return type;
    }

    void RuntimeCutsceneSkipService::tick()
    {
        impl_->tick();
    }
} // namespace kh3ap
