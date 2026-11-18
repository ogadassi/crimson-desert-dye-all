#include "actions.h"

#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>

#include "hooks.h"
#include "../core/log.h"
#include "../game/game.h"
#include "../game/mem.h"
#include "../game/signatures.h"

namespace dye::actions
{
    using namespace ml;

    static constexpr uint16_t kBag = 1, kChest = 7, kWarehouse = 8, kWardrobe = 14;
    static constexpr DWORD kStepTimeout = 6000;

    int GearSlotFor(uint32_t equipType)
    {
        switch (equipType)
        {
        case 1963713749u: return 3;    // Helm
        case 2667368044u: return 4;    // Upperbody
        case 3628286577u: return 5;    // Hand
        case 3438125342u: return 6;    // Foot
        case 1241706053u: return 16;   // Cloak
        case 503331443u:  return 17;   // Glass
        // Weapons and shields (equipslotinfo, test 22). Each type goes in the same
        // slot for every character that can use it (Kliff, Damiane, Oongka); a
        // character that cannot use it gets it refused by the game, and the mod
        // puts it back.
        case 1584411264u: return 0;    // OneHandSword
        case 2594511993u: return 0;    // OneHandAxe
        case 2327795645u: return 0;    // OneHandMace
        case 1187101662u: return 0;    // OneHandRapier (Damiane)
        case 585399773u:  return 0;    // TwoHandAxe (Oongka)
        case 257028056u:  return 1;    // OneHandShield
        case 1334259611u: return 1;    // OneHandTowerShield
        case 517658843u:  return 1;    // OneHandShieldRight (Damiane)
        case 249090674u:  return 2;    // OneHandBow
        case 2276022818u: return 2;    // OneHandMusket
        case 3956795411u: return 2;    // OneHandShotgun
        case 31650711u:   return 2;    // OneHandPistol (Damiane)
        case 190908618u:  return 2;    // OneHandCannon (Oongka)
        case 1921528741u: return 12;   // OneHandDagger
        case 1086980073u: return 13;   // TwoHandSword
        case 643712710u:  return 13;   // TwoHandGiantSword
        case 2914941932u: return 13;   // TwoHandSpear
        case 3456689820u: return 13;   // TwoHandHalberd
        case 975012496u:  return 13;   // TwoHandCannon
        case 3150053877u: return 13;   // TwoHandWarHammer (Oongka)
        case 604374103u:  return 13;   // TwoHandHammer (Oongka)
        // Horse (Equip_Horse) and A.T.A.G. (Equip_ATAG) gear, worn by the mercenary.
        case 2841735235u: return 0;    // HorseHelm
        case 3651061774u: return 1;    // HorseArmor
        case 4283032781u: return 2;    // HorseSaddle
        case 189089223u:  return 3;    // HorseStirrup
        case 786544299u:  return 0;    // RobotFist
        case 1066416539u: return 0;    // RobotGatling
        case 3370526075u: return 0;    // RobotFlameThrower
        case 261596318u:  return 0;    // RobotWelding
        case 3392409000u: return 1;    // RobotCannon
        case 3452336502u: return 1;    // RobotLaser
        case 3186226560u: return 1;    // RobotTongs
        case 1961773671u: return 2;    // RobotBody
        case 3659357466u: return 3;    // RobotBackPack
        case 316730227u:  return 4;    // RobotFoot
        default:          return -1;   // crossbows (no slot on Kliff, Damiane or Oongka)
        }
    }

    // Who wears it: 0 the character, 1 the horse, 2 the A.T.A.G. (the dyer's tab numbers).
    int TargetFor(uint32_t equipType)
    {
        switch (equipType)
        {
        case 2841735235u: case 3651061774u: case 4283032781u: case 189089223u: return 1;
        case 786544299u: case 1066416539u: case 3370526075u: case 261596318u: case 3392409000u:
        case 3452336502u: case 3186226560u: case 1961773671u: case 3659357466u: case 316730227u: return 2;
        default: return 0;
        }
    }
    static const char* TargetName(int t) { return t == 1 ? "your horse" : t == 2 ? "the A.T.A.G." : "you"; }
    bool NoAtag(uint32_t equipType) { return TargetFor(equipType) == 2 && hooks::AtagTab() == 0; }

    // ------------------------------------------------------------ state ----
    // WearDirect: equip straight from storage (no bag slot needed), tried when the bag is full.
    // Park / Unpark: when that is refused, one bag item waits in the private chest while
    // dyeing, which frees the bag slot a swap passes through, and comes back afterwards.
    enum class Step { Idle, TakeOut, Wear, Unwear, PutAway, WearDirect, Park, Unpark };
    enum class Stage { None, Worn, InBag };

    struct Want { uint16_t row, level, fromType, fromSlot; };

    static std::mutex g_lock;
    static std::string g_status;
    static bool g_wantTry = false;
    static Want g_want{};
    static bool g_wantRestore = false;
    static DWORD g_restoreAt = 0;

