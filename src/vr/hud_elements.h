#pragma once
namespace trlvr
{
    // Per-element HUD layout ([vr] hud_health_*, hud_ammo_* and
    // hud_items_*): the health bar, the weapon / ammo readout and the item
    // wheel (the d-pad gear cross) are each moved, or hidden, as one piece
    // inside the game's own interface code, before the HUD is drawn, so the
    // rest of the HUD is unaffected; the whole-HUD hud_offset_x / _y applies
    // on top. While the settings panel is open the gameplay HUD is held in
    // view so the sliders show live. Installed once with the other engine
    // hooks.
    void hud_elements_init();
}
