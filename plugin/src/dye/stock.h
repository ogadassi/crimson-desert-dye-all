#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "dyedb.h"

// Every dyeable item the player owns, across every inventory on their holder.
namespace dye::stock
{
    struct Entry
    {
        const db::Item* item;
        uint16_t  invType;
        int       bucket;
        uint16_t  slot;
        uint64_t  instance;
        long long count;
    };

    // Re-reads the inventories (guarded reads; safe from the render thread).
    // Writes a summary to the log the first time and when `logIt` is set.
    void Refresh(bool logIt);
    std::vector<Entry> Snapshot();
    const char* Status();   // one line for the panel
}