    // Owned by the game thread (Drain); copied out under g_lock for the panel.
    static Step g_step = Step::Idle;
    static DWORD g_stepAt = 0;
    static DWORD g_applyAt = 0;   // when the mod applied picked colors before a swap; 0 when not waiting
    static long g_ackBase = 0;    // the game's dye-reply count before that
    static long g_failBase = 0;   // the game's "equip refused" count before the last equip
    static bool g_colorsSaved = false;   // colors confirmed; do not ask again until the next item is out
    static Stage g_stage = Stage::None;
    static const db::Item* g_item = nullptr;
    static uint16_t g_level = 0, g_fromType = 0;
    static int g_gear = -1;
    static int g_itemBagSlot = -1;
    static bool g_haveOld = false;
    static uint16_t g_oldRow = 0, g_oldLevel = 0;
    static uint16_t g_oldInv = 1, g_oldSlot = 0;          // where the piece worn before waits (bag, or storage after a direct equip)
    static uint16_t g_srcSlot = 0;                         // storage slot the current item came from
    static uint16_t g_restoreInv = 1, g_restoreSlot = 0;   // where the old piece was taken from when putting back
    static bool g_directTried = false;
    static int g_putBackRow = -1, g_putBackLevel = -1;     // the piece the last put-back put on again                     // this pick already tried equipping straight from storage
    struct Parked { const db::Item* item; uint16_t row, level; };
    static std::vector<Parked> g_parked;                   // bag items waiting in the private chest
    static Parked g_parking{};                             // the one being moved now
    static std::set<uint32_t> g_parkRefused;               // row<<16 | level the game would not move
    static constexpr uint16_t kChestType = 7;
    static std::set<int> g_bagBefore;
    static Worn g_view{};

    static volatile LONG g_moveAwait = 0, g_moveAnswered = 0, g_moveResult = 0;

    static void Say(const char* fmt, ...)
    {
        char b[320];
        va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
        LOG("[swap] %s", b);
        std::lock_guard<std::mutex> g(g_lock);
        g_status = b;
    }

    static void Publish()
    {
        std::lock_guard<std::mutex> g(g_lock);
        g_view.active = g_stage != Stage::None;
        g_view.name = g_item ? g_item->Label() : "";
        g_view.fromType = g_fromType;
        g_view.waitingToGoBack = g_stage == Stage::InBag && g_step == Step::Idle;
    }

    // ---------------------------------------------------------- requests ----
    typedef void  (__fastcall *FnTlsInit)();
    typedef void* (__fastcall *FnDescLookup)(uint64_t zero, uint32_t descId, uint32_t mask);
    typedef void* (__fastcall *FnAllocEvent)(uint64_t zero, uint32_t sizeCode);
    typedef void  (__fastcall *FnEnqueue)(void* queueObj, void* ev, void* desc, uint64_t zero);

    static bool SendRaw(uint16_t id, uint32_t player, const unsigned char* body, uint16_t len, char* why, size_t whyN)
    {
        const game::Fns& f = game::F();
        const uint16_t total = len + 5;
        __try
        {
            reinterpret_cast<FnTlsInit>(f.tlsInit)();
            uint32_t mask = 0;
            if (!mem::Read32(f.descMask, &mask)) { snprintf(why, whyN, "mask unreadable"); return false; }
            void* desc = reinterpret_cast<FnDescLookup>(f.descLookup)(0, id, mask);
            if (!desc) { snprintf(why, whyN, "descriptor 0x%04X not found", id); return false; }
            unsigned char* e = static_cast<unsigned char*>(reinterpret_cast<FnAllocEvent>(f.allocEvent)(0, total));
            if (!e) { snprintf(why, whyN, "event allocation failed"); return false; }
            *reinterpret_cast<uint32_t*>(e + sig::kOff_Ev_One) = 1;
            *reinterpret_cast<uint32_t*>(e + sig::kOff_Ev_Zero40) = 0;
            *reinterpret_cast<uint64_t*>(e + sig::kOff_Ev_Zero48) = 0;
            *reinterpret_cast<uint32_t*>(e + sig::kOff_Ev_Player) = player;
            *reinterpret_cast<uint32_t*>(e + sig::kOff_Ev_Player + 4) = 0;
            *reinterpret_cast<uint32_t*>(e + sig::kOff_Ev_Route) = hooks::Route();
            *reinterpret_cast<void**>(e + sig::kOff_Ev_Desc) = desc;
            *reinterpret_cast<uint16_t*>(e + sig::kOff_Ev_Size) = total;
            *reinterpret_cast<uint8_t*>(e + sig::kOff_Ev_Flag78) = 1;
            unsigned char* buf = *reinterpret_cast<unsigned char**>(e + sig::kOff_Ev_Buffer);
            if (!buf) { snprintf(why, whyN, "event has no payload buffer"); return false; }
            memcpy(buf, &id, 2); buf[2] = 0xFF; memcpy(buf + 3, &len, 2); memcpy(buf + 5, body, len);
            uintptr_t q = 0;
            if (!mem::ReadPtr(f.queue, &q)) { snprintf(why, whyN, "queue unreadable"); return false; }
            reinterpret_cast<FnEnqueue>(f.enqueue)(reinterpret_cast<void*>(q), e, desc, 0);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            snprintf(why, whyN, "fault 0x%08lX while sending", GetExceptionCode());
            return false;
        }
    }

    static bool Send(const char* cls, uint32_t eid, const unsigned char* body, uint16_t len)
    {
        const uint16_t id = hooks::DescId(cls);
        char why[160] = "";
        if (!id) snprintf(why, sizeof why, "%s has no id yet", cls);
        else if (!hooks::RouteKnown()) snprintf(why, sizeof why, "no player event seen yet");
        else if (SendRaw(id, hooks::Player() ? hooks::Player() : eid, body, len, why, sizeof why)) return true;
        Say("Could not send the request: %s.", why);
        return false;
    }

