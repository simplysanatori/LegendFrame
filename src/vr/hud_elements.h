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

    // A mod message in the game's own tutorial hint window: same box,
    // font, place, fade and sound as the game's hints. `text` is UTF-8;
    // U+1800 to U+1818 are the game's coloured action tokens, turned into
    // the current button names like in the game's hints (U+180F, UTF-8
    // "\xE1\xA0\x8F", is precision aim). The text is kept by the game
    // until the next hint, so it must stay valid (a string literal).
    // Never interrupts: while a game hint is shown (or fading out) it does
    // nothing and returns kGameHintBusy; the caller may try again later.
    // Call on the game thread (from Present), in a level.
    enum GameHintResult
    {
        kGameHintShown,
        kGameHintBusy,          // a game hint is up; nothing shown
        kGameHintUnavailable,   // not this exe, or the game call failed
    };
    GameHintResult hud_show_game_hint(const char* text);
}
