#pragma once

namespace kh3ap
{
    struct RuntimeMagicLinkGates
    {
        bool mp_enabled;
        bool magic_restricted;
        bool links_restricted;
    };

    constexpr auto resolve_runtime_magic_link_gates(
            bool magic_equipped, bool links_equipped,
            bool story_restricts_magic, bool story_restricts_links) -> RuntimeMagicLinkGates
    {
        return {
                magic_equipped || links_equipped,
                story_restricts_magic || !magic_equipped,
                story_restricts_links || !links_equipped,
        };
    }
}