    // Which entry of the source inventory type's destination list leads to `to`.
    // The list is the game's own (the move routine indexes it); read live.
    static int DestIndex(uint16_t from, uint16_t to)
    {
        static uintptr_t s_global = 0;
        if (!s_global)
        {
            const uintptr_t fn = mem::Game().base + 0x57B410;   // lookup the move routine calls (exe 1.0.0.3663)
            if (!mem::MatchAt(fn, "48 83 EC 28 44 0F B7 01 48 8B 0D")) { LOG_ERR("[swap] destination table lookup moved; refusing moves"); return -1; }
            s_global = mem::RipAt(fn + 8, 7);
        }
        uintptr_t table = 0, arr = 0, obj = 0, recs = 0; uint32_t n = 0, cnt = 0;
        if (!mem::ReadPtr(s_global, &table) || !mem::Read32(table + 0x28, &n) || from >= n || !mem::ReadPtr(table + 0x20, &arr)) return -1;
        if (!mem::ReadPtr(arr + from * 0x18ull + 8, &obj) || !mem::ReadPtr(obj + 0x38, &recs) || !mem::Read32(obj + 0x40, &cnt) || cnt > 64) return -1;
        for (uint32_t i = 0; i < cnt; ++i)
        {
            uint8_t kind = 3; uint16_t src = 0, dst = 0;
            mem::Read8(recs + i * 0xA0ull, &kind); mem::Read16(recs + i * 0xA0ull + 2, &src); mem::Read16(recs + i * 0xA0ull + 4, &dst);
            if (kind != 3 && src == from && dst == to) return static_cast<int>(i);
        }
        return -1;
    }

    static bool SendMove(uint32_t eid, const db::Item* item, uint16_t level, uint16_t fromType, uint16_t fromSlot, uint16_t toType)
    {
        const int dest = DestIndex(fromType, toType);
        if (dest < 0) { Say("The game offers no route from %s to %s.", game::InvTypeName(fromType), game::InvTypeName(toType)); return false; }
        unsigned char b[30] = {};
        const uint64_t count = 1; const uint16_t key = fromType + 1; const uint32_t d = static_cast<uint32_t>(dest);
        memcpy(b + 0, &item->key, 4); memcpy(b + 4, &level, 2); memcpy(b + 6, &count, 8);
        memcpy(b + 14, &eid, 4); memcpy(b + 18, &eid, 4); memcpy(b + 22, &key, 2); memcpy(b + 24, &fromSlot, 2); memcpy(b + 26, &d, 4);
        LOG("[swap] move %s (level %u) from %s slot %u to %s (destination %d)", item->Label(), level,
            game::InvTypeName(fromType), fromSlot, game::InvTypeName(toType), dest);
        InterlockedExchange(&g_moveAnswered, 0);
        InterlockedExchange(&g_moveAwait, 1);
        return Send("TrocTrMoveItemInventoryToOtherInventoryReq", eid, b, sizeof b);
    }

    // Who the current swap dresses (TargetFor) and, for the horse / A.T.A.G., its mercenary key.
    static int g_target = 0;
    static uint64_t g_merc = 0;

    // `fromType` is the inventory the item is equipped from: the bag normally, a
    // storage inventory when trying to equip straight from it (test 26).
    static bool SendEquip(uint32_t eid, uint16_t bagSlot, uint16_t gear, uint16_t fromType = kBag)
    {
        const uint16_t inv = fromType + 1;
        g_failBase = hooks::FailEquipAcks();
        if (g_target)
        {
            // TrocTrMercenaryEquipItemReq (recorded in test 22): u64 mercenary, u16 inventory, u16 slot, u16 gear slot.
            unsigned char b[14];
            memcpy(b, &g_merc, 8); memcpy(b + 8, &inv, 2); memcpy(b + 10, &bagSlot, 2); memcpy(b + 12, &gear, 2);
            LOG("[swap] equip %s slot %u onto %s (mercenary %llu) gear slot %u", game::InvTypeName(fromType), bagSlot, TargetName(g_target),
                static_cast<unsigned long long>(g_merc), gear);
            return Send("TrocTrMercenaryEquipItemReq", eid, b, sizeof b);
        }
        unsigned char b[6];
        memcpy(b, &inv, 2); memcpy(b + 2, &bagSlot, 2); memcpy(b + 4, &gear, 2);
        LOG("[swap] equip %s slot %u into gear slot %u", game::InvTypeName(fromType), bagSlot, gear);
        return Send("TrocTrEquipItemReq", eid, b, sizeof b);
    }

    static bool SendUnequip(uint32_t eid, uint16_t gear)
    {
        if (g_target)
        {
            // TrocTrMercenaryUnequipItemReq (its handler reads 8 + 2 bytes): u64 mercenary, u16 gear slot.
            unsigned char b[10];
            memcpy(b, &g_merc, 8); memcpy(b + 8, &gear, 2);
            LOG("[swap] unequip %s (mercenary %llu) gear slot %u", TargetName(g_target), static_cast<unsigned long long>(g_merc), gear);
            return Send("TrocTrMercenaryUnequipItemReq", eid, b, sizeof b);
        }
        unsigned char b[2]; memcpy(b, &gear, 2);
        LOG("[swap] unequip gear slot %u", gear);
        return Send("TrocTrUnequipItemReq", eid, b, sizeof b);
    }

