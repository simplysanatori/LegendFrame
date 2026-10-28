# lane/hud-elements status (WIP, interrupted by power-off)

Done: research only, no code changes yet. Branch = steam-frame ebe2789 + this file.

How elements can be identified (findings so far):
- The game ships tr7.pdb (full symbols + types) in the game folder. Parse it with pdbparse
  (needs construct 2.10.70 and small patches in tpi.py: type_refs.get(...), getattr(..., None),
  str(leaf_type) in rename_2_7). Parsing the TPI stream takes > 2 min on the Frame.
- The HUD is data-driven UI screens: UIScreenManager::DrawHUD / AddHUDScreen(UIScreenID),
  gHUDScreens; items UIItemGroup_Draw 0x004EC260, UIItemMeter_Draw 0x004EE4B0 (already hooked
  in camera_head.cpp). Each item draw gets UIDrawInfo& (size 224): +0x0 anchorX, +0x4 anchorY
  (floats), +0x8 maxAnchorY, +0xC drawAnimCount, +0x10 drawAnimStack.
- Fade groups: kHUDGearFadeGroup = 5 = gear cross ("item wheel"), kHUDHealthFadeGroup, etc.
- Today the mod only sees batched HUDDrawables per texture page at BasicDrawable::Draw
  (ui_space.cpp), so one draw can mix elements: classify at the UI-item level, not per draw.

Next steps:
1. Get addresses of UIScreenManager::DrawHUD and the per-screen draw from the PDB publics;
   dump UIScreen / UIItem layouts; find which HUD screen holds ammo (hudWeapon) and the gear
   cross (UIItemInventory / kHUDInventoryMenuID).
2. Hook the per-screen (or top-level group) draw: add a per-element offset to UIDrawInfo
   anchorX/anchorY (game pixels), optional scale, skip the draw to hide. Game-native, no tearing.
3. Keys [vr] hud_ammo_x/_y/_scale/_hide, hud_items_x/_y/_scale/_hide, hud_debug (log screen
   id + anchor once per change). Global hud_offset_x/y keep working on top.
4. Settings panel section (src/proxy/overlay.cpp slider() helper already writes the ini).
5. Seed Frame defaults in the frame/install.sh seeded-keys list (items right+up, ammo right+down).
6. Build with toolchain/build.sh, run stereotest.sh and mathtest; merge into steam-frame
   (now 9cc097c): replace the "HUD-ELEMENTS" placeholder line in README + docs/CHANGELOG.md, add
   the keys to the README Settings table; check why build.sh exited 1 for the other agent.
