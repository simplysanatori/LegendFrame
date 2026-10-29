#pragma once
namespace trlvr
{
    // Per-element HUD layout ([vr] hud_items_* and hud_ammo_*): the item
    // wheel (the d-pad gear cross) and the ammo counter are moved, resized
    // or hidden inside the game's own interface code, before the HUD is
    // drawn, so the rest of the HUD and the global hud_offset_x / _y are
    // unaffected. Installed once with the other engine hooks.
    void hud_elements_init();
}