    void OnMoveResult(uint32_t result)
    {
        if (!InterlockedExchange(&g_moveAwait, 0)) return;   // the player's own move
        InterlockedExchange(&g_moveResult, static_cast<LONG>(result));
        InterlockedExchange(&g_moveAnswered, 1);
    }

    // ------------------------------------------------------- public calls ----
    bool TryOn(const stock::Entry& e)
    {
        if (e.invType != kChest && e.invType != kWardrobe && e.invType != kWarehouse && e.invType != kBag)
            return false;
        if (GearSlotFor(e.item->equipType) < 0)
        {
            std::lock_guard<std::mutex> g(g_lock);
            g_status = std::string(e.item->Label()) + ": crossbows have no slot on Kliff, Damiane or Oongka, so it can't be swapped in.";
            return false;
        }
        if (NoAtag(e.item->equipType))
        {
            std::lock_guard<std::mutex> g(g_lock);
            g_status = std::string(e.item->Label()) + ": the dyer has no A.T.A.G. tab, because you don't have an A.T.A.G. yet.";
            return false;
        }
        std::lock_guard<std::mutex> g(g_lock);
        g_want ={ static_cast<uint16_t>(e.item->row), 0, e.invType, e.slot };
        g_wantTry = true;
        g_wantRestore = false;
        g_status = std::string("Getting ") + e.item->Label() + " ready...";
        return true;
    }

    void RestoreSoon(unsigned delayMs)
    {
        std::lock_guard<std::mutex> g(g_lock);
        g_wantRestore = true;
        g_restoreAt = GetTickCount() + delayMs;
    }
    void CancelRestore()
    {
        std::lock_guard<std::mutex> g(g_lock);
        if (g_wantRestore) LOG("[swap] dye screen opened again; keeping the item on");
        g_wantRestore = false;
    }
    void RestoreNow()
    {
        std::lock_guard<std::mutex> g(g_lock);
        g_wantRestore = true;
        g_restoreAt = GetTickCount();
    }

    Worn Current() { std::lock_guard<std::mutex> g(g_lock); return g_view; }
    bool Busy() { std::lock_guard<std::mutex> g(g_lock); return g_wantTry || g_step != Step::Idle; }
    std::string Status() { std::lock_guard<std::mutex> g(g_lock); return g_status; }

    // ------------------------------------------------------------- drain ----
    struct Inv { std::vector<game::Slot> all; uint32_t eid = 0; bool ok = false; int bagUsed = -1, bagCap = -1; };

    static Inv ReadInv()
    {
        Inv v;
        const uintptr_t actor = game::ControlledActor();
        v.eid = game::ActorEid(actor);
        const uintptr_t holder = actor ? game::Holder(actor) : 0;
        v.ok = holder && v.eid && game::Slots(holder, v.all) > 0;
        game::Bucket bk[64];
        const int nb = holder ? game::Buckets(holder, bk, 64) : 0;
        for (int b = 0; b < nb; ++b) if (bk[b].type == kBag) { v.bagUsed = bk[b].used; v.bagCap = bk[b].cap; }
        return v;
    }
    static std::set<int> BagSlots(const Inv& v)
    {
        std::set<int> s;
        for (const game::Slot& x : v.all) if (x.invType == kBag) s.insert(x.slot);
        return s;
    }
    static const game::Slot* NewInBag(const Inv& v, uint16_t row, int level, const std::set<int>& before)
    {
        for (const game::Slot& x : v.all)
            if (x.invType == kBag && !before.count(x.slot) && (row == 0xFFFF || x.row == row) && (level < 0 || x.level == level)) return &x;
        return nullptr;
    }
    static const game::Slot* InBag(const Inv& v, uint16_t row, uint16_t level)
    {
        for (const game::Slot& x : v.all) if (x.invType == kBag && x.row == row && x.level == level) return &x;
        return nullptr;
    }

    static void Go(Step s) { g_step = s; g_stepAt = GetTickCount(); }

    static void Fail(const char* fmt, ...)
    {
        char b[320];
        va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
        LOG_ERR("[swap] stopped: %s", b);
        { std::lock_guard<std::mutex> g(g_lock); g_status = b; g_wantTry = false; g_wantRestore = false; }
        g_step = Step::Idle;
        // Colors the mod applied are saved, but the open dyer's table only catches up
        // on a refresh; refresh now so the next pick does not apply (and charge) again.
        if (g_colorsSaved && hooks::DyeScreenOpen()) hooks::RequestRefresh(5, 150);
        g_colorsSaved = false;
        g_directTried = false;
    }

    static const game::Slot* At(const Inv& v, uint16_t invType, uint16_t slot)
    {
        for (const game::Slot& x : v.all) if (x.invType == invType && x.slot == slot) return &x;
        return nullptr;
    }
    static const game::Slot* InType(const Inv& v, uint16_t invType, uint16_t row, uint16_t level, int preferSlot = -1)
    {
        const game::Slot* any = nullptr;
        for (const game::Slot& x : v.all)
            if (x.invType == invType && x.row == row && x.level == level) { if (x.slot == preferSlot) return &x; if (!any) any = &x; }
        return any;
    }

