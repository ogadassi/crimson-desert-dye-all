# CD Dye All: research findings

Game: Crimson Desert exe 1.0.0.3663 (2.05), Steam, `G:\SteamLibrary\steamapps\common\Crimson Desert`.
Installed mod stack: DMM (file overlay via DmmRuntime.asi), Ultimate ASI Loader (winmm.dll),
Outfit Manager, Crimson Collection (Master Looter based), Caravan Trade, Seasons.

## Archives

- `tools/paz.py` lists and extracts from the game's PAMT/PAZ archives (`python tools/paz.py list "ui/*dye*"`,
  `python tools/paz.py get "<glob>" extracted`).
- The PAMT index changed format in 2.05: name trees use a LEB128 **parent delta** (0 = root) instead of a
  u32 parent offset; a u32 file count precedes the 20-byte file records; names have no extension and the
  extension is `exts[flags >> 24]` from a table at the end of the file.
- Encryption is per file: `(flags >> 20) & 0xF == 3` means ChaCha20 (key from the basename, see paz.py).
  `(flags >> 16) & 0xF == 2` means LZ4 block.

## The dye screen

- UI source: `ui/xml/gamemain/play/dyeview.html` (+ `.css`), compiled into `ui/bin__/dyeview_game.uibinary`,
  which is what the game loads. Copies are in `extracted/0012/`.
- Controller: C++ class `pa::uiCommonScript::UIGamePlayControlRootDyeing`, vtable RVA `0x5ADB358`,
  init (vtable[1]) at RVA `0x1159780`. It binds its widgets by attribute-name IDs interned at static init
  (`0x180960..0x181080`). The item panel is `[this+0x238]`, bound from `selector-equipslot` to the widget
  `InventoryEquipmentPanel.EquippedEquipment_Default_Dye`: a panel of **equipped slots**, not an inventory list.
- `selector-inventory="#InventoryItemList"` in the HTML is vestigial: the dye controller never reads it
  (the only reader in that range belongs to the craft-transfer view). There is no hidden inventory mode.

## Dye requests (client -> game's internal server)

| Class | Server handler RVA | Payload |
|---|---|---|
| `TrocTrDyeItemListByEquipSlotReq` | `0x2CFD0F0` -> core `0x2F53A70` | 10 x { u16 equipSlot, pad, 12 x 16-byte dye slot record } + u32. This is what the camp dyer sends. |
| `TrocTrDyeItemByInventoryReq` | `0x2CFCB20` -> core `0x2E72C50` | u16 dyeItemInvKey, u16 dyeItemSlot, u16 targetInvKey, u16 targetSlot, then one dye record (u32, u8 slotNo, RGBA, u8, u16, u8). Consumes one dye **item** and dyes a target item in **any inventory** by (inventory type key, slot). |
| `TrocTrDyeMercenaryItemReq` | `0x2D216E0` | mercenary / horse gear |

- Inventory type keys go through the same table Master Looter documents (Money, Character, ..., CampWareHouse,
  WareHouse, ..., Housing_Dresser, ...). Item keys are the 0x28-byte key from Master Looter's INVENTORY.md.
- Per-item dye data lives in an array of 16-byte records at `itemSlot+0x78` (count at `+0x80`), matched by the
  record's byte 6 (dye slot number). Static data: `_isDyeable` on the item info, `PartPrefabDyeSlotInfo`.

## Conclusions so far

1. The engine can dye an item where it is stored (the consumable path proves it), so "dye from storage" is not
   blocked by the data model.
2. The camp dyer's screen only knows equip slots, both in its UI panel and in the request it sends, and its 3D
   preview shows what the character is wearing.
3. So a storage item has to either be worn for the preview (temporary swap, then put back), or the request and
   preview have to be redirected inside the game's code.

## Dyeable items

- `tools/build_dyeables.py` writes `plugin/data/dyeables.tsv` (917 of 7068 items on 3663) from `iteminfo`'s
  `is_dyeable` flag. Runtime item row = header order of iteminfo for base-game items.
- 2.05 `.paloc` string tables: `"paloc"` magic, u32 packed size at +9, u32 size at +13, LZ4 block from 0x200.

## Dye screen hooks (used by the plugin)

- `UIGamePlayControlRootDyeing` vtable slot 17 = opens (RVA `0x1159EA0`), slot 20 = closes (`0x115A1C0`),
  slot 36 = per-frame update (`0x115A680`, starts with a rel32 call, so not farhook-able).

