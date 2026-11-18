#pragma once
#include <cstdint>
#include <vector>

// What Dye All needs from the running game: the event system (to watch and later
// send the game's own item events), the player actor, and every inventory the
// player owns. Ported from Master Looter's loot/game.cpp (MIT); all reads are guarded.
namespace dye::game
{
    struct Fns
    {
        uintptr_t tlsInit = 0, descLookup = 0, allocEvent = 0, enqueue = 0;
        uintptr_t descMask = 0, queue = 0;          // globals
        uintptr_t moveUpdate = 0;                   // per-tick game-thread pump
        uintptr_t invHolder = 0;                    // GetInventoryHolder(actor)
        uintptr_t controlGlobal = 0;
        unsigned  ctlController = 0, ctlActor = 0;
    };

    bool ResolveAll();          // pattern scans; seconds, call from a worker thread
    const Fns& F();
    uintptr_t FindEntry(const char* pattern, size_t* hits);

    // The actor the player is controlling (Kliff, Damiane, ...), 0 when unknown.
    uintptr_t ControlledActor();
    uint32_t  ActorEid(uintptr_t actor);   // A0100001 for Kliff; 0 when unreadable

    // The inventory holder: one object holding every bucket the player owns
    // (bag, camp storage, warehouse, dresser, ...). Game thread refreshes the
    // game's own answer; any thread may read Holder().
    void      RefreshHolder(uintptr_t actor);
    uintptr_t Holder(uintptr_t actor);

    struct Bucket { uintptr_t addr; uint16_t type, slotN, used, cap; };
    int Buckets(uintptr_t holder, Bucket* out, int max);

    struct Slot
    {
        uint64_t  instance;   // the slot's own u64 at +0
        uint16_t  row;        // runtime item row (iteminfo order)
        uint16_t  level;      // u16 at +0x0A; the move request names it
        uint16_t  invType;    // the bucket's inventory type
        int       bucket;     // bucket index on the holder
        uint16_t  slot;       // slot index inside the bucket
        long long count;
        uintptr_t addr;
    };
    int Slots(uintptr_t holder, std::vector<Slot>& out);

    // Display name of an inventory type row (Master Looter's 2760 table; logged
    // raw too, so a renumbering shows up).
    const char* InvTypeName(uint16_t type);
}
