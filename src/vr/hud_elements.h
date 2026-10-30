#pragma once
namespace trlvr
{
    // Per-element HUD layout ([vr] hud_items_* and hud_ammo_*): the item
    // wheel (the d-pad gear cross) and the weapon / ammo readout are each
    // moved, or hidden, as one piece inside the game's own interface code,
    // before the HUD is drawn, so the rest of the HUD and the global
    // hud_offset_x / _y are unaffected. While the settings panel is open the
    // gameplay HUD is held in view so the sliders show live. Installed once
    // with the other engine hooks.
    void hud_elements_init();
}