## Item requests recorded in game (test 2, 2026-11-17)

Requests reach the internal server through `handler = descriptorVtable[2](desc, &result, msg)`; the payload is at
`[msg+0x18] + 5` (u16 id, FF, u16 len before it). They do NOT pass the OnceTimer enqueue at `+0x2A0F460`.
Equip/Unequip handlers start with a jump into protected code; chaining through it (farhook::InstallOverJump) works.

- `TrocTrMoveItemInventoryToOtherInventoryReq` (30 bytes): u32 itemKey, u16 itemLevel (= slot+0x0A),
  u64 count, u32 fromActor, u32 toActor (both A0100001), u16 fromInventoryKey (= type+1), u16 fromSlot,
  u32 destination index into the source inventory type's target list (wardrobe/chest -> bag 0, bag -> wardrobe 12,
  bag -> private chest 16). Core routine `+0x2E72810`.
- `TrocTrEquipItemReq` (6 bytes): u16 inventoryKey (2 = bag), u16 slot, u16 equip slot (3 = head).
- `TrocTrUnequipItemReq` (2 bytes): u16 equip slot.
- Inventory types: 1 bag, 7 private chest (camp storage), 8 warehouse, 14 wardrobe (dresser); key = type + 1.
- Slot record (stride 0xC8): +0 u64 instance (changes on every move), +8 u16 item row, +0x0A u16 item level,
  +0x10 u64 count.

## Swap results (tests 3-9, 2026-11-17)

- Sending requests: the mod builds the event exactly like Master Looter's OnceTimer sends (allocEvent, header,
  buffer = u16 id, FF, u16 len, body) and enqueues it on the game thread. Works for move/equip/unequip.
- The wardrobe only accepts items while the player is near it (taking out works anywhere). The camp dyer is near
  enough. Wardrobe capacity 210.
- Destination index = position in the source inventory type's target list: table global read from the lookup at
  `+0x57B410` (`[table+0x20]` entries of 0x18, `+8` -> object, `object+0x38` records of 0xA0, count `+0x40`;
  record u8 kind (3 = none), u16 +2 source type, u16 +4 destination type).
- Gear slots (equipslotinfo, same for Kliff/Damiane/Oongka): helm 3, upper body 4, hands 5, feet 6, cloak 16,
  glasses 17. Weapons differ per character.
- Full flow verified: take out, equip, close + reopen dye screen, dye (`DyeItemListByEquipSlotReq` 1584 bytes),
  close, re-equip old piece, move back to wardrobe; the dye is kept.
