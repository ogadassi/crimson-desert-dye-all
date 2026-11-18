#include "hooks.h"

#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "../core/log.h"
#include "../core/state.h"
#include "../game/farhook.h"
#include "../game/game.h"
#include "../game/mem.h"
#include "../game/signatures.h"
#include "actions.h"
#include <map>

namespace dye::hooks
{
    using namespace ml;

    // Hook helper from Master Looter: stacks on another mod's jump when one is already there.
    static bool Hook(const char* what, uintptr_t target, void* detour, void** original)
    {
        if (!target) return false;
        uint8_t first = 0;
        char why[96];
        if (mem::Read8(target, &first) && first == 0xE9 && !mem::InImage(mem::RipAt(target, 5)))
        {
            if (!farhook::InstallOverJump(what, target, detour, original, why, sizeof why))
            { LOG_ERR("[hook] %s: could not stack on another mod's jump: %s", what, why); return false; }
            LOG("[hook] %s hooked at +0x%llX, stacked on another mod's jump", what, static_cast<unsigned long long>(mem::Rva(target)));
            return true;
        }
        if (const uintptr_t theirs = farhook::AbsJumpTarget(target); theirs && !mem::InImage(theirs))
        {
            if (!farhook::InstallOverAbsJump(what, target, detour, original, why, sizeof why))
            { LOG_ERR("[hook] %s: could not stack on another mod's patch: %s", what, why); return false; }
            LOG("[hook] %s hooked at +0x%llX, stacked on another mod's absolute jump", what, static_cast<unsigned long long>(mem::Rva(target)));
            return true;
        }
        if (!farhook::Install(what, target, detour, original, why, sizeof why))
        { LOG_ERR("[hook] %s: %s", what, why); return false; }
        LOG("[hook] %s hooked at +0x%llX", what, static_cast<unsigned long long>(mem::Rva(target)));
        return true;
    }

    // ---------------------------------------------------------------- pump ----
    typedef uint64_t (*Fn8)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    static Fn8 oMove = nullptr;
    static volatile LONG g_pumpTicks = 0;
    static volatile LONG g_gameTid = 0;

    typedef void  (__fastcall *FnTlsInit)();
    typedef void* (__fastcall *FnDescLookup)(uint64_t zero, uint32_t descId, uint32_t mask);

    static SRWLOCK g_idLock = SRWLOCK_INIT;
    static std::map<std::string, uint16_t>* g_ids = nullptr;
    static volatile LONG g_route = 0, g_routeKnown = 0, g_player = 0;
    static thread_local int t_selfSend = 0;

