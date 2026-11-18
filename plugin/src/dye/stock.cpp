#include "stock.h"

#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <mutex>

#include "../core/log.h"
#include "../game/game.h"

namespace dye::stock
{
    static std::mutex g_lock;
    static std::vector<Entry> g_entries;
    static char g_status[200] = "Waiting for the game to load.";

    // Where the player keeps gear: bag 1, private chest (camp storage) 7,
    // warehouse 8, wardrobe (dresser) 14. Quest items and the rest stay out.
    static bool Listed(uint16_t t) { return t == 1 || t == 7 || t == 8 || t == 14; }

    void Refresh(bool logIt)
    {
        const uintptr_t actor = game::ControlledActor();
        const uintptr_t holder = actor ? game::Holder(actor) : 0;
        std::vector<game::Slot> slots;
        std::vector<Entry> found;
        char status[200];
        if (!actor) snprintf(status, sizeof status, "Player not found yet.");
        else if (!holder) snprintf(status, sizeof status, "Inventories not found yet.");
        else
        {
            game::Slots(holder, slots);
            for (const game::Slot& s : slots)
                if (const db::Item* it = Listed(s.invType) ? db::ByRow(s.row) : nullptr)
                    found.push_back({ it, s.invType, s.bucket, s.slot, s.instance, s.count });
            std::sort(found.begin(), found.end(), [](const Entry& a, const Entry& b) {
                if (a.invType != b.invType) return a.invType < b.invType;
                return a.item->name < b.item->name;
            });
            snprintf(status, sizeof status, "%zu dyeable items among %zu stored items.", found.size(), slots.size());
        }

        static bool s_loggedOnce = false;
        if (holder && (logIt || !s_loggedOnce))
        {
            s_loggedOnce = true;
            game::Bucket bk[64];
            const int nb = game::Buckets(holder, bk, 64);
            LOG("[stock] holder %llX: %d inventories, %zu occupied slots, %zu dyeable", static_cast<unsigned long long>(holder),
                nb, slots.size(), found.size());
            for (int b = 0; b < nb; ++b)
            {
                int items = 0, dyeable = 0;
                for (const game::Slot& s : slots) if (s.bucket == b) { ++items; if (db::ByRow(s.row)) ++dyeable; }
                LOG("[stock]   #%d type %u (%s): %u slots, %d items, %d dyeable", b, bk[b].type, game::InvTypeName(bk[b].type),
                    bk[b].slotN, items, dyeable);
            }
            int shown = 0;
            for (const Entry& e : found)
            {
                if (++shown > 150) break;
                LOG("[stock]   %-40s %-20s bucket %d slot %u row %d instance %016llX", e.item->Label(), game::InvTypeName(e.invType),
                    e.bucket, e.slot, e.item->row, static_cast<unsigned long long>(e.instance));
            }
        }

        std::lock_guard<std::mutex> g(g_lock);
        if (holder) g_entries.swap(found);
        strcpy_s(g_status, status);
    }

    std::vector<Entry> Snapshot()
    {
        std::lock_guard<std::mutex> g(g_lock);
        return g_entries;
    }

    const char* Status()
    {
        return g_status;
    }
}
