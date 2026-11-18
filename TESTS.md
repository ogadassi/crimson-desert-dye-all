# Dye All 1.0.0: release tests

Install `dist/DyeAll_1.0.0.zip` with DMM (replace the test build), back up your save, then start the game.
Do the tests in order in one session if you can. Tests marked **(look)** need your eyes; the rest I can also
check in the log. When you're done, say "done" (and mention anything that looked wrong): I grade the log
with `tools/checklog.py`.

You need: one helmet in the wardrobe, two in the private chest, one item in the warehouse, a weapon and a
shield in storage, horse gear, A.T.A.G. parts, and a few silver.

## At the camp dyer

| # | Do this | Expect |
|---|---------|--------|
| 1 | Start the game, go to the camp dyer (daytime), choose Dye. | The panel appears on the right. Its title says **Dye All 1.0.0**. |
| 2 | **(look)** Try the three sort buttons: Type, Name, Location. | Type groups helmets together, with counts. Location groups by where things are kept. |
| 3 | Double-click a helmet from the **wardrobe**. | You put it on. The dyer's list shows it **without leaving the menu**. |
| 4 | Pick colors for it, then double-click a helmet from the **private chest**. | "Colors applied to …", then the new helmet goes on. The dyer stays open. |
| 5 | Double-click an item from the **warehouse**. | It goes on, or the panel says clearly why it can't. |
| 6 | **(look)** After each swap in 3–5, check the dyer's list. | It shows the piece you're wearing now. If not, press Refresh in the panel and note it. |
| 7 | Dye the chest helmet too, then pick the third helmet. | Colors applied again, with no "not confirmed" message. Note the silver charged. |
| 8 | Dye the third helmet, then pick the **first** helmet again. | All three keep their colors. You see the first helmet's new colors on you. |
| 9 | Close the dyer and wait 10 seconds. | Your own helmet goes back on. The borrowed ones return to the wardrobe or chest. |

## Item types

| # | Do this | Expect |
|---|---------|--------|
| 10 | Open the dyer again. Swap in a **weapon**, then a **shield**, then dye one of them. | Each goes on and dyes like armor. A weapon your character can't use goes straight back with a message. |
| 11 | Swap in a **saddle** (or other horse gear), then RB to the horse tab and dye it. | "Your horse wears …". It dyes on the horse tab. |
| 12 | Swap in **A.T.A.G. plating or boots**, then RB twice to the A.T.A.G. tab, dye it, and pick a second A.T.A.G. part. | The A.T.A.G. parts aren't greyed. "The A.T.A.G. wears …". Colors are applied before the second part. |
| 12b | **(look)** Hover a crossbow in the list. | It's greyed: no slot on any character. |

## Edge cases

| # | Do this | Expect |
|---|---------|--------|
| 13 | Fill your bag to the limit (or near it), then swap in a stored piece. | It goes on straight from storage. There's no "bag full" stop. |
| 14 | Swap in piece A, then piece B, then pick A again. | "A is on again", with no error. |
| 15 | Swap something in, close the dyer, and choose Dye again within 10 seconds. | The item is still on. Nothing went back. |
| 16 | **(look)** Swap something in and press **Put back now**. | It goes back right away. |
| 17 | **(look)** Away from the dyer, press F7, then Esc. | The panel takes the mouse, then gives it back. The game controls work normally. |
| 18 | **(look)** Save, quit to the menu, and load the save. | Every item is where it started, with the colors you gave it. Your own gear is on. |

## Then

Tell me "done" and anything that looked off (including the silver charged in test 7). If all pass, I write the
release notes and the Nexus page text, and you upload the zip.