    // What was knocked off when the item went on, and the messages for it.
    static void OnWorn(const game::Slot* old, uint16_t oldInv)
    {
        g_haveOld = old != nullptr;
        if (old) { g_oldRow = old->row; g_oldLevel = old->level; g_oldInv = oldInv; g_oldSlot = old->slot; }
        g_stage = Stage::Worn;
        g_step = Step::Idle;
        g_directTried = false;
        const db::Item* oi = old ? db::ByRow(old->row) : nullptr;
        if (hooks::DyeScreenOpen())
        {
            { hooks::RequestRefresh(5, 150); g_colorsSaved = false; }
            if (g_target == 1) Say("Your horse wears %s; on the dyer's horse tab (RB), pick it to dye it.", g_item->Label());
            else if (g_target == 2) Say("The A.T.A.G. wears %s; on the dyer's A.T.A.G. tab, pick it to dye it.", g_item->Label());
            else Say("You are wearing %s; pick it in the dyer's list to dye it.", g_item->Label());
        }
        else if (g_target) Say("%s now wears %s. Open the dye screen to dye it.", g_target == 1 ? "Your horse" : "The A.T.A.G.", g_item->Label());
        else Say("You are wearing %s. Open the dye screen to dye it.", g_item->Label());
        if (old)
        {
            std::lock_guard<std::mutex> g(g_lock);
            g_status += std::string(" Your ") + (oi ? oi->Label() : "previous piece") + " waits in your " +
                        (oldInv == kBag ? std::string("bag.") : std::string(game::InvTypeName(oldInv)) + ".");
        }
        Publish();
    }

    // A bag item that can wait in the private chest: a single dyeable piece (its key
    // is known), not the item being swapped or the piece worn before. Last slot first.
    static const game::Slot* ParkCandidate(const Inv& v, uint16_t wantRow)
    {
        const game::Slot* best = nullptr;
        for (const game::Slot& x : v.all)
        {
            if (x.invType != kBag || x.count != 1 || !db::ByRow(x.row) || x.row == wantRow) continue;
            if (g_stage != Stage::None && g_haveOld && x.row == g_oldRow && x.level == g_oldLevel) continue;
            if (g_stage != Stage::None && g_item && x.row == g_item->row) continue;
            if (g_parkRefused.count((static_cast<uint32_t>(x.row) << 16) | x.level)) continue;
            if (!best || x.slot > best->slot) best = &x;
        }
        return best;
    }

    static void BeginRestore(const Inv& v)
    {
        if (g_stage == Stage::InBag && g_fromType == kBag)
        {
            g_stage = Stage::None;
            Say("%s stays in your bag, where it came from.", g_item->Label());
            return;
        }
        if (g_stage == Stage::InBag)
        {
            const game::Slot* s = InBag(v, static_cast<uint16_t>(g_item->row), g_level);
            if (!s) { Fail("%s is no longer in your bag, so there is nothing to put back.", g_item->Label()); g_stage = Stage::None; return; }
            g_itemBagSlot = s->slot;
            if (SendMove(v.eid, g_item, g_level, kBag, s->slot, g_fromType)) Go(Step::PutAway);
            else g_step = Step::Idle;
            return;
        }
        g_bagBefore = BagSlots(v);
        bool sent;
        if (g_haveOld)
        {
            const game::Slot* old = InType(v, g_oldInv, g_oldRow, g_oldLevel, g_oldSlot);
            if (!old) { Fail("The item you were wearing before is no longer in your %s; %s stays on.",
                             g_oldInv == kBag ? "bag" : game::InvTypeName(g_oldInv), g_item->Label()); return; }
            if (g_oldInv == kBag) g_bagBefore.erase(old->slot);   // the game may put the swapped-out piece into this very slot
            g_restoreInv = g_oldInv; g_restoreSlot = old->slot;
            g_putBackRow = g_oldRow; g_putBackLevel = g_oldLevel;
            sent = SendEquip(v.eid, old->slot, static_cast<uint16_t>(g_gear), g_oldInv);
        }
        else { g_restoreInv = kBag; sent = SendUnequip(v.eid, static_cast<uint16_t>(g_gear)); }
        if (sent) { Say("Putting %s back...", g_item->Label()); Go(Step::Unwear); }
    }

