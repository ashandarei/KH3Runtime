#pragma once
#include "kh3ap/features/journal/chest_journal_catalog.h"
#include "kh3ap/features/world/portals/portal_policy.h"
#include <set>
#include <string>

namespace kh3ap
{
    struct NativeChestJournalSnapshot
    {
        std::set<std::wstring> registered, opened;
    };

    struct ChestJournalPage
    {
        // A nonempty argument also represents a seed with no registered entries.
        std::wstring catalog{L"\n"}, collected;
        int count{};
    };

    inline auto chest_journal_page(const NativeChestJournalSnapshot& snapshot,
                                   portals::Kh02Access kh02_access = {}) -> ChestJournalPage
    {
        ChestJournalPage page;
        for (const auto& entry : chest_journal_entries)
        {
            const bool imported = entry.id.starts_with(L"DW_");
            if ((imported && !kh02_access.enabled) || !snapshot.registered.contains(std::wstring(entry.id)))
            {
                continue;
            }
            page.catalog += std::wstring(entry.id) + L'\t' + std::wstring(entry.name) + L"\t\t" +
                            std::wstring(entry.world) + L'~' + std::wstring(entry.description) +
                            L'~';
            if (entry.has_map && (!imported || kh02_access.unlocked))
            {
                page.catalog += L"chest/" + std::wstring(entry.id);
            }
            page.catalog += L'\n';
            ++page.count;
            if (snapshot.opened.contains(std::wstring(entry.id)))
            {
                if (!page.collected.empty())
                {
                    page.collected += L',';
                }
                page.collected += entry.id;
            }
        }
        return page;
    }
}