- While the dye screen is open the server applies the equip and the client receives `TrocTrEquipItemAck` within
  ~35 ms, but the dye screen's left list and preview character do not change until it is closed.
  - `SetDisplayMode(this, [this+0x443])` at `+0x115B810` (the game's own tab refresh): no visible change.
  - Calling the screen's own hide (slot 20) then show (slot 17): left menu disappears. Show = generic show
    `+0x1034B60`, widget reset `+0x1160000`, read equipped gear `+0x115A420`, tab rebuild.
  - Test 10 tries `+0x115A420` + tab rebuild (A), and close cleanup `+0x115FCD0` first (B).
- Dye screen's per-frame update calls `+0x1164C20` first (dye-only), used as the request pump while it is open.
- Move core routine `+0x2E72810` (10 args) writes the real move result to its 2nd argument.

## What RB / LB really do (test 16 recording, 2026-11-17)

Recorded with vtable-entry stubs on the dye screen and its widgets (`plugin/src/dye/trace.cpp`), read back
from the running game with `tools/ringdump.py` (dump in `research/trace_dump.txt`). The user's pad is not
visible to XInput reads, so the mod never saw the presses itself.

- RB: `TabBar::v115` (input) -> `NextIndex(bar, dir 1)` -> `Dyeing::v114(this, bar, ev 1 = check)` ->
  `ConfirmPendingMove(bar)` -> `NextIndex` -> `SelectTab(bar, 1, 1, 0)`. The direction byte is used, not
  only the pending index, which is why test 14 stayed on tab 0.
- About 250 ms later (header animation) `Dyeing::v114(this, bar, ev 0, prev, index)` -> `SetDisplayMode(index)`.
- SetDisplayMode asks for the preview character to be (re)loaded. When it has loaded, `+0x1167D30` runs:
  if `[this+0x443]` (shown tab) != `[this+0x445]` (tab the list was built for) it calls cleanup `+0x115FCD0`
  and the list rebuild `+0x1160000`, then sets 0x445 = 0x443 and updates each slot in the `+0x4F0` list.
- Test 11 (v114 to the other tab and back in one frame) reloaded the preview but left 0x443 == 0x445, so the
  list was not rebuilt. Test 17 sets 0x445 = 0xFE first.

## Applying colors (test 19)

- Picked colors live in a table at `[this+0x398]` (count `+0x39C`, entry pointers `+0x3B0`); the saved ones at
  `[this+0x3B8]` (count `+0x3BC`, entry pointers `+0x3D0`). Entry: `+4` i16 gear slot, `+8` 16-byte records,
  `+0x10` count. The game's own compare `+0x115CC30` dereferences null when a gear slot has no saved entry.
- Popup handler `+0x115E340(this, popup, button)`: popup `[this+0x450]` button 0 = confirm Dye: `[this+0x41C] = 6`,
  `+0x1161810(this)` (locks buttons), `+0x1162540(this, &result)` which builds the request from `+0x398` and sends
  it through `+0xACCAC0` -> `+0xD19020` (serializer, id 0x9B7).

## Keeping the dyer open (test 21, works)

`+0x1162540` ends with `+0x764F00(?, this+0x128, ?, "Close")` (lea r9 at `+0x11628E4`). The mod calls only
`+0x1162540` and drops that one "Close" UI event for the call; done = `TrocTrDyeItemListByEquipSlotAck`.
The open screen's saved table (`+0x3B8`) is not updated by the reply.

## Weapon slots (equipslotinfo, `tools/equipslots.py`)

Same slot per type for Kliff, Damiane and Oongka: one-handed sword/axe/mace, rapier, two-handed axe 0; shields 1;
bow, musket, shotgun, pistol, hand cannon 2; dagger 12; two-handed sword/greatsword/spear/halberd/cannon/
war hammer/hammer 13. Horse: helm 0, armor 1, saddle 2, stirrup 3, shoe 4. A.T.A.G.: arms 0/1, body 2,
backpack 3, feet 4.

## Horse and A.T.A.G. (tests 22-23)

- `TrocTrMercenaryEquipItemReq` (recorded): u64 mercenary key, u16 inventory key (2 = bag), u16 bag slot, u16 gear slot.
- `TrocTrMercenaryUnequipItemReq` (handler `+0x2CF3780` reads 8 + 2 bytes): u64 mercenary key, u16 gear slot.
- Mercenaries: player actor `[[[exe+0x72451C0]+0x30]+0x50]`, manager `[[actor+0x68]+0x110]`, entry pointers `+0x18`,
  count `+0x20` (115 on the test save); entry `+0x24` u16 character, `+0x28` u64 key, `+0x50` entity id when spawned,
  `+0xEE` unusable flag (`tools/peek_mercs.py`). The user's horse: key 7164, character 4111.
- The dyer picks its horse / A.T.A.G. in SetDisplayMode (mode 1 / 2) with `+0x2387AF0(mgr, category, playerChar, type)`
  and fallback `+0x2387A30(mgr, category, type)` (category 1, horse type 18, A.T.A.G. type 2 from the static globals);
  the mod watches those inside SetDisplayMode and passes its in-place refresh through tab 1 or 2 when it needs a key.
- The A.T.A.G. tab exists only when `[this+0x442]` is set, at screen open (`+0x115A430`): the player actor
  (`[g+0x30]+0x50`) must equal `[g+0x30]+0x58` (not controlling another body), and `+0x19AA2B0` (the player's
  mercenaries of the dyer category) must hold one whose character info `+0xA0` equals the A.T.A.G. type
  (`[[G+8]+0x178]+0x7A` = 2). Owning A.T.A.G. parts is not enough; the user's save showed no A.T.A.G. tab (2026-11-17).

## Reusable community work (MIT)

- lazorr410/crimson-desert-unpacker: archive crypto and the older PAMT layout.
- shin2344234/master-looter: signatures for the event system (descriptor lookup, event alloc, enqueue, game-thread
  pump), sending `TrocTr*` events, reading every inventory on the holder, the inventory-type table.
