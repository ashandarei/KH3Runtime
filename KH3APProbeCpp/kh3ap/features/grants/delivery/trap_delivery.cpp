#include "kh3ap/features/grants/trap_catalog.h"
#include "kh3ap/app/config.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/bridge/rewards/trap_queue.h"
#include "kh3ap/bridge/session/deathlink.h"
#include "kh3ap/features/grants/delivery/item_delivery_internal.h"
#include "kh3ap/features/grants/delivery/item_delivery_binding.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/features/grants/grant_state.h"
#include "kh3ap/app/state.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <atomic>
#define NOMINMAX
#include <windows.h>
#include <DynamicOutput/DynamicOutput.hpp>

namespace kh3ap
{
    using ::kh3ap::settings::s_enable_runtime_item_grant_apply;

    auto process_pending_native_traps(const RuntimeItemGrantPollDeps& deps) -> void
    {
        const RuntimeHotPathProfileScope profile{L"native_traps:poll"};
        static std::atomic_flag running = ATOMIC_FLAG_INIT;
        if (running.test_and_set()) return;
        struct Guard { std::atomic_flag& flag; ~Guard() { flag.clear(); } } guard{running};
        if (!deps.bridge_dir) return;
        const auto path = *deps.bridge_dir / native_traps::request_file;
        const auto [revision, submission] = pipe::grant_batch(path);
        static uint64_t seen_revision{};
        static std::filesystem::path seen_path;
        const auto current_identity = [] { return item_delivery::installed_delivery_seed_identity(); };
        if (seen_path != path) { seen_path = path; seen_revision = 0; }
        if (submission && revision != seen_revision)
        {
            seen_revision = revision;
            const auto identity = current_identity();
            const auto status = read_text_file(*deps.bridge_dir / channels::ap_status_file).value_or("");
            const auto error = item_delivery::delivery_binding_error(submission->request_schema,
                submission->process_id, GetCurrentProcessId(), submission->seed_name, submission->slot,
                JsonFieldReader::string_field(status, "seed_name").value_or(L""),
                JsonFieldReader::int64_field(status, "slot").value_or(0), identity.seed, identity.slot);
            if (error.empty()) native_traps::submit(submission);
            else native_traps::reject(submission, std::string(error.begin(), error.end()));
        }
        const auto state = native_traps::view();
        if (!state->head || !state->ready) return;
        if (deathlink::view()->fenced) { native_traps::waiting("deathlink_transition_pending"); return; }
        const auto identity = current_identity();
        if (identity.seed != state->seed || identity.slot != state->slot)
        { native_traps::waiting("runtime_seed_mismatch"); return; }
        const auto ap_status = read_text_file(*deps.bridge_dir / channels::ap_status_file).value_or("");
        const auto binding = item_delivery::delivery_binding_error(L"kh3ap.item_delivery.v1", GetCurrentProcessId(),
            GetCurrentProcessId(), state->seed, state->slot,
            JsonFieldReader::string_field(ap_status, "seed_name").value_or(L""),
            JsonFieldReader::int64_field(ap_status, "slot").value_or(0), identity.seed, identity.slot);
        if (!binding.empty()) { native_traps::waiting(std::string(binding.begin(), binding.end())); return; }
        const auto& request = *state->head;
        const auto now = std::chrono::steady_clock::now();
        const auto policy = runtime_item_delivery_policy(std::span{&request, size_t{1}}, request.action_kind, false);
        auto context = item_delivery::prepare_delivery_context(deps, policy, false, now);
        if (!context.safety.ready)
        {
            native_traps::waiting(std::string(context.safety.reason.begin(), context.safety.reason.end()));
            return;
        }
        if constexpr (!s_enable_runtime_item_grant_apply) return;
        if (context.candidates.empty()) context.candidates = item_delivery::collect_delivery_world_candidates(deps, false);
        if (context.candidates.empty()) { native_traps::waiting("no_playable_sora"); return; }
        RuntimeItemApplyPayload payload{};
        std::wstring failure;
        RC::Unreal::UObject* owner{};
        bool unsafe{};
        const RuntimeHotPathProfileScope apply_profile{L"native_traps:apply"};
        const bool applied = try_apply_runtime_item_grant_request(request, context.candidates,
            deps.apply_deps, &payload, &failure, &owner, &unsafe);
        if (applied)
        {
            if (request.grant_method != king_mickey_trap_method)
            {
                native_traps::applied(request.index);
            }
            RC::Output::send<RC::LogLevel::Verbose>(L"[KH3APProbeCpp] Native trap applied index={} token={}\n", request.index, request.runtime_token);
        }
        else native_traps::waiting("waiting_for_trap_state");
    }
}
