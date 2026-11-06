#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace kh3ap::channels
{
    enum class Kind { ClientCheckpoint, ClientCommand, NativeSnapshot, NativeCheckpoint, ReadOnlySnapshot, NativeEvent, NativeJournal };
    struct Definition
    {
        std::string_view name;
        Kind kind;
        constexpr auto client_owned() const -> bool
        { return kind == Kind::ClientCheckpoint || kind == Kind::ClientCommand; }
        constexpr auto checkpoint() const -> bool { return kind == Kind::ClientCheckpoint; }
        constexpr auto event() const -> bool { return kind == Kind::NativeEvent || kind == Kind::NativeJournal; }
    };

    // One registry owns native names, direction, durability and event semantics.
    // Keep wire names stable: the Python transport checks this registry for parity.
#define KH3AP_BRIDGE_CHANNELS(X) \
    X(runtime_fight_events, "runtime_fight_events.jsonl", NativeJournal) \
    X(ap_status, "ap_status.json", ClientCheckpoint) \
    X(ap_items, "ap_items.json", ClientCheckpoint) \
    X(game_state, "game_state.json", ClientCheckpoint) \
    X(item_delivery, "item_delivery.json", NativeSnapshot) \
    X(item_delivery_ledger, "item_delivery_ledger.json", NativeSnapshot) \
    X(item_delivery_state, "item_delivery_state.json", NativeSnapshot) \
    X(item_delivery_ledger_summary, "item_delivery_ledger_summary.json", NativeSnapshot) \
    X(item_consumer_plan, "item_consumer_plan.json", NativeSnapshot) \
    X(runtime_item_apply_state, "runtime_item_apply_state.json", NativeSnapshot) \
    X(runtime_item_grant_state, "runtime_item_grant_state.json", NativeSnapshot) \
    X(runtime_receipt_ack_event, "runtime_receipt_ack_event.json", NativeEvent) \
    X(runtime_music_chests, "runtime_music_chests.json", ReadOnlySnapshot) \
    X(runtime_music_chests_state, "runtime_music_chests_state.json", NativeSnapshot) \
    X(runtime_keyblade_chests, "runtime_keyblade_chests.json", ReadOnlySnapshot) \
    X(runtime_mom_clear_state, "runtime_mom_clear_state.json", ClientCheckpoint) \
    X(current_state_write_polyhook_events, "current_state_write_polyhook_events.jsonl", NativeJournal) \
    X(runtime_autosave_probe_file_events, "runtime_autosave_probe_file_events.jsonl", NativeJournal) \
    X(runtime_autosave_probe_auto_save_request_events, "runtime_autosave_probe_auto_save_request_events.jsonl", NativeJournal) \
    X(runtime_autosave_probe_save_completed_events, "runtime_autosave_probe_save_completed_events.jsonl", NativeJournal) \
    X(lucky_mark_native_ap_resolver_diagnostics, "lucky_mark_native_ap_resolver_diagnostics.jsonl", NativeJournal) \
    X(runtime_item_grant, "runtime_item_grant.json", ClientCommand) \
    X(runtime_item_grant_priority, "runtime_item_grant.priority.json", ClientCommand) \
    X(runtime_item_grant_readiness, "runtime_item_grant_readiness.json", NativeSnapshot) \
    X(runtime_trap_queue_request, "runtime_trap_queue_request.json", ClientCommand) \
    X(runtime_trap_queue_ack, "runtime_trap_queue_ack.json", NativeEvent) \
    X(runtime_trap_queue_state, "runtime_trap_queue_state.json", NativeSnapshot) \
    X(runtime_ap_message, "runtime_ap_message.json", ClientCommand) \
    X(runtime_receipt_catalog, "runtime_receipt_catalog.json", ClientCommand) \
    X(runtime_reconciliation_state, "runtime_reconciliation_state.json", NativeSnapshot) \
    X(runtime_save_receipts, "runtime_save_receipts.json", NativeSnapshot) \
    X(runtime_check_state, "runtime_check_state.json", NativeSnapshot) \
    X(runtime_item_descriptions, "runtime_item_descriptions.json", NativeSnapshot) \
    X(runtime_status, "runtime_status.json", NativeSnapshot) \
    X(runtime_secret_report_hints, "runtime_secret_report_hints.json", NativeSnapshot) \
    X(runtime_death_retry_replay_window, "runtime_death_retry_replay_window.json", NativeSnapshot) \
    X(runtime_death_retry_baseline, "runtime_death_retry_baseline.json", NativeSnapshot) \
    X(runtime_display_save_slot, "runtime_display_save_slot.json", NativeSnapshot) \
    X(runtime_mom_barrier_unlock, "runtime_mom_barrier_unlock.json", ClientCommand) \
    X(runtime_gameflow_command, "runtime_gameflow_command.json", ClientCommand) \
    X(runtime_warp_command, "runtime_warp_command.json", ClientCommand) \
    X(runtime_gameflow_result, "runtime_gameflow_result.json", NativeSnapshot) \
    X(runtime_warp_result, "runtime_warp_result.json", NativeSnapshot) \
    X(runtime_loot_event, "runtime_loot_event.json", NativeEvent) \
    X(runtime_item_apply_event, "runtime_item_apply_event.json", NativeSnapshot) \
    X(runtime_notification_event, "runtime_notification_event.json", NativeEvent) \
    X(runtime_context, "runtime_context.json", NativeSnapshot) \
    X(runtime_minimap_context, "runtime_minimap_context.json", NativeSnapshot) \
    X(runtime_map_ready, "runtime_map_ready.json", NativeSnapshot) \
    X(runtime_sora_state, "runtime_sora_state.json", NativeSnapshot) \
    X(runtime_blocking_ui_state, "runtime_blocking_ui_state.json", NativeSnapshot) \
    X(runtime_current_save_slot, "runtime_current_save_slot.json", NativeSnapshot) \
    X(runtime_ap_message_result, "runtime_ap_message_result.json", NativeSnapshot) \
    X(runtime_lucky_emblem_state, "runtime_lucky_emblem_state.json", NativeSnapshot) \
    X(runtime_classic_kingdom_state, "runtime_classic_kingdom_state.json", NativeCheckpoint) \
    X(runtime_lucky_mark_checks, "runtime_lucky_mark_checks.json", NativeSnapshot) \
    X(runtime_level_up_checks, "runtime_level_up_checks.json", NativeSnapshot) \
    X(runtime_deathlink_state, "runtime_deathlink_state.json", NativeSnapshot) \
    X(runtime_deathlink_control, "runtime_deathlink_control.json", ClientCommand) \
    X(runtime_kh02_launch_request, "runtime_kh02_launch_request.json", NativeSnapshot) \
    X(runtime_kh02_launch_response, "runtime_kh02_launch_response.json", ClientCommand) \
    X(runtime_ingredient_state, "runtime_ingredient_state.json", NativeCheckpoint) \
    X(runtime_adversary_state, "runtime_adversary_state.json", NativeSnapshot) \
    X(runtime_minigame_rewards, "runtime_minigame_rewards.json", NativeCheckpoint) \
    X(runtime_workshop, "runtime_workshop.json", NativeCheckpoint) \
    X(runtime_olympus_coliseum_state, "runtime_olympus_coliseum_state.json", NativeCheckpoint) \
    X(runtime_new_game_staging, "runtime_new_game_staging.json", NativeSnapshot) \
    X(runtime_kh3_retry_point_event, "runtime_kh3_retry_point_event.json", NativeSnapshot) \
    X(runtime_kh3_retry_blueprint_event, "runtime_kh3_retry_blueprint_event.json", NativeSnapshot) \
    X(runtime_autosave_probe_save_completed_event, "runtime_autosave_probe_save_completed_event.json", NativeSnapshot) \
    X(runtime_autosave_probe_file_event, "runtime_autosave_probe_file_event.json", NativeSnapshot) \
    X(runtime_autosave_probe_event, "runtime_autosave_probe_event.json", NativeSnapshot) \
    X(runtime_autosave_probe_auto_save_request_event, "runtime_autosave_probe_auto_save_request_event.json", NativeSnapshot) \
    X(runtime_mom_barrier_state, "runtime_mom_barrier_state.json", NativeSnapshot) \
    X(runtime_mom_barrier_interaction, "runtime_mom_barrier_interaction.json", NativeSnapshot) \
    X(runtime_space_checks, "runtime_space_checks.json", NativeSnapshot) \
    X(runtime_vbonus_checks, "runtime_vbonus_checks.json", NativeSnapshot) \
    X(runtime_event_checks, "runtime_event_checks.json", NativeSnapshot) \
    X(runtime_chest_checks, "runtime_chest_checks.json", NativeSnapshot)

#define KH3AP_CHANNEL_NAMES(Id, Name, Category) \
    inline constexpr char Id[] = Name; \
    inline constexpr wchar_t Id##_file[] = L##Name;
    KH3AP_BRIDGE_CHANNELS(KH3AP_CHANNEL_NAMES)
#undef KH3AP_CHANNEL_NAMES

#define KH3AP_CHANNEL_ENTRY(Id, Name, Category) Definition{Id, Kind::Category},
    inline constexpr std::array registry{KH3AP_BRIDGE_CHANNELS(KH3AP_CHANNEL_ENTRY)};
#undef KH3AP_CHANNEL_ENTRY
#undef KH3AP_BRIDGE_CHANNELS

    constexpr auto find(std::string_view name) -> const Definition*
    {
        for (const auto& channel : registry) if (channel.name == name) return &channel;
        return nullptr;
    }
    constexpr auto known(std::string_view name) -> bool { return find(name) != nullptr; }
    constexpr auto event_channel(std::string_view name) -> bool
    { const auto* channel = find(name); return channel && channel->event(); }
    constexpr auto client_channel(std::string_view name) -> bool
    { const auto* channel = find(name); return channel && channel->client_owned(); }
    constexpr auto checkpoint_channel(std::string_view name) -> bool
    { const auto* channel = find(name); return channel && channel->checkpoint(); }
    constexpr auto durable_native_channel(std::string_view name) -> bool
    { const auto* channel = find(name); return channel && channel->kind == Kind::NativeCheckpoint; }
    constexpr auto allows_client_operation(std::string_view name, uint8_t operation) -> bool
    {
        const auto* channel = find(name);
        if (!channel) return false;
        switch (operation)
        {
        case 1: return channel->client_owned();
        case 2: return channel->kind != Kind::ReadOnlySnapshot;
        case 3: return channel->event();
        case 4: return channel->checkpoint();
        default: return false;
        }
    }
    static_assert([] {
        for (std::size_t i = 0; i < registry.size(); ++i)
            for (std::size_t j = i + 1; j < registry.size(); ++j)
                if (registry[i].name == registry[j].name) return false;
        return true;
    }(), "Duplicate KH3 IPC channel");
}