    void Drain()
    {
        bool wantTry, wantRestore; Want want; DWORD restoreAt;
        {
            std::lock_guard<std::mutex> g(g_lock);
            wantTry = g_wantTry; wantRestore = g_wantRestore; want = g_want; restoreAt = g_restoreAt;
        }
        const DWORD now = GetTickCount();
        if (g_step == Step::Idle)
        {
            const bool restoreDue = wantRestore && static_cast<LONG>(now - restoreAt) >= 0 && g_stage != Stage::None;
            if (wantRestore && g_stage == Stage::None) { std::lock_guard<std::mutex> g(g_lock); g_wantRestore = false; }
            // Dyeing is over (nothing worn, nothing asked, dye screen closed): bring
            // back what waited in the private chest.
            if (!g_parked.empty() && g_stage == Stage::None && !wantTry && !wantRestore && !hooks::DyeScreenOpen() && !g_applyAt)
            {
                const Inv pv = ReadInv();
                if (!pv.ok) return;
                const Parked p = g_parked.back();
                const game::Slot* s = InType(pv, kChestType, p.row, p.level);
                if (!s)
                {
                    g_parked.pop_back();
                    Say("%s is no longer in your private chest; nothing to bring back.", p.item->Label());
                    return;
                }
                g_parking = p;
                if (SendMove(pv.eid, p.item, p.level, kChestType, s->slot, kBag)) { Say("Bringing %s back to your bag...", p.item->Label()); Go(Step::Unpark); }
                else g_parked.pop_back();
                return;
            }
            if (!restoreDue && !wantTry) return;
            // A swap refreshes the dye screen like a tab change, which drops colors
            // that were picked but not applied. Never do that silently.
            // So colors picked but not applied yet are applied first, exactly as if
            // the player pressed Dye and confirmed, and the swap waits until the
            // game has saved them on the item.
            // Waiting for the game to confirm colors the mod applied: its dye
            // reply. The dye screen's own "saved" table is not updated while it
            // stays open, so the reply is the signal, and the check is not asked
            // again until this swap has taken the next item out.
            if (g_applyAt)
            {
                if (hooks::DyeAcks() != g_ackBase)
                {
                    Say("Colors applied%s%s.", g_item ? " to " : "", g_item ? g_item->Label() : "");
                    LOG("[swap] the game confirmed the colors %lu ms after applying", now - g_applyAt);
                    g_applyAt = 0;
                    g_colorsSaved = true;
                }
                else if (now - g_applyAt > 8000)
                {
                    g_applyAt = 0;
                    Fail("The game did not confirm the colors (the dyer may have refused, for example over cost). Press Dye yourself or reset them, then pick the next item.");
                    return;
                }
                else return;
            }
            const int pending = (hooks::DyeScreenOpen() && !g_colorsSaved) ? hooks::UnappliedDye() : 0;
            if (pending < 0)
            {
                // Never apply (and maybe pay) when the mod cannot tell what changed.
                if (wantTry) Fail("The mod cannot tell whether you have colors that are not applied yet. Press Dye (or reset), then pick the next item.");
                return;
            }
            if (pending > 0)
            {
                g_ackBase = hooks::DyeAcks();
                if (hooks::ApplyDyeNow() < 0)
                {
                    if (wantTry) Fail("Your new colors are not applied yet and the mod could not apply them. Press Dye, then pick the next item.");
                    return;
                }
                g_applyAt = now ? now : 1;
                Say("Applying your colors%s%s first...", g_item ? " to " : "", g_item ? g_item->Label() : "");
                return;
            }
            const Inv v = ReadInv();
            if (!v.ok) return;
            if (g_stage != Stage::None) { BeginRestore(v); Publish(); return; }   // put the last one back first
            const game::Slot* src = nullptr;
            for (const game::Slot& x : v.all) if (x.invType == want.fromType && x.slot == want.fromSlot) src = &x;
            const db::Item* item = db::ByRow(want.row);
            if (!item || !src || src->row != want.row)
            {
                // Picking the piece that was worn before (waiting in the bag): putting the
                // last item back already put it on again, which is what was asked.
                if (item && want.fromType == kBag && static_cast<int>(want.row) == g_putBackRow)
                {
                    { std::lock_guard<std::mutex> g(g_lock); g_wantTry = false; }
                    g_putBackRow = -1;
                    Say("%s is on again.", item->Label());
                    return;
                }
                Fail("That item has moved since the list was read; pick it again.");
                return;
            }
            g_putBackRow = -1;
            // From storage with a full bag: first try equipping straight from storage
            // (no bag slot needed); if the game refuses, park a bag item in the
            // private chest to free a slot (it comes back when dyeing is over).
            if (want.fromType != kBag && v.bagCap > 0 && v.bagUsed >= v.bagCap)
            {
                if (!g_directTried)
                {
                    g_directTried = true;
                    const int target = TargetFor(item->equipType);
                    uint64_t merc = 0;
                    if (target)
                    {
                        if (!hooks::DyeScreenOpen())
                        { Fail("Open the dye screen first: %s is put on %s, and the dyer says which one.", item->Label(), TargetName(target)); return; }
                        merc = hooks::LearnMercKey(target);
                        if (!merc) { Fail("The dyer shows no %s right now, so %s cannot be put on.", target == 1 ? "horse" : "A.T.A.G.", item->Label()); return; }
                    }
                    g_target = target; g_merc = merc;
                    g_item = item; g_level = src->level; g_fromType = want.fromType; g_srcSlot = want.fromSlot;
                    g_gear = GearSlotFor(item->equipType);
                    g_haveOld = false;
                    g_bagBefore = BagSlots(v);
                    { std::lock_guard<std::mutex> g(g_lock); g_wantTry = false; }
                    // g_colorsSaved stays: if this is refused the swap goes on with parking,
                    // and the dyer's saved-colors table is not fresh until the next refresh.
                    LOG("[swap] bag full (%d of %d): trying to equip %s straight from the %s", v.bagUsed, v.bagCap, item->Label(), game::InvTypeName(want.fromType));
                    if (SendEquip(v.eid, want.fromSlot, static_cast<uint16_t>(g_gear), want.fromType))
                    { Say("Your bag is full; putting %s on straight from the %s...", item->Label(), game::InvTypeName(want.fromType)); Go(Step::WearDirect); }
                    return;
                }
                if (g_parked.size() >= 4) { Fail("Your bag is full (%d of %d) and the mod has already moved 4 items aside; free a bag slot, then pick it again.", v.bagUsed, v.bagCap); return; }
                const game::Slot* c = ParkCandidate(v, want.row);
                if (!c) { Fail("Your bag is full (%d of %d) and the mod found nothing it can move aside; free a bag slot, then pick it again.", v.bagUsed, v.bagCap); return; }
                const db::Item* ci = db::ByRow(c->row);
                g_parking = { ci, c->row, c->level };
                if (SendMove(v.eid, ci, c->level, kBag, c->slot, kChestType))
                { Say("Your bag is full; moving %s to your private chest for now...", ci->Label()); Go(Step::Park); }
                else Fail("Your bag is full and the mod could not move anything aside; free a bag slot, then pick it again.");
                return;
            }
            // Take the new one out.
            { std::lock_guard<std::mutex> g(g_lock); g_wantTry = false; }
            g_colorsSaved = false;
            g_directTried = false;
            // Horse / A.T.A.G. gear: the dyer tells which horse or A.T.A.G. it shows.
            const int target = TargetFor(item->equipType);
            uint64_t merc = 0;
            if (target)
            {
                if (!hooks::DyeScreenOpen())
                { Fail("Open the dye screen first: %s is put on %s, and the dyer says which one.", item->Label(), TargetName(target)); return; }
                merc = hooks::LearnMercKey(target);
                if (!merc)
                { Fail("The dyer shows no %s right now, so %s cannot be put on.", target == 1 ? "horse" : "A.T.A.G.", item->Label()); return; }
            }
            g_target = target; g_merc = merc;
            g_item = item; g_level = src->level; g_fromType = want.fromType; g_gear = GearSlotFor(item->equipType);
            g_haveOld = false;
            g_bagBefore = BagSlots(v);
            if (want.fromType == kBag)
            {
                g_itemBagSlot = src->slot;
                g_stage = Stage::InBag;
                if (SendEquip(v.eid, src->slot, static_cast<uint16_t>(g_gear))) { Say("Putting on %s...", item->Label()); Go(Step::Wear); }
                Publish();
                return;
            }
            if (SendMove(v.eid, item, g_level, want.fromType, want.fromSlot, kBag)) { Say("Taking %s out of the %s...", item->Label(), game::InvTypeName(want.fromType)); Go(Step::TakeOut); }
            return;
        }

        const bool late = now - g_stepAt > kStepTimeout;
        const Inv v = ReadInv();
        if (!v.ok) { if (late) Fail("Could not read your inventories."); return; }

        switch (g_step)
        {
        case Step::TakeOut:
        {
            if (InterlockedCompareExchange(&g_moveAnswered, 0, 0) && InterlockedCompareExchange(&g_moveResult, 0, 0) != 0)
            {
                const unsigned long code = static_cast<unsigned long>(g_moveResult);
                if (code == 0xD2023F88ul) Fail("The game says your bag is full, so %s cannot come out. Free a bag slot, then pick it again.", g_item->Label());
                else Fail("The game refused to take %s out (code %08lX). Nothing changed.", g_item->Label(), code);
                return;
            }
            const game::Slot* s = NewInBag(v, static_cast<uint16_t>(g_item->row), g_level, g_bagBefore);
            if (!s) { if (late) Fail("%s did not arrive in your bag. Nothing changed.", g_item->Label()); return; }
            g_stage = Stage::InBag;
            g_itemBagSlot = s->slot;
            g_bagBefore = BagSlots(v);
            if (SendEquip(v.eid, s->slot, static_cast<uint16_t>(g_gear))) { Say("Putting on %s...", g_item->Label()); Go(Step::Wear); }
            else g_step = Step::Idle;
            Publish();
            return;
        }
        case Step::Wear:
        {
            bool stillInBag = false;
            for (const game::Slot& x : v.all) if (x.invType == kBag && x.slot == g_itemBagSlot && x.row == g_item->row) stillInBag = true;
            if (stillInBag)
            {
                const bool refused = hooks::FailEquipAcks() != g_failBase;
                if (refused || late)
                {
                    Fail(refused ? "Your current character cannot wear %s, so it goes back."
                                 : "The game did not put %s on, so it goes back.", g_item->Label());
                    // Return it to where it came from right away.
                    if (g_fromType != kBag) { std::lock_guard<std::mutex> g(g_lock); g_wantRestore = true; g_restoreAt = now; }
                    else g_stage = Stage::None;
                }
                Publish();
                return;
            }
            // Worn. Whatever was in that gear slot before has landed in the bag.
            std::set<int> before = g_bagBefore; before.erase(g_itemBagSlot);
            const game::Slot* old = NewInBag(v, 0xFFFF, -1, before);
            if (!old && now - g_stepAt < 800) return;   // give the old piece a moment to show up
            OnWorn(old, kBag);
            return;
        }
        case Step::WearDirect:
        {
            const game::Slot* at = At(v, g_fromType, g_srcSlot);
            const bool stillThere = at && at->row == g_item->row && at->level == g_level;
            if (stillThere)
            {
                const bool refused = hooks::FailEquipAcks() != g_failBase;
                if (refused || now - g_stepAt > 2500)
                {
                    LOG("[swap] equipping straight from the %s was %s; making room in the bag instead",
                        game::InvTypeName(g_fromType), refused ? "refused" : "not answered");
                    g_step = Step::Idle;
                    std::lock_guard<std::mutex> g(g_lock);
                    g_wantTry = true;   // g_want still names the item; the next pass parks a bag item
                }
                return;
            }
            // On. The piece knocked off went to the bag, or into the storage slot it came from.
            const game::Slot* old = NewInBag(v, 0xFFFF, -1, g_bagBefore);
            uint16_t oldInv = kBag;
            if (!old && at && at->row != g_item->row) { old = at; oldInv = g_fromType; }
            if (!old && now - g_stepAt < 800) return;
            LOG("[swap] equipped straight from the %s; the previous piece is %s", game::InvTypeName(g_fromType),
                old ? (oldInv == kBag ? "in the bag" : "in the storage slot") : "not there (slot was empty)");
            OnWorn(old, oldInv);
            return;
        }
        case Step::Park:
        {
            if (!InterlockedCompareExchange(&g_moveAnswered, 0, 0))
            {
                if (late) { Fail("No answer from the game while making room; free a bag slot, then pick the item again."); Publish(); }
                return;
            }
            const uint32_t r = static_cast<uint32_t>(InterlockedCompareExchange(&g_moveResult, 0, 0));
            g_step = Step::Idle;
            if (r != 0)
            {
                LOG("[swap] the game would not move %s to the private chest (code %08X); trying another", g_parking.item->Label(), r);
                g_parkRefused.insert((static_cast<uint32_t>(g_parking.row) << 16) | g_parking.level);
                if (g_parkRefused.size() > 6) { Fail("Your bag is full and the game would not let the mod move items aside; free a bag slot, then pick it again."); return; }
            }
            else
            {
                g_parked.push_back(g_parking);
                Say("%s waits in your private chest while you dye; it comes back to your bag when you close the dyer.", g_parking.item->Label());
            }
            std::lock_guard<std::mutex> g(g_lock);
            g_wantTry = true;   // go on with the item that was picked
            return;
        }
        case Step::Unpark:
        {
            if (!InterlockedCompareExchange(&g_moveAnswered, 0, 0))
            {
                if (late) { g_parked.pop_back(); Fail("No answer from the game; %s stays in your private chest.", g_parking.item->Label()); }
                return;
            }
            const uint32_t r = static_cast<uint32_t>(InterlockedCompareExchange(&g_moveResult, 0, 0));
            g_parked.pop_back();
            g_step = Step::Idle;
            if (r != 0) Fail("%s could not come back to your bag (code %08X); it is in your private chest.", g_parking.item->Label(), r);
            else Say("%s is back in your bag.", g_parking.item->Label());
            return;
        }
        case Step::Unwear:
        {
            const game::Slot* s = NewInBag(v, static_cast<uint16_t>(g_item->row), g_level, g_bagBefore);
            if (!s && g_restoreInv != kBag)
            {
                // Put back by equipping the old piece straight from storage: the dyed
                // item may have gone into that storage slot, which is where it belongs.
                const game::Slot* at = At(v, g_restoreInv, g_restoreSlot);
                if (at && at->row == g_item->row && at->level == g_level)
                {
                    g_stage = Stage::None; g_step = Step::Idle;
                    { std::lock_guard<std::mutex> g(g_lock); g_wantRestore = false; if (!g_wantTry) g_colorsSaved = false; }
                    Say("%s is back in the %s.", g_item->Label(), game::InvTypeName(g_restoreInv));
                    if (hooks::DyeScreenOpen()) hooks::RequestRefresh(5, 150);
                    Publish();
                    return;
                }
            }
            if (!s) { if (late) Fail("The game did not take %s off; it stays on.", g_item->Label()); return; }
            g_stage = Stage::InBag;
            g_itemBagSlot = s->slot;
            if (g_fromType == kBag)
            {
                g_stage = Stage::None; g_step = Step::Idle;
                { std::lock_guard<std::mutex> g(g_lock); g_wantRestore = false; }
                Say("%s is back in your bag.", g_item->Label());
                Publish();
                return;
            }
            if (SendMove(v.eid, g_item, g_level, kBag, s->slot, g_fromType)) Go(Step::PutAway);
            else g_step = Step::Idle;
            Publish();
            return;
        }
        case Step::PutAway:
        {
            if (!InterlockedCompareExchange(&g_moveAnswered, 0, 0)) { if (late) Fail("No answer from the game; %s is in your bag.", g_item->Label()); Publish(); return; }
            const uint32_t r = static_cast<uint32_t>(InterlockedCompareExchange(&g_moveResult, 0, 0));
            if (r != 0)
            {
                Fail("%s could not go back to the %s (code %08X); it is in your bag. Press Put back when you are next to it.",
                     g_item->Label(), game::InvTypeName(g_fromType), r);
                Publish();
                return;
            }
            Say("%s is back in the %s.", g_item->Label(), game::InvTypeName(g_fromType));
            if (hooks::DyeScreenOpen()) hooks::RequestRefresh(5, 150);
            // Mid-swap the next item comes out right away, before the dyer's list
            // refreshes, so the confirmed colors must not be asked about again
            // (that would apply, and charge, twice). Only a plain put-back clears it.
            { std::lock_guard<std::mutex> g(g_lock); if (!g_wantTry) g_colorsSaved = false; }

            g_stage = Stage::None;
            g_step = Step::Idle;
            { std::lock_guard<std::mutex> g(g_lock); g_wantRestore = false; }
            Publish();
            return;
        }
        default: g_step = Step::Idle; return;
        }
    }
}
