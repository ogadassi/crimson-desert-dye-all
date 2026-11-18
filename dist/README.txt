Dye All 1.0.0 for Crimson Desert 1.0.0.3663

Ever wanted to go on a dyeing spree, not limited to what you have equipped? Dye everything you own
at the camp dyer, not only what you are wearing.

What it does
  At a camp dyer, open the dye screen. A panel on the right lists every dyeable item you own: bag,
  wardrobe, private chest and warehouse. Point at the panel to use it; the rest of the screen stays
  with the dyer.
  Double-click an item (or pick it and press "Wear it to dye"). The mod puts it on, and the dyer's
  list shows it right away, without leaving the menu. Dye it as usual.
  Dye many items in a row: pick colors, then simply pick the next item. The mod applies your colors
  first, like the Dye button, and the dye screen stays open. The dyer charges what it normally charges,
  once per item.
  Close the dye screen and everything goes back after 10 seconds: your own piece goes back on and each
  dyed item returns to where it came from. "Put back now" does it at once; choosing Dye again within
  the 10 seconds keeps the item on.

Item types
  Armor, cloaks, weapons and shields go on you. Horse gear goes on the horse the dyer shows (RB to the
  horse tab to dye it); A.T.A.G. parts go on your A.T.A.G. (its tab appears once you own one).
  Greyed in the list: crossbows (no character has a slot for them) and A.T.A.G. parts while you have
  no A.T.A.G. A weapon your current character cannot use is refused by the game and put straight back.
  The list sorts by Type (all helmets together, and so on), by Name, or by Location.

A full bag
  Works. The mod puts items on straight from storage. Should the game ever refuse that, it moves one
  dyeable piece from your bag to your private chest while you dye and brings it back afterwards.

Controls
  F7 shows the panel anywhere; F7 or Esc gives the mouse back to the game.
  If the dyer's list ever looks out of date, press Refresh in the panel (or RB then LB).

Safety
  The mod never makes an item dyeable that the game does not let you dye. It only uses the game's own
  move, equip, unequip and dye requests, one step at a time, and checks your inventories after each
  step. If a step fails it stops and says so: the item is then either worn or in your bag, never lost.
  DyeAll.log in the game's bin64 folder records every step; include it with any bug report.

Requirements
  Crimson Desert 1.0.0.3663. On another game version the mod checks the game's code before every hook
  and switches the affected part off instead of guessing.

Install (DMM)
  Drag DyeAll_1.0.0.zip onto DMM, enable DyeAll on the ASI tab, mount, start the game.

Remove
  Disable or remove DyeAll in DMM.

Credits
  Made with Claude (Anthropic's AI assistant).
  Overlay, input, memory and game-event code adapted from Master Looter by Seth (MIT), which adapted
  its overlay from Trinity by XeTrinityz (MIT). MinHook (BSD 2-clause), Dear ImGui (MIT).
  See LICENSE.MasterLooter.txt and THIRD_PARTY_NOTICES.md.
