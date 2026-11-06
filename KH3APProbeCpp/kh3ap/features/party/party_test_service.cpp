#include "kh3ap/bridge/transport/request_file_watcher.h"
#include <array>
#include <memory>
#include <optional>
#include "kh3ap/features/party/party_test_service.h"
#include "kh3ap/features/party/party_member_service.h"
#include "kh3ap/features/enemies/enemy_clear_service.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/utilities.h"

#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/UObject.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <sstream>
#include <utility>

namespace kh3ap
{
    namespace
    {
        constexpr auto minimum_party_burst_count = 2;

        constexpr auto max_party_test_request_lifetime_ms = 120000;
        constexpr auto party_test_lease_duration = std::chrono::seconds(90);
        constexpr auto party_test_wait_interval = std::chrono::milliseconds(100);
        constexpr auto party_test_update_interval = std::chrono::milliseconds(50);
        constexpr auto party_test_stability_duration = std::chrono::milliseconds(250);
        constexpr auto party_test_request_history_limit = 128;
        constexpr auto party_test_max_burst_count = 32;

        using Clock = std::chrono::steady_clock;
        constexpr auto request_name = L"runtime_party_test_request.json";
        constexpr auto result_name = L"runtime_party_test_result.json";
        constexpr auto events_name = L"runtime_party_test_events.jsonl";
        auto unix_ms() -> int64_t
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        }
    } // namespace

    struct RuntimePartyTestService::Impl
    {
        RuntimePartyTestConfig config;
        RequestFileWatcher requests{request_name};
        std::optional<std::filesystem::path> dir;
        std::wstring request_id, command, session, request_session;
        std::deque<std::wstring> consumed;
        RC::Unreal::FWeakObjectPtr session_pawn{};
        Clock::time_point next_poll{}, lease{}, started{}, stable_since{};
        int64_t expires{}, sequence{}, last_sequence{-1};
        bool waiting{};
        std::array<std::wstring, 2> stable_pawns{};
        uint64_t failures_before{};
        int burst_remaining{}, burst_index{};
        bool burst_random{}, burst_room{};
        size_t burst_first{}, burst_second{};

        explicit Impl(RuntimePartyTestConfig value) : config(std::move(value))
        {
        }

        void report(std::wstring_view status, std::wstring_view reason)
        {
            if (!dir)
            {
                return;
            }
            const auto s = config.party.test_snapshot();
            std::ostringstream out;
            out << std::boolalpha << "{\"schema\":\"kh3ap.party_test.v1\",\"request_id\":\""
                << json_escape(request_id) << "\",\"command\":\"" << json_escape(command)
                << "\",\"session_id\":\"" << json_escape(request_session)
                << "\",\"sequence\":" << sequence << ",\"process_id\":" << GetCurrentProcessId()
                << ",\"expires_unix_ms\":" << expires << ",\"status\":\"" << json_escape(status)
                << "\",\"reason\":\"" << json_escape(reason) << "\",\"at\":\"" << now_iso8601_utc()
                << "\",\"build\":\"" << __DATE__ << " " << __TIME__ << "\",\"elapsed_ms\":"
                << std::chrono::duration<double, std::milli>(Clock::now() - started).count()
                << ",\"available\":" << s.available << ",\"test_active\":" << s.test_active
                << ",\"pending\":" << s.pending << ",\"slots_readable\":" << s.slots_readable
                << ",\"slots_match\":" << s.slots_match << ",\"randomized\":" << s.randomized
                << ",\"every_room\":" << s.every_room << ",\"selected\":[" << s.selected[0] << ','
                << s.selected[1] << ']' << ",\"active\":[" << s.active[0] << ',' << s.active[1]
                << ']' << ",\"manager\":\"" << json_escape(s.manager) << "\",\"level\":\""
                << json_escape(s.level) << "\",\"map_policy\":\"" << json_escape(s.map_policy)
                << "\",\"pawns\":[\"" << json_escape(s.pawns[0]) << "\",\""
                << json_escape(s.pawns[1]) << "\"],\"classes\":[\"" << json_escape(s.classes[0])
                << "\",\"" << json_escape(s.classes[1]) << "\"]"
                << ",\"navigation\":[\"" << json_escape(s.navigation[0]) << "\",\""
                << json_escape(s.navigation[1]) << "\"]"
                << ",\"vanilla_npc_ids\":[\"" << json_escape(s.vanilla_npc_ids[0]) << "\",\""
                << json_escape(s.vanilla_npc_ids[1]) << "\"]"
                << ",\"active_npc_ids\":[\"" << json_escape(s.active_npc_ids[0]) << "\",\""
                << json_escape(s.active_npc_ids[1]) << "\"]"
                << ",\"requests\":" << s.requests << ",\"reconciles\":" << s.reconciles
                << ",\"failures\":" << s.failures << ",\"deletes\":" << s.deletes
                << ",\"spawns\":" << s.spawns << ",\"last_reconcile_ms\":" << s.last_reconcile_ms
                << ",\"last_native_work_ms\":" << s.last_native_work_ms;
            if (command == L"inspect" || command == L"begin")
            {
                out << ",\"choices\":[";
                for (size_t choice = 0; choice < config.party.choice_count(); ++choice)
                {
                    const auto definition = config.party.choice_definition(choice);
                    if (choice)
                    {
                        out << ',';
                    }
                    out << "{\"id\":" << choice << ",\"name\":\""
                        << json_escape(definition.display_name) << "\",\"character\":\""
                        << json_escape(definition.unique_character) << "\",\"npc\":\""
                        << json_escape(definition.npc_id)
                        << "\",\"unlocked\":" << config.party.choice_unlocked(choice) << '}';
                }
                out << ']';
            }
            out << "}\n";
            write_text_file_atomic_verified(*dir / result_name, out.str());
            append_text_file(*dir / events_name, out.str());
        }

        void restore()
        {
            config.party.test_end();
            session.clear();
            session_pawn = nullptr;
            burst_remaining = 0;
        }

        void fail(std::wstring_view reason, bool end_session)
        {
            // Retain the failing slots before restoration changes the evidence.
            report(L"error", reason);
            waiting = false;
            if (end_session)
            {
                restore();
            }
        }

        bool same_gameplay()
        {
            auto* const pawn = config.playable_sora();
            const bool observing_arrival = command == L"observe_arrival" || command == L"restore";
            return (observing_arrival ? config.control_ready() : config.gameplay_ready()) && pawn &&
                   is_uobject_valid_fast(pawn) && (session.empty() || session_pawn.Get() == pawn);
        }

        void accept()
        {
            dir = config.bridge_dir();
            if (!dir || !path_points_to_regular_file(*dir / request_name))
            {
                return;
            }
            const auto text = read_text_file(*dir / request_name);
            if (!text)
            {
                return;
            }
            try_remove_file(*dir / request_name);
            request_id = JsonFieldReader::string_field(*text, "request_id").value_or(L"");
            if (request_id.empty() ||
                std::find(consumed.begin(), consumed.end(), request_id) != consumed.end())
            {
                return;
            }
            consumed.push_back(request_id);
            if (consumed.size() > party_test_request_history_limit)
            {
                consumed.pop_front();
            }
            started = Clock::now();
            stable_since = {};
            burst_remaining = 0;
            command = JsonFieldReader::string_field(*text, "command").value_or(L"");
            request_session = JsonFieldReader::string_field(*text, "session_id").value_or(L"");
            sequence = JsonFieldReader::int64_field(*text, "sequence").value_or(-1);
            expires = JsonFieldReader::int64_field(*text, "expires_unix_ms").value_or(0);
            if (JsonFieldReader::string_field(*text, "schema").value_or(L"") !=
                L"kh3ap.party_test.v1")
            {
                fail(L"unsupported_request_schema", false);
                return;
            }
            if (JsonFieldReader::int64_field(*text, "process_id").value_or(0) !=
                GetCurrentProcessId())
            {
                fail(L"process_id_mismatch", false);
                return;
            }
            if (expires <= unix_ms() || expires > unix_ms() + max_party_test_request_lifetime_ms)
            {
                fail(L"invalid_or_expired_request", false);
                return;
            }
            if (command == L"inspect")
            {
                report(L"completed", L"party_snapshot");
                return;
            }
            if (command == L"begin")
            {
                const auto seed = JsonFieldReader::int64_field(*text, "seed");
                const auto all = JsonFieldReader::bool_field(*text, "all_choices");
                if (!session.empty() || request_session.empty() || sequence != 0 || !seed ||
                    *seed < 0 || !all)
                {
                    fail(L"invalid_or_busy_session", false);
                    return;
                }
                const auto snapshot = config.party.test_snapshot();
                if (!same_gameplay() || snapshot.map_policy != L"managed" ||
                    !snapshot.slots_readable)
                {
                    fail(L"requires_playable_sora_on_managed_map", false);
                    return;
                }
                if (!config.party.test_begin(static_cast<uint64_t>(*seed), *all))
                {
                    fail(L"party_unavailable_or_reconcile_busy", false);
                    return;
                }
                session = request_session;
                session_pawn = config.playable_sora();
                last_sequence = 0;
                lease = Clock::now() + party_test_lease_duration;
                report(L"completed", L"temporary_test_session_started");
                return;
            }
            if (session.empty() || request_session != session || sequence <= last_sequence)
            {
                fail(L"session_or_sequence_mismatch", false);
                return;
            }
            last_sequence = sequence;
            lease = Clock::now() + party_test_lease_duration;
            failures_before = config.party.test_snapshot().failures;
            if (command == L"keepalive")
            {
                // Owner-only lease renewal during a bounded map/Retry test.
                // This does not bind a new pawn or dispatch party mutations.
                report(L"completed", L"test_session_lease_renewed");
                return;
            }
            if (command == L"observe_arrival")
            {
                const auto expected = JsonFieldReader::string_field(*text, "expected_level");
                auto* const pawn = config.playable_sora();
                const auto snapshot = config.party.test_snapshot();
                if (!expected || expected->empty() || *expected != snapshot.level ||
                    !config.control_ready() || !is_uobject_valid_fast(pawn) ||
                    snapshot.map_policy != L"managed" || !snapshot.slots_readable)
                {
                    fail(L"arrival_requires_ready_sora_at_expected_level", false);
                    return;
                }
                // Explicit arrival observation adopts the new weak pawn only
                // after exact destination and gameplay validation. It does not
                // request a swap, so it measures production travel/Retry policy.
                session_pawn = pawn;
                waiting = true;
                report(L"accepted", L"observing_party_after_travel");
                return;
            }
            if (command == L"restore")
            {
                restore();
                waiting = true;
                report(L"accepted", L"restoring_original_party");
                return;
            }
            if (!same_gameplay() || config.party.test_snapshot().map_policy != L"managed")
            {
                fail(L"gameplay_or_map_changed", true);
                return;
            }
            bool accepted = false;
            if (command == L"damage_friend")
            {
                const auto slot = JsonFieldReader::int64_field(*text, "slot");
                const auto expected = JsonFieldReader::string_field(*text, "expected_pawn");
                const auto snapshot = config.party.test_snapshot();
                if (!slot || *slot < 0 || *slot > 1 || !expected || expected->empty() ||
                    snapshot.pending || !snapshot.slots_match || snapshot.pawns[*slot] != *expected)
                {
                    fail(L"damage_requires_stable_expected_friend", false);
                    return;
                }
                auto* target = config.party.active_party_pawns()[*slot];
                if (!is_uobject_valid_fast(target) || target->GetFullName() != *expected ||
                    !runtime_damage_friend_for_test(config.playable_sora(), target))
                {
                    fail(L"friend_damage_rejected", false);
                    return;
                }
                // Dispatch is not proof of knockout. The runner must observe
                // HP, native death state and subsequent recovery separately.
                report(L"completed", L"ordinary_friend_damage_dispatched");
                return;
            }
            if (command == L"configure" || command == L"burst")
            {
                const auto first = JsonFieldReader::int64_field(*text, "first");
                const auto second = JsonFieldReader::int64_field(*text, "second");
                const auto random = JsonFieldReader::bool_field(*text, "randomized");
                const auto room = JsonFieldReader::bool_field(*text, "every_room");
                const auto count = JsonFieldReader::int64_field(*text, "count").value_or(0);
                if (!first || !second || *first < 0 || *second < 0 || !random || !room ||
                    (command == L"burst" &&
                     (count < minimum_party_burst_count || count > party_test_max_burst_count)))
                {
                    fail(L"invalid_party_options", false);
                    return;
                }
                accepted = config.party.test_configure(*random, *room, static_cast<size_t>(*first),
                                                       static_cast<size_t>(*second));
                if (accepted && command == L"burst")
                {
                    burst_remaining = static_cast<int>(count) - 1;
                    burst_index = 0;
                    burst_random = *random;
                    burst_room = *room;
                    burst_first = static_cast<size_t>(*first);
                    burst_second = static_cast<size_t>(*second);
                }
            }
            else if (command == L"reroll" || command == L"room_event")
            {
                accepted = config.party.test_reroll(command == L"room_event");
            }
            else
            {
                fail(L"unknown_command", false);
                return;
            }
            if (!accepted)
            {
                fail(L"invalid_locked_or_duplicate_party_choice", false);
                return;
            }
            waiting = true;
            report(L"accepted", L"waiting_for_observed_party_slots");
        }

        void tick()
        {
            const auto now = Clock::now();
            if (!session.empty() && now >= lease)
            {
                report(L"error", L"session_lease_expired_restoring");
                waiting = false;
                restore();
            }
            if (!waiting)
            {
                if (now < next_poll)
                {
                    return;
                }
                next_poll = now + party_test_wait_interval;
                if (requests.update(config.bridge_dir(), true))
                {
                    accept();
                }
                return;
            }
            if (unix_ms() >= expires)
            {
                fail(L"party_observation_timeout", true);
                return;
            }
            if (!same_gameplay())
            {
                fail(L"gameplay_or_pawn_changed", true);
                return;
            }
            if (burst_remaining > 0)
            {
                --burst_remaining;
                ++burst_index;
                const bool accepted =
                    burst_random
                        ? config.party.test_reroll(false)
                        : config.party.test_configure(false, burst_room,
                                                      burst_index % 2 ? burst_second : burst_first,
                                                      burst_index % 2 ? burst_first : burst_second);
                if (!accepted)
                {
                    fail(L"burst_update_rejected", true);
                }
                return;
            }
            // Only explicit active tests inspect slots; sample at 20 Hz.
            if (now < next_poll)
            {
                return;
            }
            next_poll = now + party_test_update_interval;
            const auto snapshot = config.party.test_snapshot();
            if (snapshot.failures > failures_before)
            {
                fail(L"native_reconcile_failed", true);
                return;
            }
            if (!snapshot.pending && snapshot.slots_match)
            {
                if (stable_since == Clock::time_point{} || stable_pawns != snapshot.pawns)
                {
                    stable_since = now;
                    stable_pawns = snapshot.pawns;
                }
                if (now - stable_since >= party_test_stability_duration)
                {
                    report(L"ready", command == L"restore" ? L"original_party_restored"
                                                           : L"observed_stable_party_slots");
                    waiting = false;
                }
            }
            else
            {
                stable_since = {};
            }
        }
    };

    RuntimePartyTestService::RuntimePartyTestService(RuntimePartyTestConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }
    RuntimePartyTestService::~RuntimePartyTestService() = default;
    void RuntimePartyTestService::tick()
    {
        impl_->tick();
    }
} // namespace kh3ap
