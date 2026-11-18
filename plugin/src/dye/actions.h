#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "stock.h"

// The swap: put a stored dyeable item on the character so the game's own dye
// screen can preview and dye it, then put everything back.
//
// Uses the game's own requests, built byte for byte as the game builds them
// (recorded on exe 1.0.0.3663):
//   TrocTrMoveItemInventoryToOtherInventoryReq  u32 itemKey, u16 itemLevel, u64 count,
//       u32 fromActor, u32 toActor, u16 fromInventoryKey (type + 1), u16 fromSlot,
//       u32 index into the source inventory type's list of destinations
//   TrocTrEquipItemReq    u16 inventoryKey (2 = bag), u16 bagSlot, u16 gearSlot
//   TrocTrUnequipItemReq  u16 gearSlot
// Every step is confirmed by reading the inventories again before the next one,
// and any failure stops the swap where it is: an item is always either worn or
// in the bag, never lost.
namespace dye::actions
{
    // Gear slot for an equip type (armor and cloaks), or -1 when the swap does not cover it.
    int GearSlotFor(uint32_t equipType);
    bool NoAtag(uint32_t equipType);   // A.T.A.G. gear while the dyer shows no A.T.A.G. tab

    bool TryOn(const stock::Entry& e);      // render thread; queued
    void RestoreSoon(unsigned delayMs);     // the dye screen closed; put things back after a short grace
    void CancelRestore();                   // the dye screen opened again in time
    void RestoreNow();                      // button

    struct Worn
    {
        std::string name;
        uint16_t fromType;
        bool active;
        bool waitingToGoBack;               // restore stopped part way; see Status()
    };
    Worn Current();
    bool Busy();
    std::string Status();

    void Drain();                                       // game thread only
    void OnMoveResult(uint32_t result);                 // the game's move routine answered
}
