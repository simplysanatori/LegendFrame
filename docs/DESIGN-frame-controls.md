# Steam Frame controls: design

Status: implemented on `lane/controls` (Proposal A, D1 + D2 + D3.1) with the owner's decisions in
section F. Sections A to E are the design as drafted.

Based on `steam-frame` at 46a66c2. The Frame install uses `vr_runtime=openvr`
(`frame/install.sh:207`), so the SteamVR binding JSON and `ovr_runtime.cpp` are
the paths that matter. The owner's `trlvr.ini` has `immersive_controls=1`,
`first_person=0`, `gear_menu=radial` (immersive third person, radial gear menu).

## A. What the Frame controllers have

Source: `/opt/steamvr/drivers/frame_controller/resources/input/frame_controller_profile.json`
(names from `../localization/localization.json`).

| Input (driver path) | Hand | Kind | Notes |
|---|---|---|---|
| `/input/a`, `/input/b`, `/input/x`, `/input/y` | **right only** (`"side": "right"`) | button, click + touch | Xbox diamond: Y top, B right, A bottom, X left (`mirror_mappings`) |
| `/input/dpad_up/right/down/left` | **left only** | 4 **real buttons**, click + touch | Not a trackpad and not the stick's dpad mode. It mirrors the ABXY diamond (up=Y, right=B, down=A, left=X) |
| `/input/menu` ("Menu Button") | right | button, click + touch | Unbound by SteamVR's compositor, so the app can use it |
| `/input/view` ("View Button") | left | button, click + touch | The `oculus_touch` remap turns Touch's left menu into this (`remapping_mode: replace`) |
| `/input/system` ("Steam Button") | both | button | **Reserved**: click = dashboard, double = room view, held = chord, long = recentre (`vrcompositor_bindings_frame_controller.json`) |
| `/input/bumper` | both | button, click + touch | A shoulder button next to the trigger. The Touch remap fires it together with the trigger |
| `/input/trigger` | both | trigger, value + click + touch | |
| `/input/grip` | both | trigger, value + click + touch | |
| `/input/thumbstick` | both | joystick, click + touch | |
| `/input/thumbrest` | both | touch only | `SteamVRInternal`, so apps can't bind it |
| skeleton, haptic, poses | both | | handgrip / openxr_grip / aim etc. |

How SteamVR translates an `oculus_touch` binding (`frame_controller_remapping.json`, priority 95):
Touch left X becomes d-pad down; left Y becomes d-pad left/right/up; right A becomes A; right B becomes
**B, X and Y**; left menu becomes View. No OpenXR interaction profile for the Frame ships in `/opt/steamvr`.

## B. Today's mapping, and whether it was built for Touch

The manifest (`input/actions.json`, installed as `trlvr_actions.json`) only exposes **raw controller
slots** (`stick_click`, `primary`, `secondary`, `trackpad`, `menu`, touch sensors, per hand). The
game meaning of each slot lives in code: `update_logical_actions()` at `src/vr/vr_input.cpp:2185-2377`,
the key table at `:83-100`, and the gear keys at `:342`. Long press = 650 ms, double = 350 ms (`:2087`).

| Game verb | Key sent | Touch binding | Frame binding today | Owner's mode (immersive 3P) |
|---|---|---|---|---|
| Move (light push = walk) | W A S D (+ walk_vk) | L stick | L stick | |
| Camera / turn | mouse | R stick | R stick | smooth turn |
| Jump / surface; menu select | Space / Enter | R **A** | R A | |
| Crouch / roll / dive; menu back | F / Esc | R **B** | R B **and R X and R Y** | |
| Precision aim (a toggle, `:4244`) | Z | L **X** | L d-pad down | |
| Grenade / flare (tap) | K | L **Y** tap | L d-pad up/left/right, tap | |
| Personal light | Delete | L Y long | same d-pads, long | suppressed: left grip at the chest, or radial left |
| Board-mode finger flick (left hand) | (mod) | L Y held | same d-pads, held | |
| Action / interact / pull the grapple | E | LT | LT | |
| Fire; menu select | Mouse 1 / Enter | RT | RT | |
| Throw the grapple | Q | L grip | L grip | |
| Combat lock-on | G | R grip | R grip | |
| Weapon switch | End | R stick click | R stick click | suppressed (`:4110-4114`), radial down |
| PDA | Tab | R stick double | R stick double | |
| Binoculars | PgDn | R stick long | R stick long | long press = 1P/3P view switch; binoculars via radial right |
| Medipack | Home | radial up | radial up | radial up |
| Radial gear menu | Home/PgDn/End/Delete | hold L stick, push | same | same |
| Recentre | (mod) | L stick double | same | |
| Pause / settings panel | Esc / (mod) | L menu tap / hold 0.6 s | L **View** tap / hold | |
| (unbound) | | | **R Menu, both bumpers** | |