    static void* LookupDesc(uintptr_t fn, uint32_t id, uint32_t mask)
    {
        __try { return reinterpret_cast<FnDescLookup>(fn)(0, id, mask); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    static bool Interesting(const char* n)
    {
        static const char* kWords[] = { "Item", "Equip", "Inventory", "Ware", "Dye", "Dresser", "Housing",
                                        "Storage", "Wardrobe", "Slot", "Wear", "Costume", "Outfit" };
        for (const char* w : kWords) if (strstr(n, w)) return true;
        return false;
    }

    static void RememberId(const char* n, uint16_t id)
    {
        std::string cls(n);
        const size_t at = cls.find('@');
        if (at != std::string::npos) cls.resize(at);
        AcquireSRWLockExclusive(&g_idLock);
        if (!g_ids) g_ids = new std::map<std::string, uint16_t>();
        (*g_ids)[cls] = id;
        ReleaseSRWLockExclusive(&g_idLock);
    }

    // Once, on the game thread: every event class the game knows under its own
    // mask, so the item-moving and equipping events can be named for round 2.
    static void DumpDescriptors()
    {
        const game::Fns& f = game::F();
        if (!f.descLookup || !f.tlsInit || !f.descMask) return;
        __try { reinterpret_cast<FnTlsInit>(f.tlsInit)(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { LOG_ERR("[desc] tls init faulted"); return; }
        uint32_t mask = 0;
        if (!mem::Read32(f.descMask, &mask)) return;
        int all = 0, shown = 0;
        for (uint32_t id = 1; id <= 0x1FFF; ++id)
        {
            void* d = LookupDesc(f.descLookup, id, mask);
            if (!d) continue;
            ++all;
            const char* n = mem::RttiShort(reinterpret_cast<uintptr_t>(d));
            if (n) RememberId(n, static_cast<uint16_t>(id));
            if (n && Interesting(n)) ++shown;
        }
        LOG("[desc] mask 0x%08X: %d descriptors, %d item-related", mask, all, shown);
    }

    static void Pump()
    {
        InterlockedIncrement(&g_pumpTicks);
        InterlockedExchange(&g_gameTid, static_cast<LONG>(GetCurrentThreadId()));
        static bool s_dumped = false;
        if (!s_dumped) { s_dumped = true; DumpDescriptors(); }
        if (!t_selfSend) { ++t_selfSend; dye::actions::Drain(); --t_selfSend; }
        static DWORD s_last = 0;
        const DWORD now = GetTickCount();
        if (now - s_last > 1000)
        {
            s_last = now;
            if (const uintptr_t a = game::ControlledActor()) game::RefreshHolder(a);
        }
    }

    static uint64_t hkMove(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8)
    {
        const uint64_t r = oMove(a1, a2, a3, a4, a5, a6, a7, a8);
        Pump();
        return r;
    }

    // ----------------------------------------------------------- event spy ----
    // Watches every event the game raises and writes the item-related ones with
    // their payload, so the swap can send exactly what the game sends.
    typedef void (*FnEnqueue)(void* q, void* ev, void* desc, uint64_t z);
    static FnEnqueue oEnq = nullptr;
    static volatile LONG g_spyLines = 0;
    static SRWLOCK g_seenLock = SRWLOCK_INIT;
    static std::set<std::string>* g_seenOther = nullptr;

    static void SpyEvent(uintptr_t ev, uintptr_t desc)
    {
        if (g_spyLines > 4000) return;
        const char* n = mem::RttiShort(desc);
        if (!n) return;
        const bool want = Interesting(n);
        if (!want)
        {
            AcquireSRWLockExclusive(&g_seenLock);
            if (!g_seenOther) g_seenOther = new std::set<std::string>();
            const bool fresh = g_seenOther->insert(n).second;
            ReleaseSRWLockExclusive(&g_seenLock);
            (void)fresh;
            return;
        }
        uint16_t size = 0; uintptr_t buf = 0;
        mem::Read16(ev + sig::kOff_Ev_Size, &size);
        mem::ReadPtr(ev + sig::kOff_Ev_Buffer, &buf);
        char hex[3 * 256 + 8] = "";
        int w = 0;
        const unsigned shown = size < 256 ? size : 256;
        for (unsigned i = 0; buf && i < shown; ++i)
        {
            uint8_t b = 0;
            if (!mem::Read8(buf + i, &b)) { w += snprintf(hex + w, sizeof hex - w, " ??"); break; }
            w += snprintf(hex + w, sizeof hex - w, i ? " %02X" : "%02X", b);
        }
        uint32_t player = 0; mem::Read32(ev + sig::kOff_Ev_Player, &player);
        InterlockedIncrement(&g_spyLines);
        LOG("[spy] %s size %u from %08X tid %lu dye %s: %s%s", n, size, player, GetCurrentThreadId(),
            DyeScreenOpen() ? "open" : "closed", hex, size > shown ? " ..." : "");
    }

    static void LearnRoute(uintptr_t ev)
    {
        uint32_t who = 0, rt = 0;
        if (!mem::Read32(ev + sig::kOff_Ev_Player, &who) || !mem::Read32(ev + sig::kOff_Ev_Route, &rt)) return;
        if ((who >> 24) != 0xA0 || !rt) return;
        const LONG was = InterlockedExchange(&g_route, static_cast<LONG>(rt));
        InterlockedExchange(&g_player, static_cast<LONG>(who));
        if (!InterlockedExchange(&g_routeKnown, 1) || was != static_cast<LONG>(rt))
            LOG("[send] events from %08X travel on route %08X", who, rt);
    }

    static void hkEnqueue(void* q, void* ev, void* desc, uint64_t z)
    {
        if (!t_selfSend)
        {
            __try { SpyEvent(reinterpret_cast<uintptr_t>(ev), reinterpret_cast<uintptr_t>(desc)); LearnRoute(reinterpret_cast<uintptr_t>(ev)); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        oEnq(q, ev, desc, z);
        // In menus the movement tick can go quiet; the game thread still
        // raises events, so pending actions are sent from here too.
        if (!t_selfSend && static_cast<DWORD>(InterlockedCompareExchange(&g_gameTid, 0, 0)) == GetCurrentThreadId())
        { ++t_selfSend; dye::actions::Drain(); --t_selfSend; }
    }

    // ------------------------------------------------------- request spy ----
    // The game's own requests (equip, move between inventories, dye) never pass
    // the event queue: the internal server runs each one through the request
    // class's handler, vtable slot 2, as handler(this, &result, event). The
    // event's payload is at [event+0x18]: u16 id, FF, u16 length, then the body.
    // Watched only; every call goes on to the game unchanged.
    struct ReqHook { const char* cls; uintptr_t at; Fn8 orig; volatile LONG seen; };
    static ReqHook g_req[] = {
        { "TrocTrEquipItemReq" }, { "TrocTrUnequipItemReq" }, { "TrocTrMoveItemInventoryToOtherInventoryReq" },
        { "TrocTrRequestMoveItemInternalReq" }, { "TrocTrDyeItemListByEquipSlotReq" }, { "TrocTrDyeItemByInventoryReq" },
        { "TrocTrSwapEquipSlotToOtherSlotReq" }, { "TrocTrAutoEquipItemReq" }, { "TrocTrRemoveItemToInventoryReq" },
        // Client-side replies: when does the client hear about the equip, relative to the dye screen?
        { "TrocTrEquipItemAck" }, { "TrocTrEquipItemOnlyAck" }, { "TrocTrFailEquipItemAck" }, { "TrocTrAddVisualEquipItemAck" },
        { "TrocTrRequestDelayedEquipmentInOutAck" }, { "TrocTrSetDyeItemDataToDyeViewAck" }, { "TrocTrDyeItemListByEquipSlotAck" },
        // Test 22: horse and A.T.A.G. gear, recorded so their swaps can be built.
        { "TrocTrMercenaryEquipItemReq" }, { "TrocTrMercenaryUnequipItemReq" }, { "TrocTrDyeMercenaryItemReq" },
        { "TrocTrMoveItemInventoryToVehicleReq" }, { "TrocTrMoveItemVehicleToInventoryReq" }, { "TrocTrTransferMercenaryItemReq" },
    };
    static constexpr int kReqN = sizeof g_req / sizeof g_req[0];

    static void LogReq(int i, uint64_t ev, const char* when, uint64_t result)
    {
        uintptr_t buf = 0; uint16_t total = 0, len = 0;
        mem::ReadPtr(static_cast<uintptr_t>(ev) + 0x18, &buf);
        mem::Read16(static_cast<uintptr_t>(ev) + 0x10, &total);
        if (buf) mem::Read16(buf + 3, &len);
        if (when[0] == 'o')
        {
            uint32_t r = 0; mem::Read32(static_cast<uintptr_t>(result), &r);
            LOG("[req] %s answered 0x%08X", g_req[i].cls, r);
            return;
        }
        static char hex[3 * 2100 + 8];
        int w = 0;
        const unsigned shown = len < 2048 ? len : 2048;
        for (unsigned k = 0; buf && k < shown; ++k)
        {
            uint8_t b = 0;
            if (!mem::Read8(buf + 5 + k, &b)) { w += snprintf(hex + w, sizeof hex - w, " ??"); break; }
            w += snprintf(hex + w, sizeof hex - w, k ? " %02X" : "%02X", b);
        }
        if (!buf) snprintf(hex, sizeof hex, "(no buffer)");
        InterlockedIncrement(&g_spyLines);
        LOG("[req] %s len %u (event %u) tid %lu dye %s: %s", g_req[i].cls, len, total, GetCurrentThreadId(),
            DyeScreenOpen() ? "open" : "closed", hex);
    }

    template <int I>
    static uint64_t hkReq(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8)
    {
        const bool log = g_spyLines < 6000;
        if (log) { __try { LogReq(I, a3, "in", 0); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
        const uint64_t r = g_req[I].orig(a1, a2, a3, a4, a5, a6, a7, a8);
        InterlockedIncrement(&g_req[I].seen);
        if (log) { __try { LogReq(I, a3, "out", a2); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
        return r;
    }
    long DyeAcks()   // TrocTrDyeItemListByEquipSlotAck, plus TrocTrDyeMercenaryItemReq handled by the game
    { return InterlockedCompareExchange(&g_req[15].seen, 0, 0) + InterlockedCompareExchange(&g_req[18].seen, 0, 0); }
    long FailEquipAcks() { return InterlockedCompareExchange(&g_req[11].seen, 0, 0); }  // TrocTrFailEquipItemAck
    template <int I> static void* ReqDetour() { return reinterpret_cast<void*>(&hkReq<I>); }
    static void* const kReqDetours[] = {
        ReqDetour<0>(), ReqDetour<1>(), ReqDetour<2>(), ReqDetour<3>(), ReqDetour<4>(),
        ReqDetour<5>(), ReqDetour<6>(), ReqDetour<7>(), ReqDetour<8>(), ReqDetour<9>(),
        ReqDetour<10>(), ReqDetour<11>(), ReqDetour<12>(), ReqDetour<13>(), ReqDetour<14>(), ReqDetour<15>(),
        ReqDetour<16>(), ReqDetour<17>(), ReqDetour<18>(), ReqDetour<19>(), ReqDetour<20>(), ReqDetour<21>(),
    };
    static_assert(sizeof kReqDetours / sizeof kReqDetours[0] == kReqN, "one detour per request");

    static void InstallRequestSpy()
    {
        int ok = 0;
        for (int i = 0; i < kReqN; ++i)
        {
            char rtti[160];
            snprintf(rtti, sizeof rtti, ".?AV%s@pa@@", g_req[i].cls);
            uintptr_t vt[4] = {};
            if (mem::FindVtablesByName(rtti, vt, 4) != 1) { LOG_ERR("[req] %s: vtable not found", g_req[i].cls); continue; }
            uintptr_t h = 0;
            if (!mem::ReadPtr(vt[0] + 0x10, &h) || !mem::InImage(h)) { LOG_ERR("[req] %s: no handler", g_req[i].cls); continue; }
            g_req[i].at = h;
            uint8_t first = 0;
            mem::Read8(h, &first);
            char why[96] = "";
            bool done;
            if (first == 0xE9)   // the game's protection wraps some handlers in a jump; chain through it
                done = farhook::InstallOverJump(g_req[i].cls, h, kReqDetours[i], reinterpret_cast<void**>(&g_req[i].orig), why, sizeof why);
            else
                done = Hook(g_req[i].cls, h, kReqDetours[i], reinterpret_cast<void**>(&g_req[i].orig));
            if (done) ++ok;
            else LOG_ERR("[req] %s at +0x%llX not hooked %s", g_req[i].cls, static_cast<unsigned long long>(mem::Rva(h)), why);
            if (done && first == 0xE9) LOG("[req] %s hooked at +0x%llX through its protection jump", g_req[i].cls, static_cast<unsigned long long>(mem::Rva(h)));
        }
        LOG("[req] %d of %d request handlers watched", ok, kReqN);
    }

    // ---------------------------------------------------- move core result ----
    // The request handler always answers 0; the real outcome of a move (for
    // example "the wardrobe only takes items while you stand at it") is what
    // the move routine writes to its result, its second argument. Ten args.
    typedef uint64_t (*Fn12)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    static Fn12 oMoveCore = nullptr;
    static constexpr uintptr_t kRvaMoveCore = 0x2E72810;
    static constexpr const char* kMoveCorePrologue = "48 89 5C 24 10 66 44 89 4C 24 20 48 89 4C 24 08";
    static uint64_t hkMoveCore(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8, uint64_t a9, uint64_t a10, uint64_t a11, uint64_t a12)
    {
        const uint64_t r = oMoveCore(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
        uint32_t res = 0;
        mem::Read32(static_cast<uintptr_t>(a2), &res);
        LOG("[req] move routine result 0x%08X (item %u)", res, static_cast<uint32_t>(a3));
        dye::actions::OnMoveResult(res);
        return r;
    }

    // ------------------------------------------------- dye screen per frame ----
    // While the dye screen is open the movement tick stops, so nothing would send
    // the swap's requests. The screen's own update (slot 36) calls this dye-only
    // function first thing every frame; it is the pump while the screen is up.
    static Fn8 oDyeTick = nullptr;
    static void RunPendingRefresh();
    static constexpr uintptr_t kRvaDyeTick = 0x1164C20;
    static constexpr const char* kDyeTickPrologue = "48 89 5C 24 10 48 89 6C 24 18 56 57 41 54 41 56";
    static uint64_t hkDyeTick(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8)
    {
        const uint64_t r = oDyeTick(a1, a2, a3, a4, a5, a6, a7, a8);
        const DWORD me = GetCurrentThreadId();
        const DWORD game = static_cast<DWORD>(InterlockedCompareExchange(&g_gameTid, 0, 0));
        static bool s_said = false;
        if (!s_said) { s_said = true; LOG("[dye] dye screen updates on thread %lu; the game tick ran on %lu", me, game); }
        if (game == me && !t_selfSend) { ++t_selfSend; dye::actions::Drain(); RunPendingRefresh(); --t_selfSend; }
        return r;
    }

    // ----------------------------------------------------------- dye screen ----
    // UIGamePlayControlRootDyeing: vtable slot 17 runs when the screen opens
    // (refreshes its lists), slot 20 when it closes (resets the dye camera).
    // Found by RTTI name; each entry must start with the prologue seen on
    // exe 1.0.0.3663 or it is left alone.
    static constexpr const char* kRttiDye = ".?AVUIGamePlayControlRootDyeing@uiCommonScript@pa@@";
    static constexpr int kSlotShow = 17, kSlotHide = 20;
    static constexpr const char* kShowPrologue = "48 89 5C 24 08 48 89 6C 24 10 56 57 41 56 48 83 EC 50 48 8B F1";
    static constexpr const char* kHidePrologue = "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B F9 E8";

    typedef uint64_t (*Fn1)(uint64_t);
    static Fn8 oShow = nullptr;
    static Fn1 oHide = nullptr;
    static volatile LONG64 g_dyeThis = 0;
    static volatile LONG g_dyeOpen = 0;
    static volatile LONG g_closedAt = 0;
    static bool g_dyeHooks = false;

    static uint64_t hkShow(uint64_t self, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8)
    {
        InterlockedExchange64(&g_dyeThis, static_cast<LONG64>(self));
        InterlockedExchange(&g_dyeOpen, 1);
        LOG("[dye] dye screen opened (%llX, thread %lu)", static_cast<unsigned long long>(self), GetCurrentThreadId());
        if (const uintptr_t a = game::ControlledActor()) game::RefreshHolder(a);
        dye::actions::CancelRestore();
        return oShow(self, a2, a3, a4, a5, a6, a7, a8);
    }

    static uint64_t hkHide(uint64_t self)
    {
        if (static_cast<uint64_t>(InterlockedCompareExchange64(&g_dyeThis, 0, 0)) == self)
        {
            InterlockedExchange(&g_dyeOpen, 0);
            LOG("[dye] dye screen closed");
            InterlockedExchange(&g_closedAt, static_cast<LONG>(GetTickCount()));
            dye::actions::RestoreSoon(kGraceMs);   // reopening within that time keeps the item on
        }
        return oHide(self);
    }

    // ------------------------------------------------- horse and A.T.A.G. ----
    // Test 23. Which horse / A.T.A.G. the dye screen shows is decided in
    // SetDisplayMode (+0x115B810, mode 1 horse, 2 A.T.A.G.) by the mercenary
    // lookups +0x2387AF0 and +0x2387A30, which return a mercenary entry:
    // +0x24 u16 character, +0x28 u64 mercenary key (what
    // TrocTrMercenaryEquipItemReq carries), +0xEE nonzero = not usable.
    // Watching those calls gives the same horse the dyer shows.
    static Fn8 oSetMode = nullptr, oMercA = nullptr, oMercB = nullptr;
    static thread_local int t_dispMode = -1;
    static volatile LONG64 g_mercKey[3] = { 0, 0, 0 };
    static void NoteMerc(uint64_t entry)
    {
        const int m = t_dispMode;
        if (m != 1 && m != 2 || !entry) return;
        uint64_t key = 0; uint16_t ch = 0xFFFF; uint8_t bad = 1;
        if (!mem::Read64(static_cast<uintptr_t>(entry) + 0x28, &key) || !mem::Read16(static_cast<uintptr_t>(entry) + 0x24, &ch) ||
            !mem::Read8(static_cast<uintptr_t>(entry) + 0xEE, &bad) || key == ~0ull || ch == 0xFFFF || bad) return;
        if (static_cast<uint64_t>(InterlockedExchange64(&g_mercKey[m], static_cast<LONG64>(key))) != key)
            LOG("[merc] the dyer's %s is mercenary %llu (character %u)", m == 1 ? "horse" : "A.T.A.G.", static_cast<unsigned long long>(key), ch);
    }
    static uint64_t hkSetMode(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8)
    {
        const int was = t_dispMode;
        t_dispMode = static_cast<uint8_t>(a2);
        const uint64_t r = oSetMode(a1, a2, a3, a4, a5, a6, a7, a8);
        t_dispMode = was;
        return r;
    }
    static uint64_t hkMercA(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8)
    {
        const uint64_t r = oMercA(a1, a2, a3, a4, a5, a6, a7, a8);
        NoteMerc(r);
        return r;
    }
    static uint64_t hkMercB(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8)
    {
        const uint64_t r = oMercB(a1, a2, a3, a4, a5, a6, a7, a8);
        NoteMerc(r);
        return r;
    }
    static void InstallMercWatch()
    {
        const uintptr_t base = mem::Game().base;
        if (!mem::MatchAt(base + 0x115B810, "48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24") ||
            !mem::MatchAt(base + 0x2387AF0, "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 56 41 57 48 83 EC 20") ||
            !mem::MatchAt(base + 0x2387A30, "48 89 5C 24 18 48 89 6C 24 20 66 89 54 24 10 56 57 41 56 48 83 EC 20"))
        { LOG_ERR("[merc] the dyer's horse lookup is not where exe 1.0.0.3663 has it; horse and A.T.A.G. gear cannot be swapped"); return; }
        Hook("dye screen tab view", base + 0x115B810, reinterpret_cast<void*>(&hkSetMode), reinterpret_cast<void**>(&oSetMode));
        Hook("mercenary lookup A", base + 0x2387AF0, reinterpret_cast<void*>(&hkMercA), reinterpret_cast<void**>(&oMercA));
        Hook("mercenary lookup B", base + 0x2387A30, reinterpret_cast<void*>(&hkMercB), reinterpret_cast<void**>(&oMercB));
    }
    uint64_t MercKey(int mode) { return mode == 1 || mode == 2 ? static_cast<uint64_t>(InterlockedCompareExchange64(&g_mercKey[mode], 0, 0)) : 0; }

    static void InstallDyeScreen()
    {
        uintptr_t vt[4] = {};
        const int nv = mem::FindVtablesByName(kRttiDye, vt, 4);
        if (nv != 1) { LOG_ERR("[dye] %d vtables named UIGamePlayControlRootDyeing; the panel will not follow the dye screen", nv); return; }
        uintptr_t show = 0, hide = 0;
        mem::ReadPtr(vt[0] + 8 * kSlotShow, &show);
        mem::ReadPtr(vt[0] + 8 * kSlotHide, &hide);
        LOG("[dye] dye controller vtable +0x%llX, show +0x%llX, hide +0x%llX", static_cast<unsigned long long>(mem::Rva(vt[0])),
            static_cast<unsigned long long>(mem::Rva(show)), static_cast<unsigned long long>(mem::Rva(hide)));
        if (!mem::InImage(show) || !mem::InImage(hide)) { LOG_ERR("[dye] slots do not point into the game"); return; }
        if (!mem::MatchAt(show, kShowPrologue) || !mem::MatchAt(hide, kHidePrologue))
        {
            LOG_ERR("[dye] the dye screen's show/hide code is not what this build expects (game updated, or another mod hooked it); not hooking it");
            return;
        }
        const bool a = Hook("dye screen show", show, reinterpret_cast<void*>(&hkShow), reinterpret_cast<void**>(&oShow));
        const bool b = Hook("dye screen hide", hide, reinterpret_cast<void*>(&hkHide), reinterpret_cast<void**>(&oHide));
        g_dyeHooks = a && b;
    }

    void Install()
    {
        const game::Fns& f = game::F();
        Hook("movement tick", f.moveUpdate, reinterpret_cast<void*>(&hkMove), reinterpret_cast<void**>(&oMove));
        Hook("event queue (watch only)", f.enqueue, reinterpret_cast<void*>(&hkEnqueue), reinterpret_cast<void**>(&oEnq));
        InstallDyeScreen();
        InstallRequestSpy();
        const uintptr_t base = mem::Game().base;
        if (mem::MatchAt(base + kRvaMoveCore, kMoveCorePrologue))
            Hook("move routine", base + kRvaMoveCore, reinterpret_cast<void*>(&hkMoveCore), reinterpret_cast<void**>(&oMoveCore));
        else LOG_ERR("[hook] move routine not where exe 1.0.0.3663 has it; move results unknown");
        if (mem::MatchAt(base + kRvaDyeTick, kDyeTickPrologue))
            Hook("dye screen update", base + kRvaDyeTick, reinterpret_cast<void*>(&hkDyeTick), reinterpret_cast<void**>(&oDyeTick));
        else LOG_ERR("[hook] dye screen update not where exe 1.0.0.3663 has it; swaps wait until the dye screen closes");
        InstallMercWatch();
    }

    void Remove()
    {
        farhook::RemoveAll();
        oMove = nullptr; oEnq = nullptr; oShow = nullptr; oHide = nullptr; oMoveCore = nullptr; oDyeTick = nullptr;
        g_dyeHooks = false;
    }

    uint16_t DescId(const char* cls)
    {
        AcquireSRWLockShared(&g_idLock);
        uint16_t id = 0;
        if (g_ids) { const auto it = g_ids->find(cls); if (it != g_ids->end()) id = it->second; }
        ReleaseSRWLockShared(&g_idLock);
        return id;
    }
    bool RouteKnown() { return InterlockedCompareExchange(&g_routeKnown, 0, 0) != 0; }
    uint32_t Route() { return static_cast<uint32_t>(InterlockedCompareExchange(&g_route, 0, 0)); }
    uint32_t Player() { return static_cast<uint32_t>(InterlockedCompareExchange(&g_player, 0, 0)); }
    bool OnGameThread() { return static_cast<DWORD>(InterlockedCompareExchange(&g_gameTid, 0, 0)) == GetCurrentThreadId(); }

    static volatile LONG g_refreshKind = 0, g_refreshAt = 0;
    void RequestRefresh(int kind, unsigned delayMs)
    {
        InterlockedExchange(&g_refreshAt, static_cast<LONG>(GetTickCount() + delayMs));
        InterlockedExchange(&g_refreshKind, kind);
    }

    // What RB / LB does: the tab bar tells the dye screen a tab was picked,
    // through its virtual OnTabbarEvent (slot 114, +0x115E430):
    //   (this, tabbar = [this+0x1E8], event 0 = picked, unused, tab index)
    // It clears the item list widgets and rebuilds the view for that tab.
    // Picking the horse tab and then the character tab again makes the screen
    // re-read the character's gear, which re-selecting the same tab does not.
    static constexpr int kSlotTabEvent = 114;
    static constexpr const char* kTabEventPrologue = "41 80 F8 01 0F 87";
    typedef void (*FnTabEvent)(uintptr_t self, uintptr_t tabbar, uint8_t ev, uint64_t unused, uint8_t index);
    static bool CallTab(FnTabEvent fn, uintptr_t self, uintptr_t bar, uint8_t index)
    {
        __try { fn(self, bar, 0, 0, index); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    // The dye screen's own "colors picked but not applied?" check (+0x115CC30),
    // the one behind its "discard changes?" question when changing tabs.
    static constexpr uintptr_t kRvaUnapplied = 0x115CC30;
    // Test 18: the same comparison as the game's check, done here with safe
    // reads. The game's function walks off a null pointer when a gear slot has
    // colors now but no saved entry (a freshly swapped-in piece), which the old
    // wrapper caught and took as "nothing unapplied": test 17 swapped away
    // with colors still pending.
    //   now:   [this+0x39C] entries, entry pointers at [this+0x3B0]
    //   saved: [this+0x3BC] entries, entry pointers at [this+0x3D0]
    //   entry: +4 i16 gear slot, +8 -> 16-byte dye records, +0x10 u32 count;
    //   a record's byte 6 is its dye slot, bytes 0..12 its color.
    static constexpr const char* kUnappliedPrologue = "40 55 57 8B A9 9C 03 00 00 48 8B F9";
    static bool Entries(uintptr_t self, unsigned countOff, unsigned arrOff, uintptr_t* out, uint32_t* n)
    {
        uint32_t c = 0; uintptr_t arr = 0;
        if (!mem::Read32(self + countOff, &c) || c > 64) return false;
        *n = c;
        if (!c) return true;
        if (!mem::ReadPtr(self + arrOff, &arr)) return false;
        for (uint32_t i = 0; i < c; ++i) if (!mem::ReadPtr(arr + 8 * i, &out[i])) return false;
        return true;
    }
    static int Unapplied(uintptr_t self, char* why, size_t whyLen)
    {
        if (!mem::MatchAt(mem::Game().base + kRvaUnapplied, kUnappliedPrologue))
        { snprintf(why, whyLen, "dye screen layout unknown"); return -1; }
        uintptr_t now[64], saved[64]; uint32_t nn = 0, ns = 0;
        if (!Entries(self, 0x39C, 0x3B0, now, &nn) || !Entries(self, 0x3BC, 0x3D0, saved, &ns))
        { snprintf(why, whyLen, "color tables unreadable"); return -1; }
        if (nn != ns) { snprintf(why, whyLen, "%u gear slots have colors now, %u saved", nn, ns); return 1; }
        for (uint32_t i = 0; i < nn; ++i)
        {
            int16_t slot = 0; uintptr_t recs = 0; uint32_t rn = 0;
            if (!mem::Read16(now[i] + 4, reinterpret_cast<uint16_t*>(&slot)) || !mem::Read32(now[i] + 0x10, &rn) || rn > 32)
            { snprintf(why, whyLen, "color table entry unreadable"); return -1; }
            uintptr_t match = 0;
            for (uint32_t j = 0; j < ns && !match; ++j)
            {
                int16_t s2 = 0;
                if (mem::Read16(saved[j] + 4, reinterpret_cast<uint16_t*>(&s2)) && s2 == slot) match = saved[j];
            }
            if (!rn) continue;
            if (!match) { snprintf(why, whyLen, "gear slot %d has colors but nothing saved", slot); return 1; }
            uintptr_t srecs = 0; uint32_t sn = 0;
            if (!mem::Read32(match + 0x10, &sn) || sn > 32 || (rn && !mem::ReadPtr(now[i] + 8, &recs)) || (sn && !mem::ReadPtr(match + 8, &srecs)))
            { snprintf(why, whyLen, "color records unreadable"); return -1; }
            if (sn < rn) { snprintf(why, whyLen, "gear slot %d: %u colors now, %u saved", slot, rn, sn); return 1; }
            uint8_t a[32][16], b[32][16];
            if (!mem::ReadBytes(recs, a, 16 * rn) || (sn && !mem::ReadBytes(srecs, b, 16 * sn)))
            { snprintf(why, whyLen, "color records unreadable"); return -1; }
            for (uint32_t x = 0; x < sn; ++x)
                for (uint32_t y = 0; y < rn; ++y)
                    if (b[x][6] == a[y][6] && memcmp(b[x], a[y], 13) != 0)
                    { snprintf(why, whyLen, "gear slot %d dye slot %u changed", slot, b[x][6]); return 1; }
        }
        snprintf(why, whyLen, "%u gear slots, all saved", nn);
        return 0;
    }
    // Test 19: what confirming the dyer's "Dye" popup runs (popup handler
    // +0x115E340, popup [this+0x450], button 0): state [this+0x41C] = 6, lock
    // the screen's buttons (+0x1161810), then +0x1162540(this, &result), which
    // builds TrocTrDyeItemListByEquipSlotReq from the picked colors at
    // [this+0x398] and sends it. The game charges and saves exactly as when
    // the player confirms; on failure it shows its own error message.
    //
    // Test 20 showed +0x1162540 also ends the dyer: after sending the request it
    // sends the UI event "Close" through +0x764F00(?, this+0x128, ?, "Close").
    // Test 21 runs only +0x1162540 (no state change, no button lock) and, for
    // that one call on this thread, drops the "Close" event, so the colors are
    // saved and the dye screen stays open. Done = the game's
    // TrocTrDyeItemListByEquipSlotAck arriving (DyeAcks()).
    static constexpr uintptr_t kRvaDyeApply = 0x1162540, kRvaUiEvent = 0x764F00;
    static Fn8 oUiEvent = nullptr;
    static thread_local int t_blockClose = 0;
    static volatile LONG g_closeBlocked = 0;
    static bool IsClose(uint64_t p)
    {
        char s[8] = {};
        return p && mem::ReadBytes(static_cast<uintptr_t>(p), s, 6) && memcmp(s, "Close", 6) == 0;
    }
    static uint64_t hkUiEvent(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8)
    {
        if (t_blockClose && IsClose(a4)) { InterlockedIncrement(&g_closeBlocked); return 0; }
        return oUiEvent(a1, a2, a3, a4, a5, a6, a7, a8);
    }
    static bool CallApply(uintptr_t applyFn, uintptr_t self, uint32_t* result)
    {
        __try { reinterpret_cast<void (*)(uintptr_t, uint32_t*)>(applyFn)(self, result); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    int ApplyDyeNow()
    {
        const uintptr_t self = DyeScreen();
        if (!self) return -1;
        const uintptr_t base = mem::Game().base;
        if (!mem::MatchAt(base + kRvaDyeApply, "48 89 5C 24 18 48 89 54 24 10 55 56 57 41 54 41 55 41 56 41 57") ||
            !mem::MatchAt(base + 0x11628E4, "4C 8D 0D") || mem::RipAt(base + 0x11628E4, 7) == 0 || !IsClose(mem::RipAt(base + 0x11628E4, 7)))
        { LOG_ERR("[dye] apply: the dyer's code is not where exe 1.0.0.3663 has it"); return -1; }
        if (!oUiEvent)
        {
            if (!mem::MatchAt(base + kRvaUiEvent, "48 89 5C 24 10 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57") ||
                !Hook("UI event (to keep the dyer open)", base + kRvaUiEvent, reinterpret_cast<void*>(&hkUiEvent), reinterpret_cast<void**>(&oUiEvent)))
            { LOG_ERR("[dye] apply: cannot keep the dyer open, so not applying"); oUiEvent = nullptr; return -1; }
        }
        // Any tab: it is the same function the Dye button runs there. The
        // horse / A.T.A.G. tabs may answer with the mercenary dye request
        // instead, which DyeAcks() counts too.
        uint8_t tab = 0xFF;
        mem::Read8(self + 0x443, &tab);
        LOG("[dye] apply on dyer tab %u", tab);
        const LONG closedBefore = InterlockedCompareExchange(&g_closeBlocked, 0, 0);
        uint32_t result = 0xFFFFFFFF;
        ++t_blockClose;
        const bool ok = CallApply(base + kRvaDyeApply, self, &result);
        --t_blockClose;
        const bool blocked = InterlockedCompareExchange(&g_closeBlocked, 0, 0) != closedBefore;
        LOG("[dye] applied the picked colors: %s, result 0x%X, dyer's Close %s", ok ? "sent" : "FAULTED", result,
            blocked ? "held back" : "not sent");
        if (!ok) return -1;
        return static_cast<int>(result);
    }

    int UnappliedDye()
    {
        const uintptr_t self = DyeScreen();
        if (!self) return 0;
        char why[96] = "";
        const int r = Unapplied(self, why, sizeof why);
        static int s_last = 99; static char s_why[96] = "";
        if (r != s_last || strcmp(why, s_why) != 0)
        {
            s_last = r; snprintf(s_why, sizeof s_why, "%s", why);
            LOG("[dye] unapplied colors: %s (%s)", r > 0 ? "yes" : r == 0 ? "no" : "cannot tell", why);
        }
        return r;
    }
    bool HasUnappliedDye() { return UnappliedDye() != 0; }   // when unsure, keep the colors safe and do not swap

    // Test 17. The left list is rebuilt by the dye screen's "preview character
    // loaded" step (+0x1167D30), but only when the shown tab ([this+0x443])
    // differs from the tab the list was last built for ([this+0x445]). Test 11
    // reloaded the preview (horse, then character, in one frame) and the model
    // showed the new gear, but the list stayed old because both bytes were 0.
    // So: mark the list as built for no tab, then reload the preview the same
    // way. Nothing is pressed and the tab bar never moves.
    static volatile LONG g_rebuildWatchAt = 0;
    // `via` picks the tab the preview passes through (1 horse, 2 A.T.A.G.), which
    // also makes the dyer look up that mercenary (see NoteMerc).
    static bool RefreshInPlace(int via = -1)
    {
        const uintptr_t self = DyeScreen();
        if (!self) return false;
        uintptr_t vt = 0, fn = 0, bar = 0;
        uint8_t mode = 0xFF, built = 0xFF;
        if (!mem::ReadPtr(self, &vt) || !mem::ReadPtr(vt + 8 * kSlotTabEvent, &fn) || !mem::InImage(fn) ||
            !mem::MatchAt(fn, kTabEventPrologue) || !mem::ReadPtr(self + 0x1E8, &bar) || !mem::Read8(self + 0x443, &mode) || mode > 2 ||
            !mem::Read8(self + 0x445, &built))
        { LOG_ERR("[dye] in-place refresh: dye screen not as on exe 1.0.0.3663"); return false; }
        const uint8_t other = via >= 0 && via <= 2 && via != mode ? static_cast<uint8_t>(via) : (mode == 0 ? 1 : 0);
        *reinterpret_cast<volatile uint8_t*>(self + 0x445) = 0xFE;
        const bool a = CallTab(reinterpret_cast<FnTabEvent>(fn), self, bar, other);
        const bool b = a && CallTab(reinterpret_cast<FnTabEvent>(fn), self, bar, mode);
        InterlockedExchange(&g_rebuildWatchAt, static_cast<LONG>(GetTickCount()));
        LOG("[dye] in-place refresh on tab %u (list was built for %u): preview reload %s", mode, built, b ? "asked" : "FAULTED");
        return b;
    }
    uint64_t LearnMercKey(int mode)
    {
        if (const uint64_t k = MercKey(mode)) return k;
        if (!DyeScreen() || !oSetMode) return 0;
        RefreshInPlace(mode);   // the dyer looks up its horse / A.T.A.G. on the way through
        const uint64_t k = MercKey(mode);
        if (!k) LOG_ERR("[merc] the dyer did not show a %s", mode == 1 ? "horse" : "A.T.A.G.");
        return k;
    }
    static void WatchRebuild()
    {
        const LONG at = InterlockedCompareExchange(&g_rebuildWatchAt, 0, 0);
        if (!at) return;
        const uintptr_t self = DyeScreen();
        uint8_t mode = 0xFF, built = 0xFF;
        if (!self || !mem::Read8(self + 0x443, &mode) || !mem::Read8(self + 0x445, &built))
        { InterlockedExchange(&g_rebuildWatchAt, 0); return; }
        const DWORD ms = GetTickCount() - static_cast<DWORD>(at);
        if (built == mode) { InterlockedExchange(&g_rebuildWatchAt, 0); LOG("[dye] the dyer's list was rebuilt %lu ms after the refresh", ms); }
        else if (ms > 5000) { InterlockedExchange(&g_rebuildWatchAt, 0); LOG_ERR("[dye] the dyer's list was not rebuilt within 5 s (tab %u, built %u)", mode, built); }
    }

    bool TabBounce()
    {
        if (RefreshInPlace()) return true;
        const uintptr_t self = DyeScreen();
        if (!self) return false;
        uintptr_t vt = 0, fn = 0, bar = 0;
        uint8_t mode = 0xFF;
        if (!mem::ReadPtr(self, &vt) || !mem::ReadPtr(vt + 8 * kSlotTabEvent, &fn) || !mem::InImage(fn) ||
            !mem::MatchAt(fn, kTabEventPrologue) || !mem::ReadPtr(self + 0x1E8, &bar) || !mem::Read8(self + 0x443, &mode) || mode > 2)
        { LOG_ERR("[dye] tab switch not as on exe 1.0.0.3663; not refreshing"); return false; }
        const uint8_t other = mode == 0 ? 1 : 0;
        const bool a = CallTab(reinterpret_cast<FnTabEvent>(fn), self, bar, other);
        const bool b = a && CallTab(reinterpret_cast<FnTabEvent>(fn), self, bar, mode);
        LOG("[dye] tab bounce %u -> %u -> %u: %s", mode, other, mode, b ? "ok" : "FAULTED");
        return b;
    }

    static void RunPendingRefresh()
    {
        WatchRebuild();
        const LONG kind = InterlockedCompareExchange(&g_refreshKind, 0, 0);
        if (!kind || static_cast<LONG>(GetTickCount() - static_cast<DWORD>(InterlockedCompareExchange(&g_refreshAt, 0, 0))) < 0) return;
        InterlockedExchange(&g_refreshKind, 0);
        if (kind == 5) TabBounce();
    }

    bool DyeHooksOk() { return g_dyeHooks; }
    unsigned long SinceDyeClosed()
    {
        const LONG at = InterlockedCompareExchange(&g_closedAt, 0, 0);
        return at ? GetTickCount() - static_cast<DWORD>(at) : 0xFFFFFFFFul;
    }
    bool DyeScreenOpen() { return InterlockedCompareExchange(&g_dyeOpen, 0, 0) != 0; }
    uintptr_t DyeScreen() { return DyeScreenOpen() ? static_cast<uintptr_t>(InterlockedCompareExchange64(&g_dyeThis, 0, 0)) : 0; }

    // The dye screen's own "show the A.T.A.G. tab" flag ([this+0x442], set at open only when the
    // player has an A.T.A.G. in their mercenary list). Remembered after the screen closes.
    int AtagTab()
    {
        static volatile LONG s_last = -1;
        const uintptr_t self = DyeScreen();
        uint8_t v = 0;
        if (self && mem::Read8(self + 0x442, &v))
        {
            const LONG now = v ? 1 : 0;
            if (InterlockedExchange(&s_last, now) != now) LOG("[dye] the dyer %s an A.T.A.G. tab", now ? "has" : "has no");
        }
        return InterlockedCompareExchange(&s_last, 0, 0);
    }
    unsigned long PumpTicks() { return static_cast<unsigned long>(g_pumpTicks); }
    long SpyLines() { return g_spyLines; }
}
