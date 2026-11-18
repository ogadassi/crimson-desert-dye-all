// Ported from Master Looter (https://github.com/shin2344234/master-looter), loot/game.cpp,
// MIT License, Copyright (c) 2026 Seth. Trimmed to what Dye All needs.
#include "game.h"

#include <Windows.h>
#include <cstring>
#include <string>

#include "farhook.h"
#include "mem.h"
#include "signatures.h"
#include "../core/log.h"

namespace dye::game
{
    using namespace ml;
    using namespace ml::sig;

    static Fns g_f;

    static void OwnerOf(uintptr_t dest, char* owner, size_t n)
    {
        strncpy_s(owner, n, "an unknown module", _TRUNCATE);
        HMODULE mod = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(dest), &mod) && mod)
        {
            char path[MAX_PATH];
            if (GetModuleFileNameA(mod, path, MAX_PATH))
            {
                const char* base = strrchr(path, '\\');
                strncpy_s(owner, n, base ? base + 1 : path, _TRUNCATE);
            }
        }
    }

    // A pattern that starts at a function entry finds nothing once another mod
    // (Crimson Collection, Trinity, CDAutoLoot, ...) has put its jump there. Try
    // each jump form with the covered bytes wildcarded; the jump must leave the game.
    static uintptr_t FindUnderPatch(const char* pattern, char* owner, size_t ownerN)
    {
        std::vector<std::string> t;
        for (const char* p = pattern; *p; )
        {
            while (*p == ' ') ++p;
            const char* q = p;
            while (*q && *q != ' ') ++q;
            if (q > p) t.emplace_back(p, q);
            p = q;
        }
        struct Form { const char* head; unsigned headN, cover, minTail; bool rel; };
        static const Form forms[] = {
            { "E9 ?? ?? ?? ??", 5, 8, 8, true },
            { "48 B8 ?? ?? ?? ?? ?? ?? ?? ?? FF E0", 12, 16, 6, false },
            { "FF 25 00 00 00 00 ?? ?? ?? ?? ?? ?? ?? ??", 14, 16, 6, false },
        };
        for (const Form& f : forms)
        {
            if (t.size() < f.cover + f.minTail) continue;
            std::string v = f.head;
            for (unsigned i = f.headN; i < f.cover; ++i) v += " ??";
            for (size_t i = f.cover; i < t.size(); ++i) { v += ' '; v += t[i]; }
            size_t hits = 0;
            const uintptr_t j = mem::FindUnique(v.c_str(), &hits);
            if (!j) continue;
            const uintptr_t dest = f.rel ? mem::RipAt(j, 5) : farhook::AbsJumpTarget(j);
            if (!dest || mem::InImage(dest)) continue;
            OwnerOf(dest, owner, ownerN);
            return j;
        }
        return 0;
    }

    uintptr_t FindEntry(const char* pattern, size_t* hits)
    {
        uintptr_t a = mem::FindUnique(pattern, hits);
        char owner[MAX_PATH] = "";
        if (!a && !*hits && (a = FindUnderPatch(pattern, owner, sizeof owner)) != 0)
        {
            *hits = 1;
            LOG("[sig] +0x%llX found under a jump %s put over its entry", static_cast<unsigned long long>(mem::Rva(a)), owner);
        }
        return a;
    }

    static uintptr_t Scan(const char* name, const char* pattern, bool required)
    {
        size_t hits = 0;
        uintptr_t a = mem::FindUnique(pattern, &hits);
        char owner[MAX_PATH] = "";
        if (!a && !hits) a = FindUnderPatch(pattern, owner, sizeof owner);
        if (a && owner[0])
            LOG("[sig] %-14s +0x%llX, under a jump %s put over its entry", name, static_cast<unsigned long long>(mem::Rva(a)), owner);
        else if (a) LOG("[sig] %-14s +0x%llX", name, static_cast<unsigned long long>(mem::Rva(a)));
        else LOG_ERR("[sig] %-14s %s (%zu hits)%s", name, hits ? "AMBIGUOUS" : "not found", hits, required ? " - required" : "");
        return a;
    }

    static uintptr_t ScanJumped(const char* name, const char* pattern, const char* jumped)
    {
        const uintptr_t a = Scan(name, pattern, false);
        if (a) return a;
        size_t hits = 0;
        const uintptr_t j = mem::FindUnique(jumped, &hits);
        if (!j) return 0;
        const uintptr_t dest = mem::RipAt(j, 5);
        if (mem::InImage(dest)) return 0;
        char owner[MAX_PATH];
        OwnerOf(dest, owner, sizeof owner);
        LOG("[sig] %-14s +0x%llX, under a jump %s put over its entry", name, static_cast<unsigned long long>(mem::Rva(j)), owner);
        return j;
    }

    bool ResolveAll()
    {
        g_f = Fns{};
        const uintptr_t tls = Scan("tls+desc", kSig_TlsDesc, true);
        if (tls) { g_f.tlsInit = tls; g_f.descLookup = tls + kOff_TlsDesc_DescLookup; }
        g_f.allocEvent = Scan("alloc_event", kSig_AllocEvent, true);
        g_f.enqueue    = Scan("enqueue", kSig_Enqueue, true);
        const uintptr_t dq = Scan("desc_mask+queue", kSig_DescMaskQueue, true);
        if (dq)
        {
            g_f.descMask = mem::RipAt(dq + kOff_DescMask_Mov, 7);
            g_f.queue    = mem::RipAt(dq + kOff_Queue_Mov, 7);
            if (!mem::InImage(g_f.descMask) || !mem::InImage(g_f.queue)) g_f.descMask = g_f.queue = 0;
        }
        g_f.moveUpdate = ScanJumped("move_update", kSig_MoveUpdate, kSig_MoveUpdateJumped);
        {
            size_t h = 0;
            g_f.invHolder = mem::FindUnique(kSig_InvHolder3663, &h) ? Scan("inv_holder", kSig_InvHolder3663, false)
                                                                     : Scan("inv_holder", kSig_InvHolder, false);
        }
        {
            size_t hits = 0;
            const uintptr_t site = mem::FindUnique(kSig_ControlSite, &hits);
            const uintptr_t g = site ? mem::RipAt(site, 7) : 0;
            const uintptr_t acc = site ? mem::RipAt(site + kOff_ControlSite_Call, 5) : 0;
            unsigned ctl = 0, act = 0;
            for (unsigned i = 0; acc && i < 0x180 && (!ctl || !act); ++i)
            {
                uint8_t d8 = 0; uint32_t d32 = 0;
                if (!ctl && mem::MatchAt(acc + i, "48 8B 79") && mem::Read8(acc + i + 3, &d8)) ctl = d8;
                else if (ctl && !act && mem::MatchAt(acc + i, "4C 8B B0") && mem::Read32(acc + i + 3, &d32) && d32 < 0x1000) act = d32;
            }
            if (g && mem::InImage(g) && ctl && act) { g_f.controlGlobal = g; g_f.ctlController = ctl; g_f.ctlActor = act; }
            LOG("[sig] controller     %s (controller +0x%X, actor +0x%X)", g_f.controlGlobal ? "found" : "NOT found", ctl, act);
        }
        return g_f.tlsInit && g_f.allocEvent && g_f.enqueue && g_f.descMask && g_f.queue;
    }

    const Fns& F() { return g_f; }

    uintptr_t ControlledActor()
    {
        if (!g_f.controlGlobal) return 0;
        const uintptr_t svcs = mem::Deref(g_f.controlGlobal, 0);
        const uintptr_t svc  = svcs ? mem::Deref(svcs, kOff_ControlSite_Svc) : 0;
        const uintptr_t ctl  = svc ? mem::Deref(svc, g_f.ctlController) : 0;
        const uintptr_t a    = ctl ? mem::Deref(ctl, g_f.ctlActor) : 0;
        if (!a || !mem::Readable(a, 0x100)) return 0;
        uint32_t eid = 0;
        return mem::Read32(a + kOff_Ent_Eid, &eid) && eid ? a : 0;
    }

    uint32_t ActorEid(uintptr_t actor)
    {
        uint32_t eid = 0;
        return actor && mem::Read32(actor + kOff_Ent_Eid, &eid) ? eid : 0;
    }

    // ------------------------------------------------------------- holder ----
    static SRWLOCK   g_holderLock = SRWLOCK_INIT;
    static uintptr_t g_holderFor = 0, g_holderIs = 0;
    static bool      g_holderKnown = false;

    static uintptr_t OwnHolder(uintptr_t actor)
    {
        const uintptr_t comps = actor ? mem::Deref(actor, kOff_Ent_Comps) : 0;
        return comps ? mem::Deref(comps, kOff_Comps_InvHolder) : 0;
    }

    static bool CallInvHolder(uintptr_t fn, uintptr_t actor, uintptr_t* out)
    {
        __try { *out = reinterpret_cast<uintptr_t (*)(uintptr_t)>(fn)(actor); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { *out = 0; return false; }
    }

    void RefreshHolder(uintptr_t actor)
    {
        if (!g_f.invHolder || !actor || !mem::Readable(actor, 0x100)) return;
        uintptr_t h = 0;
        if (!CallInvHolder(g_f.invHolder, actor, &h)) return;
        if (h && !mem::Readable(h, 0x40)) return;
        AcquireSRWLockExclusive(&g_holderLock);
        const bool changed = g_holderFor != actor || g_holderIs != h || !g_holderKnown;
        g_holderFor = actor; g_holderIs = h; g_holderKnown = true;
        ReleaseSRWLockExclusive(&g_holderLock);
        if (changed) LOG("[inv] holder for actor %llX is %llX (its own is %llX)", static_cast<unsigned long long>(actor),
                         static_cast<unsigned long long>(h), static_cast<unsigned long long>(OwnHolder(actor)));
    }

    uintptr_t Holder(uintptr_t actor)
    {
        if (!actor) return 0;
        AcquireSRWLockShared(&g_holderLock);
        const bool known = g_holderKnown && g_holderFor == actor;
        const uintptr_t h = known ? g_holderIs : 0;
        ReleaseSRWLockShared(&g_holderLock);
        return known ? h : OwnHolder(actor);
    }

    int Buckets(uintptr_t holder, Bucket* out, int max)
    {
        int n = 0;
        uintptr_t barr = 0; uint32_t bn = 0;
        if (!holder || !mem::ReadPtr(holder + kOff_Inv_Buckets, &barr) || !mem::Read32(holder + kOff_Inv_BucketN, &bn) || bn > 64) return 0;
        for (uint32_t b = 0; b < bn && n < max; ++b)
        {
            Bucket bi = { 0, 0xFFFF, 0, 0, 0 };
            if (mem::ReadPtr(barr + 8ull * b, &bi.addr))
            {
                mem::Read16(bi.addr + 0x10, &bi.type);
                mem::Read16(bi.addr + kOff_Bucket_SlotN, &bi.slotN);
                mem::Read16(bi.addr + kOff_Bucket_Used, &bi.used);
                mem::Read16(bi.addr + kOff_Bucket_Cap, &bi.cap);
            }
            out[n++] = bi;
        }
        return n;
    }

    // Master Looter found 0xC0 and 0xC8 in different references; pick the one
    // under which more slots read as empty (0xFFFF) or a plausible item row.
    static unsigned g_stride = 0;
    static unsigned ProbeStride(uintptr_t slots, uint16_t sn)
    {
        const unsigned cands[2] = { 0xC0, 0xC8 };
        int best = -1; unsigned bestStride = kInv_SlotStride;
        for (unsigned st : cands)
        {
            if (!mem::Readable(slots, static_cast<size_t>(sn) * st)) continue;
            int ok = 0;
            for (uint16_t i = 0; i < sn && i < 64; ++i)
            {
                uint16_t type = 0;
                if (!mem::Read16(slots + static_cast<uintptr_t>(i) * st + kOff_Slot_TypeId, &type)) break;
                if (type == 0xFFFF || type < 0x4000) ++ok;
            }
            if (ok > best) { best = ok; bestStride = st; }
        }
        LOG("[inv] slot stride 0x%X (%d of %u slots plausible)", bestStride, best, sn < 64 ? sn : 64);
        return bestStride;
    }

    int Slots(uintptr_t holder, std::vector<Slot>& out)
    {
        out.clear();
        Bucket bk[64];
        const int nb = Buckets(holder, bk, 64);
        for (int b = 0; b < nb; ++b)
        {
            uintptr_t slots = 0;
            const uint16_t sn = bk[b].slotN;
            if (!bk[b].addr || !sn || sn > 4096 || !mem::ReadPtr(bk[b].addr + kOff_Bucket_Slots, &slots)) continue;
            if (!g_stride && sn >= 8) g_stride = ProbeStride(slots, sn);
            const unsigned stride = g_stride ? g_stride : kInv_SlotStride;
            if (!mem::Readable(slots, static_cast<size_t>(sn) * stride)) continue;
            for (uint16_t i = 0; i < sn; ++i)
            {
                const uintptr_t s = slots + static_cast<uintptr_t>(i) * stride;
                uint16_t row = 0; uint64_t inst = 0, q = 0;
                if (!mem::Read16(s + kOff_Slot_TypeId, &row) || row == 0xFFFF || row == 0) continue;
                if (!mem::Read64(s, &inst) || !inst || inst == ~0ull) continue;
                const long long count = (mem::Read64(s + 0x10, &q) && q > 0 && q < 100000000ull) ? static_cast<long long>(q) : 1;
                uint16_t level = 0; mem::Read16(s + 0x0A, &level);
                out.push_back({ inst, row, level, bk[b].type, b, i, count, s });
            }
        }
        return static_cast<int>(out.size());
    }

    const char* InvTypeName(uint16_t type)
    {
        static const char* kNames[] = {
            "Money", "Bag", "PearlUser", "PearlCharacter", "Quest", "Wagon", "PetAndVehicle",
            "Private chest (camp storage)", "Warehouse", "Bank", "CampStraw", "Recovery", "Kuku", "InvisibleInventory",
            "Wardrobe (dresser)", "Refrigerator", "Housing_Symbol", "Housing_Collecting",
            "Housing_GatheredMaterials", "BirdFeed", "Ship",
        };
        return type < sizeof kNames / sizeof kNames[0] ? kNames[type] : "Unknown";
    }
}