In binocular view the left stick zooms (H/G). Only the triggers (the view's own mode keys), pause,
recentre and medipack get through (`:4096-4101`). On the motorbike the stick steers and accelerates or
brakes (or the hands do, with `bike_handlebars` 2/3, `camera_head.cpp:5305`), and fire works as usual.
None of the buttons in this document matter on the bike. Every key the game exposes
(`english_key_name`, `input_labels.cpp:200-235`) already has a VR input. **No verb is missing, so this
is a placement problem only.**

**Verdict: the layout was built for Touch.** The Frame binding never got its own design:
- `bindings_frame_controller.json` is a mechanical copy of the Touch binding through SteamVR's
  remap (commit 39a42eb says so). That is why X and Y repeat Dive and three d-pad directions repeat grenade.
- `ovr_runtime.cpp:270-276` reports `frame_controller` as `XrControllerTouch`. The code comment at
  `vr_input.cpp:2348` says outright: "A / B on the right, X / Y ... on the left".
- The prompt labels (`input_labels.cpp:178-189`) say "Left X" and "Left Y". The Frame has no such buttons.
- `release/VR CONTROLLER BINDINGS.txt` also still says a left-stick *click* opens the gear menu. The code
  (`vr_input.cpp:2224`, `:4791`) opens it with a *hold* (after 150 ms).

So "leave as is" works, but it wastes R X, R Y, three d-pad buttons, R Menu and both bumpers, and the
prompts name buttons that don't exist.

## C. Proposed Frame layout

### C1. Proposal A, Frame-native (recommended)

| Frame input | Gameplay | Menus / overlay | How |
|---|---|---|---|
| R **A** | Jump / surface | select | unchanged |
| R **B** | Crouch / roll / dive | back | unchanged (drop the X/Y duplicates) |
| R **X** | Precision aim toggle (Z) | overlay X | JSON: `x` to `primary_left` |
| R **Y** | Grenade / flare tap (K); long = light (classic only); held = board flick | overlay Y | JSON: `y` to `secondary_left` |
| L **d-pad up** | Medipack (Home) | menu up | code |
| L **d-pad right** | Binoculars (PgDn) | menu right | code |
| L **d-pad down** | Weapon switch (End) | menu down | code |
| L **d-pad left** | Personal light (Delete) | menu left | code |
| R **Menu** | Pause tap / settings panel hold 0.6 s | Start / back | JSON: `menu` to `menu_left` |
| L **View** | PDA (Tab) | | code, 1 line |
| L stick | move; double = recentre; hold+push = radial gear menu (kept) | navigate | unchanged |
| R stick | camera; click / double / long as today | scroll | unchanged |
| LT / RT / L grip / R grip | action / fire / grapple / lock-on | | unchanged |
| Bumpers | left free (see E) | | |
| Steam buttons | SteamVR's | | never bind |

Why each one:
- **ABXY**: A and B keep the meanings every binding uses. X and Y simply move to where the Frame has
  them: the same verbs, with the same letters the prompts already use. Precision aim is a **toggle**, so
  pressing X with the camera thumb is fine.
- **D-pad = the game's own gear cross**, 1:1. The retail HUD cross, which the mod calls the "item wheel
  (the d-pad gear cross)" in `hud_elements.h`, and `kGearKeys` are both up medipack, right binoculars,
  down weapon, left light. The d-pad does the console's job: one press, no hold-and-push, no stick
  taken away from walking, and no 350 ms double-click wait on weapon switch. The radial menu stays for
  upstream parity. The d-pad should also flash the retail cross briefly (`camera_show_retail_gear_cross`),
  as the radial menu does.
- **Menu (right, "≡") = pause / settings**: this is where Start sits on a gamepad. **View (left) = PDA**:
  this is where Back/Select sits, the "map/journal" button.
- **Bumpers**: no verb is left without a button, so binding them only adds things to press by accident
  (the Touch remap fires them with the trigger).

Conflicts and edge cases:
- **Board flick**: it is the *left* hand's finger flick (`camera_head.cpp:21171`), but Y is now on the
  right thumb. It still works, but it feels odd. A Frame-only fix is the left bumper held (see E).
- **First person immersive**: upstream deliberately drops End / PgDn / K / Delete for holster and belt
  gestures (`vr_input.cpp:4103-4113`). A d-pad End or PgDn would bypass holster/belt state
  (`g_binocular_hand`). Recommended: in first person the d-pad keeps up (medipack) and left (light) and
  ignores down and right.
- **Binocular view**: allow d-pad right (PgDn, which closes it) and block the other three, matching the
  rule at `:4096`. Not verified in the headset.
- **Prompts vs reality** (until D3 is done): tutorials would say "Left X" / "Left Y" (should be X / Y),
  "Gear Cross" / "Left Stick Hold" for Home / End / PgDn (should be the d-pad), and "Chest + Left
  Grip" for Delete (should be d-pad left). The holster setup text "Press the menu button (left stick)"
  (`vr_submit.cpp:3226`) and "Menu button: stop here" still fit, as long as "menu" means the right Menu.
- **Vehicle, swimming, menus**: no changes beyond the d-pad also moving through menus.
- Users who customised the binding in SteamVR keep their cached copy until they re-select the supplied one.

### C2. Proposal B, conservative (JSON only, no code)

R X = precision aim, R Y = grenade (tap) / light (long). The d-pad stays as today (down = aim,
others = grenade), so Touch muscle memory still works. **Both** View and R Menu do pause / settings.
The d-pad then does no gear items, and the prompts still say "Left X" / "Left Y" unless D3 is done
anyway. This is a strict improvement over today with zero merge risk, but it doesn't use the d-pad,
which was most of the ask.

## D. Implementing it upstream-friendly

**D1. JSON (ours only; upstream never edits it):** in `input/bindings_frame_controller.json`, rebind
right `x` to `primary_left`, `y` to `secondary_left`, and right `menu` to `menu_left`. The action names are
just slots (`ovr_runtime.cpp:211-233`; `digital()` reads any source). Add the d-pad and View to the
new actions from D2. Proposal B is only this step.

**D2. Code, only for what no slot can express** (about 80-90 lines, all behind a Frame flag; the kind
stays `XrControllerTouch`, so every Touch code path and the tuning profiles are untouched):
1. `input/actions.json`: 5 optional booleans, `/actions/trlvr/in/dpad_up`, `dpad_right`, `dpad_down`,
   `dpad_left`, `view`, plus localisation strings. They are optional, so other controllers don't care.
   This is the one shared file. Upstream adding actions would mean a trivial JSON merge.
2. `ovr_runtime.cpp`: look up the 5 handles after the existing loop. Set `g_frame = true` in the existing
   `frame_controller` branch (`:270`). Export `ovr_input_frame()` / `ovr_input_frame_button(i)`.
   `xr_runtime.cpp` returns false (`oxr_` stubs). `vr_runtime.cpp` gets 2 `VR_FORWARD` lines.
   `xr_runtime.h` / `vr_backend.h` get the declarations.
3. `vr_input.cpp`: one new helper inside the anonymous namespace, `frame_dpad_update(want, menu)`,
   called from one line near the radial-menu block (`:4790`). Outside menus a press edge pulses
   `kGearKeys[dir]` through `want[]` for about 120 ms, exactly as the radial pick does (this bypasses the
   immersive-3P suppression on purpose), with the first-person and binocular rules from C1. In menus it
   ORs into the arrow keys (`:4418`). Plus one line in `update_logical_actions`:
   `if (frame) held[GamePda] |= view`.
4. Optional: if the owner picks it (E3), `held[kLogicalFlick]` comes from the left bumper on the Frame,
   which needs a sixth action, `bumper_left`.
Keeping the Frame logic in its own helper with single call lines keeps the merge-conflict surface in
`update_logical_actions` and `replacement()` (both hot upstream) to about 3 lines.

**D3. Prompts, by cost:**
1. **Cheap (recommended):** the mod already rewrites the game's coloured key token through the
   `GetButtonName` detour (`input_labels.cpp:20-23`, `replacement()` at `:112`). Add `VrControllerFrame`
   to `vr_input.h`, set it in `log_controllers()` (`vr_input.cpp:2396`) when the Frame flag is on, and
   add a Frame branch *before* the immersive cases: Space "A", F "B", Z "X", K "Y",
   Home / PgDn / End / Delete "D-pad Up/Right/Down/Left", Esc "Menu", Tab "View". New entries go in
   `g_labels`. About 25 lines. The prose stays untouched.
2. **Translations:** add the new label strings to `lang.cpp`. They fall back to English
   (`lang.cpp:472`), so this is optional.
3. **Expensive, not recommended:** Frame glyph icons. The tokens are drawn as game text, so this
   would need font or texture work.

Docs: rewrite the Frame section of `release/VR CONTROLLER BINDINGS.txt` and the README controls
table. The OpenXR runtime (upstream's default) keeps seeing the Frame through Touch emulation and is
unchanged.

## E. Decisions for the owner

1. **Pause on the right Menu with PDA on the left View (A), or keep pause on View?**
2. **D-pad in first person**: medipack + light only (recommended), all four, or none (upstream gestures)?
3. **Board-mode flick**: stay on Y (now the right thumb), or move to holding the left bumper (+1 action)?
4. **Bumpers**: leave them free (recommended), or give them duplicates (e.g. L bumper aim, R bumper grenade)?
5. **Scope**: Proposal A with the prompt fix (D1+D2+D3.1), or Proposal B first (JSON only)?

## F. Owner decisions, as implemented

1. Right Menu: pause on a tap, settings panel on a 0.6 s hold. Left View: PDA (Tab).
2. D-pad = the gear cross in **all** modes, menus = arrow keys, and it flashes the retail cross.
   In immersive first person, right goes through the belt state instead of the bare key: it sets
   `g_binocular_view` + `g_binocular_key_pulse` as raising the binoculars to the face does (not with
   a gun out, as at the belt; a view the game never showed is dropped after 1.5 s). Down sends End
   in first person too (second pass, after the headset test: the swap-only version did nothing when
   Lara had only the pistols, while End in third person showed the weapon selector). Retail weapon
   select goes through the inventory cycle `0x005B13C0` (the only gameplay writer of the requested
   slot `PlayerData+0x3E5` besides `playerInvSelectWeapon`): it steps a category list to the next
   slot Lara carries and requests it, and `playerInvProcess` swaps to it (animated with the guns out,
   at once otherwise); a non-weapon entry would go to the item slot `+0x3E2`; either way it triggers
   the weapon HUD fade groups 3 and 1, so with only the pistols the game just shows its selector.
   I found no grenade-type cycle on End (the pouch / K throw what Lara carries); the watcher logs
   `+0x3E2` if End ever changes it. `frame_weapon_select_follow` watches the press for 1.5 s:
   guns drawn, the hands follow the requested weapon (`g_holster_drawn` both for the long gun, the
   main hand only for single pistols); holstered, a pending request is made the selection at once
   (`select_weapon_kind(kind, false)`) so the hips still draw the pistols and the shoulder the long
   gun; if the game draws by itself (`+0x440 & 0x03000000` with the holsters empty) the holster
   state adopts it. Each outcome is a `[Frame] weapon select:` log line. In binocular view:
   right closes it (`binoculars_put_back`, or the key for a view opened by its key, as a grip does),
   up (medipack) works because the view lets the medipack through already, down and left are ignored.
3. Board flick on the Frame: left bumper held (`bumper_left`). Y keeps upstream's tap / long press.
4. Right bumper: the button holster (item 8). X precision aim, A jump, B crouch / roll / dive.
5. Prompts: Frame branch in `input_labels.cpp` (`frame_replacement`), holster setup cancel text.
   English only.
6. Quick draw (second pass; `[controls] frame_quick_draw`, default 1, Frame only). In immersive
   first person with both holsters empty and no gear in hand, the fire trigger (right; left when
   left-handed) or X draws the pistols through the hips' path (`select_weapon_kind(WeaponPistols,
   false)`, `g_holster_drawn`, `g_pistol_draw_hand`, `kPlayerInvEnterIndicatorMode`), so the hip grip
   still holsters them; single pistols draw only that hand's. The trigger is the natural input: it
   is the gun's own control, and with the guns holstered the fire trigger does nothing in first
   person (`GameFire` is dropped until a gun is drawn); the action trigger stays Interact. Not in
   menus, while hanging, with the grapple out (its pull owns the triggers), in binocular view,
   pointing at an ImGui menu, or when retail says combat is not allowed. The drawing pull is held
   back (`g_tuning_trigger_hold`) so it does not also fire. The aim then follows the hand as usual.
7. X in immersive first person (second pass): upstream drops it there because retail `Z` drew the
   guns straight into combat past the holster state (floating guns). On the Frame, X with the guns
   holstered is the quick draw above, and with a gun out a 120 ms `Z` pulse, the precision aim
   toggle the action trigger also gives. The owner's "head-tracked aim" was third-person X:
   precision aim (camera mode 13) with `hmd_aim` aims along the headset; that is unchanged. First
   person aims with the hand (`controller_aim = 1`); `controller_aim = 0` aims with the headset.
8. Button holster (third pass; `[controls] frame_holster_hold`, default 0.3 s, 0 = a tap, Frame
   only): "the trigger draws, the bumper holsters". The Frame's inside-out tracking loses the
   controllers at the hip and behind the shoulder, so the reaches are unreliable on this hardware.
   In immersive first person the right bumper (`bumper_right`) held for that long puts away
   whatever is drawn, once per press when the hold is reached (not on release), through the grips'
   own state: `g_holster_drawn` both cleared, `g_pistol_draw_hand` / `g_pistol_drawn_at` reset,
   `kPlayerInvEndCombatMode`, as the shoulder grip does for the long gun (two-handed or not: the
   foregrip lets go once the gun is gone) and the hips for the pistols (single-pistol mode: every
   drawn pistol). A light haptic on the right hand. Nothing happens with nothing drawn, in menus, in
   binocular view, hanging / climbing, with the grapple out or in hand, or in third person (board
   mode included); a bumper still held when input resumes (settings panel, calibration, a movie)
   waits for a new press. Third person stays as it was: retail has no holster key (the actions
   table sends none; the game ends combat itself). Each press logs `[Frame] holster:` with what
   went away or why nothing did. The reaches and the quick draw are unchanged.

Code: optional actions `dpad_up/right/down/left`, `view`, `bumper_left`, `bumper_right` in `input/actions.json`;
`xr_input_frame()` / `xr_input_frame_button()` (OpenVR only, false on OpenXR); the Frame block in
`vr_input.cpp` (`frame_logical_actions`, `frame_dpad_update`, `frame_thumb_button`,
`frame_overlay_buttons`, `frame_first_person_draw`; `frame_weapon_select_follow` runs inside
`frame_dpad_update`, `frame_bumper_holster` inside `frame_first_person_draw`), each called from one
line; `VrControllerFrame` for the labels.
