#pragma once
#include <cstdint>

namespace dye::hooks
{
    // Hooks the dye screen's show/hide, the game's event queue (watched, never
    // altered) and the per-tick pump. Call from the worker after game::ResolveAll.
    void Install();
    void Remove();

    bool DyeHooksOk();
    bool DyeScreenOpen();
    int AtagTab();   // 1 the dyer shows an A.T.A.G. tab, 0 it does not, -1 not seen yet
    uintptr_t DyeScreen();      // the UIGamePlayControlRootDyeing instance, 0 when closed
    unsigned long PumpTicks();
    long SpyLines();
    // After the dye screen closes, a swapped item stays on this long (reopening keeps it).
    inline constexpr unsigned kGraceMs = 10000;
    unsigned long SinceDyeClosed();
    // Rebuild the open dye screen's view so it sees gear changed while it was open.
    // Game thread only; false when the screen is closed or the call is not safe.
    bool TabBounce();
    // Game thread: applies the picked colors exactly like confirming the Dye
    // button. -1 when it could not run; otherwise the game's result code.
    int ApplyDyeNow();
    // Mercenary key of the horse (mode 1) or A.T.A.G. (mode 2) the dyer shows;
    // Learn passes the dyer's preview through that tab once if not known yet
    // (game thread, dye screen open). 0 when there is none.
    uint64_t MercKey(int mode);
    uint64_t LearnMercKey(int mode);
    long DyeAcks();           // how many times the game confirmed a dye (TrocTrDyeItemListByEquipSlotAck)
    long FailEquipAcks();     // how many times the game refused an equip (TrocTrFailEquipItemAck)
    int UnappliedDye();       // 1 colors picked but not applied, 0 none, -1 cannot tell
    bool HasUnappliedDye();   // colors picked on the dye screen but not applied with Dye yet   // what RB to the horse tab and back does; refreshes the dye screen's gear
    // 1 = tab refresh, 2 = soft reopen; run from the dye screen's update after delayMs.
    void RequestRefresh(int kind, unsigned delayMs);   // ms since the dye screen last closed; huge when never

    // For sending the game's own requests (actions.cpp).
    uint16_t DescId(const char* cls);   // runtime descriptor id by class name, 0 until known
    bool RouteKnown();
    uint32_t Route();
    uint32_t Player();                  // the player's event id (A01xxxxx), 0 until seen
    bool OnGameThread();
}
