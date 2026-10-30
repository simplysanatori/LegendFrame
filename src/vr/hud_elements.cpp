// Per-element HUD layout: the item wheel and the ammo counter, each moved
// (or hidden) as one piece, and the gameplay HUD held in view while the
// settings panel is open so the sliders can be set live.
//
// The mod only ever sees the HUD as batched draws, one per texture page
// (ui_space.cpp), so one draw can hold parts of several elements. The split
// is made earlier, in the game's data-driven interface (tr7.pdb types,
// addresses remapped to this build and checked below):
//
//   Every interface item starts with { u16 itemFlags; u16 itemType;
//   UIItemFnTable* fnTable; }. Each item type has one static fnTable in
//   .data, and every draw goes through fnTable->drawFn(item, UIDrawInfo&).
//   Groups (type 1) hold child items, as do selectors (10, 11), meters and
//   sliders. The player HUD (UIScreenID 5000, kUIScreenPlayerHUD)
//   is one screen; its backgroundItems group (UIScreen +0xC) is the root of
//   every element.
//
//   The visible items carry one or two fade groups (UIFadeGroupType; the
//   alpha is the larger of the two): 1 health, 2 breath, 3 weapons, 4 gear,
//   5 inventory selection (the d-pad gear cross), 6 parachute, 7 / 8 hints,
//   9 / 10 / 11 health / grenade / ammo pickups, 12 checkpoint, 13 super
//   action, 14 the cross's low-light warning, 15 low health, 16 binoculars
//   (the cross's right-hand slot), 17 controller.
//
//   Each leaf draw passes its anchor through UIDrawInfo::GetTransXYZ
//   (0x004EBC20). Anchors are fractions of the screen (0 to 1, x right,
//   y down).
//
// Elements are whole subtrees of the player HUD tree, found by structure:
// the tree is walked from its root, and the first item (from the top) whose
// subtree's "primary" fade groups all belong to one element becomes a root
// of that element -- 5, 14, 16 the item wheel; 3, 4 the weapon and gear
// readout with the ammo numbers. Pickup flashes and the low-health warning
// (9, 10, 11, 15) are not primary: they ride inside the element that holds
// them. Every other primary group (health, hints, ...) keeps its subtree
// where it is. A group holding more than one element is split one level
// down, and so on. A subtree under a split group with only pickup / warning
// groups joins the element whose root sits next to it on screen (within
// kJoinReach); everything else stays. While a root draws, GetTransXYZ is
// hooked to add the element's offset to every anchor below it, so the whole
// element moves rigidly whatever the size of its roots. A hidden element's
// roots are simply not drawn. Nothing outside the player HUD is touched.
//
// hud_debug = 1 dumps the whole tree with this classification once per
// HUD (trlvr.log, "hud elements: tree"), so a wrong pick can be corrected.
//
// Held in view: the settings panel does not pause the game; it only takes
// the controllers, so with no input the HUD's fade groups run out their hold
// time and fade away. While the panel is open, the fade timers of health,
// weapons, gear and the gear cross (UIFadeGroupData, see hold_hud_visible)
// are kept inside their hold window and their alphas at 1, every frame,
// before the HUD draws. When it closes they hold a moment, then fade as
// usual. Items the game hides by its own rules (a hide function, an empty
// weapon selector) stay hidden.

#include "hud_elements.h"

#include "camera_head.h"
#include "hook.h"

#include "../common/config.h"
#include "../common/log.h"
#include "../proxy/overlay.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace trlvr
{
    namespace
    {
        enum Element
        {
            kNone = 0,
            kItems = 1,   // the item wheel (d-pad gear cross)
            kAmmo = 2,    // the weapon / ammo readout
            kElementCount = 3,
        };

        constexpr unsigned bit(unsigned id) { return 1u << id; }

        const unsigned kFadeCount = 0x12;
        const unsigned kItemsPrimary = bit(5) | bit(14) | bit(16);
        const unsigned kAmmoPrimary = bit(3) | bit(4);
        const unsigned kOtherPrimary = bit(1) | bit(2) | bit(6) | bit(7) |
                                       bit(8) | bit(12) | bit(13) | bit(17);
        const unsigned kPrimary = kItemsPrimary | kAmmoPrimary | kOtherPrimary;
        const unsigned kSecondary = bit(9) | bit(10) | bit(11) | bit(15);
        // How far (screen fractions, per axis) a pickup / warning subtree may
        // sit from an element's root to join it.
        const float kJoinReach = 0.12f;

        // One fnTable per item type (UIItemType 1..15), with the drawFn each
        // must hold before it is swapped (read from trl.exe; the init
        // switch at 0x004EBE20 sets item->fnTable).
        struct TypeTable
        {
            uintptr_t table;
            uintptr_t draw;
            const char* name;
        };
        const TypeTable kTables[15] = {
            { 0x00F169DC, 0x004EC260, "group" },
            { 0x00F16C6C, 0x004F24A0, "static text" },
            { 0x00F16CE4, 0x004F2650, "dynamic text" },
            { 0x00F16D6C, 0x004F33D0, "text selector" },
            { 0x00F16CA8, 0x004F25F0, "edit text" },
            { 0x00F16B24, 0x004EE740, "model" },
            { 0x00F16A5C, 0x004ECDA0, "image" },
            { 0x00F16B6C, 0x004EF6B0, "rect" },
            { 0x00F16AE4, 0x004EE4B0, "meter" },
            { 0x00F16A1C, 0x004ECA30, "group selector" },
            { 0x00F16A9C, 0x004ED6A0, "inventory selector" },
            { 0x00F16C2C, 0x004F1360, "slider" },
            { 0x00F16BAC, 0x004F0270, "rounded rect" },
            { 0x00F16BEC, 0x004F1110, "rect outline" },
            { 0x00F16D2C, 0x004F2E00, "text log" },
        };
        const unsigned kDrawFnOffset = 0x10;

        using PFN_Draw = int(__cdecl*)(void*, void*);
        PFN_Draw g_original[15] = {};

        // UIDrawInfo::GetTransXYZ(float*, float*, float*), thiscall on the
        // UIDrawInfo.
        const uintptr_t kGetTransXYZ = 0x004EBC20;
        const unsigned char kGetTransSignature[7] =
            { 0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x77, 0x0C };
        typedef void(__thiscall* PFN_GetXYZ)(void*, float*, float*, float*);
        PFN_GetXYZ g_get_trans = nullptr;

        // UIScreenManager::gHUDScreens (3 UIScreen*) and sHUDScreenCount;
        // UIScreen { short screenID; ...; UIItemGroup* backgroundItems
        // (+0xC); }.
        const uintptr_t kHUDScreens = 0x011161D0;
        const uintptr_t kHUDScreenCount = 0x011161F8;
        const short kPlayerHUD = 5000;

        // The fade groups (UIFadeGroupTrigger 0x004EB1D0, UpdateFadeGroup
        // 0x004EB340): [0x010C0A58] +0x14 count, +0x18 UIFadeGroupData*[]
        // { u8 fadeGroupID; ...; float fadeInTimeEnd (+4), holdOnTimeEnd
        // (+8), fadeOutTimeEnd (+0xC), fadeTimer (+0x10); }, times scaled by
        // the float at 0x00EFE508; gFadeGroupValues (the alphas) at
        // 0x011137B8.
        const uintptr_t kFadeGlobals = 0x010C0A58;
        const uintptr_t kFadeTimeScale = 0x00EFE508;
        const uintptr_t kFadeValues = 0x011137B8;
        // Held in view while the settings panel is open.
        const unsigned kHeldFades = bit(1) | bit(3) | bit(4) | bit(5);

        // --- items -------------------------------------------------------

        unsigned item_type(const unsigned char* item)
        {
            return *reinterpret_cast<const unsigned short*>(item + 2);
        }

        bool valid_item(const unsigned char* item)
        {
            const unsigned type = item_type(item);
            return type >= 1 && type <= 15 &&
                   *reinterpret_cast<const uintptr_t*>(item + 4) ==
                       kTables[type - 1].table;
        }

        unsigned fade_bit(unsigned char id)
        {
            return id > 0 && id < kFadeCount ? 1u << id : 0u;
        }

        // The fade groups of one item itself (not its children).
        unsigned own_fade_mask(const unsigned char* item, unsigned type)
        {
            switch (type)
            {
            case 2: case 3: case 5:     // UIItemStaticText and derived
                return fade_bit(item[8]) | fade_bit(item[9]);
            case 6:                     // UIItemModel
            case 9:                     // UIItemMeter
                return fade_bit(item[8]);
            case 8:                     // UIItemRect
                return fade_bit(item[0xD]) | fade_bit(item[0xE]);
            case 13: case 14:           // UIItemRoundedRect, RRectOutline
                return fade_bit(item[0xA]) | fade_bit(item[0xB]);
            default:
                return 0;
            }
        }

        int child_count(const unsigned char* item, unsigned type)
        {
            switch (type)
            {
            case 1:                     // UIItemGroup: itemCount, items[]
            {
                const int n = *reinterpret_cast<const int*>(item + 0x20);
                return n > 0 && n <= 256 ? n : 0;
            }
            case 9:                     // UIItemMeter: backgroundItems
            case 12:                    // UIItemSlider: sliderItem
                return 1;
            case 10:                    // UIItemGroupSelector
            case 11:                    // UIItemInventorySelector
            {
                const int n = *reinterpret_cast<const short*>(item + 0xA);
                return n > 0 && n <= 64 ? n : 0;
            }
            default:
                return 0;
            }
        }

        const unsigned char* child_at(const unsigned char* item, unsigned type,
                                      int index)
        {
            typedef const unsigned char* Ptr;
            switch (type)
            {
            case 1:
                return reinterpret_cast<const Ptr*>(item + 0x24)[index];
            case 9:
                return *reinterpret_cast<const Ptr*>(item + 0x48);
            case 12:
                return *reinterpret_cast<const Ptr*>(item + 0x28);
            case 10:                    // { UIItemGroup* group; float value; }
            {
                const Ptr sel = reinterpret_cast<const Ptr*>(item + 0x1C)[index];
                return sel ? *reinterpret_cast<const Ptr*>(sel) : nullptr;
            }
            case 11:                    // { short type, subType; UIItem* }
            {
                const Ptr sel = reinterpret_cast<const Ptr*>(item + 0xC)[index];
                return sel ? *reinterpret_cast<const Ptr*>(sel + 4) : nullptr;
            }
            default:
                return nullptr;
            }
        }

        // Where an item keeps its own anchorX / anchorY (0: it has none).
        unsigned anchor_offset(unsigned type)
        {
            switch (type)
            {
            case 1: return 0x14;        // UIItemGroup
            case 2: case 3: case 5: return 0x10;
            case 4: return 0x18;        // UIItemTextSelector
            case 6: return 0x10;        // UIItemModel
            case 7: return 0x0C;        // UIItemImage
            case 8: return 0x1C;        // UIItemRect
            case 9: return 0x14;        // UIItemMeter
            case 10: return 0x0C;       // UIItemGroupSelector
            case 12: return 0x0C;       // UIItemSlider (start)
            case 13: case 14: return 0x14;
            case 15: return 0x10;       // UIItemTextLog
            default: return 0;
            }
        }

        // UIItem::GetAnchor (0x004EBED0): itemFlags 0x1000 adds the
        // parent's anchor; an own 0 takes the parent's.
        void resolve_anchor(const unsigned char* item, unsigned type,
                            float px, float py, float* x, float* y)
        {
            const unsigned offset = anchor_offset(type);
            if (!offset)
            {
                *x = px;
                *y = py;
                return;
            }
            const float gx = *reinterpret_cast<const float*>(item + offset);
            const float gy = *reinterpret_cast<const float*>(item + offset + 4);
            const bool relative =
                (*reinterpret_cast<const unsigned short*>(item) & 0x1000) != 0;
            *x = relative ? gx + px : gx == 0.0f ? px : gx;
            *y = relative ? gy + py : gy == 0.0f ? py : gy;
        }

        bool has_own_anchor(const unsigned char* item, unsigned type)
        {
            const unsigned offset = anchor_offset(type);
            return offset &&
                   (*reinterpret_cast<const float*>(item + offset) != 0.0f ||
                    *reinterpret_cast<const float*>(item + offset + 4) != 0.0f);
        }

        // --- the player HUD tree -----------------------------------------

        enum Role
        {
            kInside = 0,    // below a root, a split or a kept subtree
            kStay,          // its own subtree stays where it is
            kSplit,         // holds more than one element: its children decide
            kRoot,          // the top of an element's subtree
            kJoin,          // a pickup / warning subtree next to an element
        };

        struct Node
        {
            const unsigned char* item;
            short parent;
            unsigned char type, depth;
            unsigned own, sub;          // fade groups: own, whole subtree
            float sx, sy;               // anchor from the data
            float rx, ry;               // anchor when last drawn
            bool rvalid, own_anchor;
            unsigned char role, element;
        };

        const int kMaxNodes = 512;
        struct Tree
        {
            Node nodes[kMaxNodes];
            int count;
            bool truncated;
            uint32_t signature;
        };
        Tree g_tree = {};
        Tree g_scratch = {};
        // item pointer -> node index + 1, open addressing.
        const unsigned kIndexSize = 1024;
        short g_index[kIndexSize] = {};

        unsigned hash_slot(const void* item)
        {
            return (unsigned)(((uintptr_t)item >> 2) * 2654435761u) &
                   (kIndexSize - 1);
        }

        int add_node(Tree& t, const unsigned char* item, int parent, int depth,
                     float px, float py)
        {
            if (!item || depth > 12)
                return -1;
            if (t.count >= kMaxNodes)
            {
                t.truncated = true;
                return -1;
            }
            if (!valid_item(item))
                return -1;
            const int index = t.count++;
            Node& n = t.nodes[index];
            memset(&n, 0, sizeof(n));
            n.item = item;
            n.parent = (short)parent;
            n.type = (unsigned char)item_type(item);
            n.depth = (unsigned char)depth;
            n.own = own_fade_mask(item, n.type);
            n.own_anchor = has_own_anchor(item, n.type);
            resolve_anchor(item, n.type, px, py, &n.sx, &n.sy);
            unsigned sub = n.own;
            const int children = child_count(item, n.type);
            for (int i = 0; i < children; ++i)
            {
                const int c = add_node(t, child_at(item, n.type, i), index,
                                       depth + 1, n.sx, n.sy);
                if (c >= 0)
                    sub |= t.nodes[c].sub;
            }
            t.nodes[index].sub = sub;
            return index;
        }

        bool walk(Tree& t, const unsigned char* root)
        {
            t.count = 0;
            t.truncated = false;
            volatile bool ok = false;
            __try
            {
                add_node(t, root, -1, 0, 0.0f, 0.0f);
                ok = true;
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
            }
            uint32_t h = 2166136261u;
            for (int i = 0; i < t.count; ++i)
            {
                const uint32_t parts[3] = {
                    (uint32_t)(uintptr_t)t.nodes[i].item,
                    (uint32_t)t.nodes[i].type,
                    (uint32_t)(int)t.nodes[i].parent,
                };
                for (uint32_t v : parts)
                    for (int b = 0; b < 4; ++b)
                        h = (h ^ ((v >> (8 * b)) & 0xFF)) * 16777619u;
            }
            t.signature = h ^ (uint32_t)t.count;
            return ok && t.count > 0;
        }

        void place(const Node& n, float* x, float* y)
        {
            *x = n.rvalid ? n.rx : n.sx;
            *y = n.rvalid ? n.ry : n.sy;
        }

        void classify_node(Tree& t, int index)
        {
            Node& n = t.nodes[index];
            const unsigned primary = n.sub & kPrimary;
            if (!primary)
            {
                n.role = kStay;
                return;
            }
            if (!(primary & ~kItemsPrimary))
            {
                n.role = kRoot;
                n.element = kItems;
                return;
            }
            if (!(primary & ~kAmmoPrimary))
            {
                n.role = kRoot;
                n.element = kAmmo;
                return;
            }
            if (!(primary & (kItemsPrimary | kAmmoPrimary)))
            {
                n.role = kStay;         // health, hints, ...: none of ours
                return;
            }
            bool any = false;
            for (int c = index + 1; c < t.count && t.nodes[c].depth > n.depth;
                 ++c)
                if (t.nodes[c].parent == index)
                {
                    any = true;
                    classify_node(t, c);
                }
            n.role = any ? kSplit : kStay;
        }

        void classify(Tree& t)
        {
            for (int i = 0; i < t.count; ++i)
            {
                t.nodes[i].role = kInside;
                t.nodes[i].element = kNone;
            }
            if (!t.count)
                return;
            classify_node(t, 0);
            // Pickup flashes and warnings beside an element join it.
            for (int i = 0; i < t.count; ++i)
            {
                Node& n = t.nodes[i];
                if (n.role != kStay || n.parent < 0 ||
                    t.nodes[n.parent].role != kSplit ||
                    !(n.sub & kSecondary) || (n.sub & kPrimary) ||
                    !n.own_anchor)
                    continue;
                float x, y;
                place(n, &x, &y);
                int best = -1;
                float best_d = kJoinReach;
                for (int j = 0; j < t.count; ++j)
                {
                    if (t.nodes[j].role != kRoot)
                        continue;
                    float rx, ry;
                    place(t.nodes[j], &rx, &ry);
                    const float d = fmaxf(fabsf(x - rx), fabsf(y - ry));
                    if (d <= best_d)
                    {
                        best_d = d;
                        best = j;
                    }
                }
                if (best >= 0)
                {
                    n.role = kJoin;
                    n.element = t.nodes[best].element;
                }
            }
        }

        void build_index()
        {
            memset(g_index, 0, sizeof(g_index));
            for (int i = 0; i < g_tree.count; ++i)
            {
                unsigned slot = hash_slot(g_tree.nodes[i].item);
                while (g_index[slot])
                {
                    if (g_tree.nodes[g_index[slot] - 1].item ==
                        g_tree.nodes[i].item)
                        break;          // shared item: the first one wins
                    slot = (slot + 1) & (kIndexSize - 1);
                }
                if (!g_index[slot])
                    g_index[slot] = (short)(i + 1);
            }
        }

        Node* find_node(const void* item)
        {
            unsigned slot = hash_slot(item);
            while (g_index[slot])
            {
                Node& n = g_tree.nodes[g_index[slot] - 1];
                if (n.item == item)
                    return &n;
                slot = (slot + 1) & (kIndexSize - 1);
            }
            return nullptr;
        }

        // --- settings ----------------------------------------------------

        struct Settings
        {
            float dx, dy;
            bool hide;
        };

        Settings settings_for(int element)
        {
            const Config& c = config();
            // ini units are hud_offset's (NDC: 2 = the whole view, +y up);
            // the interface works in screen fractions with y down.
            if (element == kItems)
                return { c.hud_items_x * 0.5f, -c.hud_items_y * 0.5f,
                         c.hud_items_hide };
            return { c.hud_ammo_x * 0.5f, -c.hud_ammo_y * 0.5f,
                     c.hud_ammo_hide };
        }

        bool neutral(const Settings& s)
        {
            return s.dx == 0.0f && s.dy == 0.0f && !s.hide;
        }

        // --- hud_debug ---------------------------------------------------

        const char* element_name(int element)
        {
            return element == kItems ? "item wheel"
                 : element == kAmmo ? "ammo" : "none";
        }

        void fade_list(unsigned mask, char* out, size_t size)
        {
            int n = 0;
            out[0] = '\0';
            for (unsigned id = 1; id < kFadeCount && n < (int)size - 4; ++id)
                if (mask & (1u << id))
                    n += snprintf(out + n, size - n, "%s%u", n ? "," : "",
                                  id);
            if (!n)
                snprintf(out, size, "-");
        }

        void copy_name(const unsigned char* src, char* out, size_t size)
        {
            size_t i = 0;
            for (; i + 1 < size && i < 32 && src[i]; ++i)
                out[i] = src[i] >= 0x20 && src[i] < 0x7F ? (char)src[i] : '?';
            out[i] = '\0';
        }

        // What an item shows, for the tree dump.
        void describe(const Node& n, char* out, size_t size)
        {
            const unsigned char* item = n.item;
            out[0] = '\0';
            char name[40];
            switch (n.type)
            {
            case 1:
                snprintf(out, size, "%d items, hide fn %d/%d",
                         *reinterpret_cast<const int*>(item + 0x20),
                         *reinterpret_cast<const short*>(item + 0x8),
                         *reinterpret_cast<const short*>(item + 0xA));
                break;
            case 2: case 3: case 5:
                snprintf(out, size, "text %d",
                         *reinterpret_cast<const short*>(item + 0xA));
                if (n.type == 5)
                    snprintf(out + strlen(out), size - strlen(out),
                             ", game value %d",
                             *reinterpret_cast<const short*>(item + 0x26));
                break;
            case 4: case 10:
                snprintf(out, size, "game value %d",
                         *reinterpret_cast<const short*>(item + 0x8));
                break;
            case 6:
                copy_name(item + 0x24, name, sizeof(name));
                snprintf(out, size, "model \"%s\"", name);
                break;
            case 7:
                copy_name(item + 0x30, name, sizeof(name));
                snprintf(out, size, "image \"%s\"", name);
                break;
            case 8:
                snprintf(out, size, "hide fn %d/%d",
                         *reinterpret_cast<const short*>(item + 0x14),
                         *reinterpret_cast<const short*>(item + 0x16));
                break;
            case 9:
                snprintf(out, size, "game value %d",
                         *reinterpret_cast<const short*>(item + 0xC));
                break;
            case 11:
            {
                const unsigned flags =
                    *reinterpret_cast<const unsigned short*>(item + 0x8);
                int n_out = snprintf(out, size, "%s, inventory types",
                                     flags & 0x8000 ? "weapons" : "gear");
                const int count = child_count(item, 11);
                for (int i = 0; i < count && i < 8 && n_out < (int)size - 8;
                     ++i)
                {
                    const unsigned char* sel = reinterpret_cast<
                        const unsigned char* const*>(item + 0xC)[i];
                    if (sel)
                        n_out += snprintf(out + n_out, size - n_out, " %d.%d",
                            *reinterpret_cast<const short*>(sel),
                            *reinterpret_cast<const short*>(sel + 2));
                }
                break;
            }
            default:
                break;
            }
        }

        void dump_tree(const Tree& t)
        {
            log("hud elements: tree of the player HUD, %d items%s (fade "
                "groups own/subtree; anchor as drawn, or * from the data; "
                "-> element)", t.count, t.truncated ? ", cut short" : "");
            for (int i = 0; i < t.count; ++i)
            {
                const Node& n = t.nodes[i];
                char own[48], sub[48], extra[128];
                fade_list(n.own, own, sizeof(own));
                fade_list(n.sub, sub, sizeof(sub));
                extra[0] = '\0';
                __try
                {
                    describe(n, extra, sizeof(extra));
                }
                __except(EXCEPTION_EXECUTE_HANDLER)
                {
                    snprintf(extra, sizeof(extra), "?");
                }
                float x, y;
                place(n, &x, &y);
                const char* role = "";
                char role_text[40];
                switch (n.role)
                {
                case kStay: role = " -> stays"; break;
                case kSplit: role = " -> split"; break;
                case kRoot:
                    snprintf(role_text, sizeof(role_text), " -> %s",
                             element_name(n.element));
                    role = role_text;
                    break;
                case kJoin:
                    snprintf(role_text, sizeof(role_text),
                             " -> %s (beside it)", element_name(n.element));
                    role = role_text;
                    break;
                default: break;
                }
                const int indent = n.depth < 12 ? n.depth * 2 : 24;
                log("hud elements: tree %*s%s %p, fade %s/%s, at %.3f,%.3f%s"
                    "%s%s%s", indent, "", kTables[n.type - 1].name,
                    (const void*)n.item, own, sub, x, y, n.rvalid ? "" : "*",
                    extra[0] ? ", " : "", extra, role);
            }
        }

        void summary(const Tree& t, const char* when)
        {
            int roots[kElementCount] = {}, joins[kElementCount] = {};
            for (int i = 0; i < t.count; ++i)
            {
                if (t.nodes[i].role == kRoot)
                    ++roots[t.nodes[i].element];
                else if (t.nodes[i].role == kJoin)
                    ++joins[t.nodes[i].element];
            }
            log("hud elements: player HUD %s: %d items; item wheel %d "
                "subtree(s) + %d beside it, ammo %d + %d; the rest stays",
                when, t.count, roots[kItems], joins[kItems], roots[kAmmo],
                joins[kAmmo]);
        }

        const char* screen_name(int id)
        {
            switch (id)
            {
            case 5000: return "player HUD";
            case 5001: return "binoculars HUD";
            case 5002: return "vehicle HUD";
            case 5003: return "cinematic HUD";
            default: return "screen";
            }
        }

        void debug_screens()
        {
            static int last[4] = { -1, -1, -1, -1 };
            int now[4] = { 0, 0, 0, 0 };
            __try
            {
                int count = *reinterpret_cast<const int*>(kHUDScreenCount);
                if (count < 0)
                    count = 0;
                if (count > 3)
                    count = 3;
                now[0] = count;
                const unsigned char* const* screens =
                    reinterpret_cast<const unsigned char* const*>(kHUDScreens);
                for (int i = 0; i < count; ++i)
                    now[1 + i] = screens[i]
                        ? *reinterpret_cast<const short*>(screens[i]) : 0;
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
                return;
            }
            if (memcmp(now, last, sizeof(now)) == 0)
                return;
            memcpy(last, now, sizeof(now));
            char text[160];
            int n = snprintf(text, sizeof(text), "%d:", now[0]);
            for (int i = 0; i < now[0]; ++i)
                n += snprintf(text + n, sizeof(text) - n, " %d (%s)",
                              now[1 + i], screen_name(now[1 + i]));
            log("hud elements: HUD screens %s", text);
        }

        // --- the HUD held in view ----------------------------------------

        void hold_hud_visible()
        {
            const Config& c = config();
            unsigned want = kHeldFades;
            if (c.hud_ammo_hide)
                want &= ~kAmmoPrimary;
            if (c.hud_items_hide || camera_retail_gear_cross_hidden())
                want &= ~bit(5);
            volatile unsigned held = 0;
            __try
            {
                const unsigned char* globals =
                    *reinterpret_cast<const unsigned char* const*>(
                        kFadeGlobals);
                if (!globals)
                    return;
                const int count = *reinterpret_cast<const int*>(globals + 0x14);
                if (count <= 0 || count > 64)
                    return;
                unsigned char* const* list =
                    *reinterpret_cast<unsigned char* const* const*>(
                        globals + 0x18);
                const float scale =
                    *reinterpret_cast<const float*>(kFadeTimeScale);
                float* values = reinterpret_cast<float*>(kFadeValues);
                for (int i = 0; list && i < count; ++i)
                {
                    unsigned char* data = list[i];
                    if (!data || data[0] >= kFadeCount ||
                        !(want & bit(data[0])))
                        continue;
                    const float in =
                        *reinterpret_cast<const float*>(data + 4) * scale;
                    const float hold =
                        *reinterpret_cast<const float*>(data + 8) * scale;
                    // Mid-hold: full alpha, no fade sounds, and a moment
                    // more of it once the panel closes.
                    if (std::isfinite(in) && std::isfinite(hold) && hold > in)
                        *reinterpret_cast<float*>(data + 0x10) =
                            in + 0.5f * (hold - in);
                    values[data[0]] = 1.0f;
                    held |= bit(data[0]);
                }
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
            }
            static unsigned reported = ~0u;
            if (held != reported)
            {
                reported = held;
                char list_text[48];
                fade_list(held, list_text, sizeof(list_text));
                log("hud elements: settings panel open: HUD fade groups %s "
                    "held in view", list_text);
            }
        }

        // --- the wrapper -------------------------------------------------

        int g_element = kNone;          // the element being drawn
        float g_dx = 0.0f, g_dy = 0.0f;
        bool g_in_hud = false;          // inside the player HUD root's draw
        bool g_root_seen = false;
        unsigned g_root_missed = 0;     // top-level draws without it
        unsigned g_stable_draws = 0;    // root draws since the tree changed
        bool g_summarised = false, g_dumped = false;
        unsigned g_summaries = 0, g_dumps = 0;
        unsigned g_moved[kElementCount] = {};

        PFN_Draw original_for(const unsigned char* item, unsigned type)
        {
            const uintptr_t table =
                *reinterpret_cast<const uintptr_t*>(item + 4);
            if (type >= 1 && type <= 15 && kTables[type - 1].table == table)
                return g_original[type - 1];
            for (unsigned i = 0; i < 15; ++i)
                if (kTables[i].table == table)
                    return g_original[i];
            return nullptr;
        }

        const void* player_hud_root()
        {
            __try
            {
                int count = *reinterpret_cast<const int*>(kHUDScreenCount);
                if (count > 3)
                    count = 3;
                const unsigned char* const* screens =
                    reinterpret_cast<const unsigned char* const*>(kHUDScreens);
                for (int i = 0; i < count; ++i)
                    if (screens[i] &&
                        *reinterpret_cast<const short*>(screens[i]) ==
                            kPlayerHUD)
                        return *reinterpret_cast<const void* const*>(
                            screens[i] + 0xC);
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
            }
            return nullptr;
        }

        int draw_element(int element, void* item, void* info,
                         PFN_Draw original)
        {
            const Settings s = settings_for(element);
            if (s.hide)
                return 0;
            if (s.dx == 0.0f && s.dy == 0.0f)
                return original(item, info);
            g_element = element;
            g_dx = s.dx;
            g_dy = s.dy;
            const int result = original(item, info);
            g_element = kNone;
            return result;
        }

        // Once a frame: the tree (rebuilt if it changed), the panel's hold,
        // then the HUD itself.
        int draw_hud_root(void* item, void* info, PFN_Draw original)
        {
            const bool debug = config().hud_debug;
            if (debug)
                debug_screens();
            if (walk(g_scratch, static_cast<const unsigned char*>(item)))
            {
                if (g_scratch.signature != g_tree.signature ||
                    g_scratch.count != g_tree.count)
                {
                    memcpy(&g_tree, &g_scratch, sizeof(g_tree));
                    build_index();
                    g_stable_draws = 0;
                    g_summarised = g_dumped = false;
                }
                classify(g_tree);
            }
            else
            {
                g_tree.count = 0;
                memset(g_index, 0, sizeof(g_index));
            }
            if (g_stable_draws < 0x7FFFFFFF)
                ++g_stable_draws;
            // A second in, the drawn anchors are known: report the pick.
            if (g_stable_draws == 90 && g_tree.count)
            {
                if (!g_summarised && g_summaries < 8)
                {
                    g_summarised = true;
                    ++g_summaries;
                    summary(g_tree, "found");
                }
            }
            if (debug && g_stable_draws >= 90 && g_tree.count && !g_dumped &&
                g_dumps < 4)
            {
                g_dumped = true;
                ++g_dumps;
                dump_tree(g_tree);
                log("hud elements: last frame moved %u item wheel and %u "
                    "ammo parts", g_moved[kItems], g_moved[kAmmo]);
            }
            if (overlay_open())
                hold_hud_visible();

            g_moved[kItems] = g_moved[kAmmo] = 0;
            const Node* self = find_node(item);
            g_in_hud = true;
            const int result = self && self->role == kRoot
                ? draw_element(self->element, item, info, original)
                : original(item, info);
            g_in_hud = false;
            return result;
        }

        int __cdecl draw_item(void* item_ptr, void* info_ptr)
        {
            const unsigned char* item =
                static_cast<const unsigned char*>(item_ptr);
            const unsigned type = item_type(item);
            const PFN_Draw original = original_for(item, type);
            if (!original)
                return 0;
            if (g_element != kNone)
                return original(item_ptr, info_ptr);   // inside an element

            if (!g_in_hud)
            {
                if (!config().hud_debug && !overlay_open() &&
                    neutral(settings_for(kItems)) &&
                    neutral(settings_for(kAmmo)))
                    return original(item_ptr, info_ptr);
                const void* root = player_hud_root();
                if (root && root == item_ptr)
                {
                    g_root_seen = true;
                    return draw_hud_root(item_ptr, info_ptr, original);
                }
                if (root && !g_root_seen && ++g_root_missed == 2000)
                    log("hud elements: the player HUD's root group %p has "
                        "not drawn through the item tables; nothing is "
                        "moved", root);
                return original(item_ptr, info_ptr);
            }

            Node* n = find_node(item_ptr);
            if (!n || n->role == kInside)
                return original(item_ptr, info_ptr);
            // Where it lands this frame (the interface keeps a sibling's x).
            const float* info = static_cast<const float*>(info_ptr);
            float x = 0.0f, y = 0.0f;
            resolve_anchor(item, type, info[0], info[1], &x, &y);
            if (std::isfinite(x) && std::isfinite(y))
            {
                n->rx = x;
                n->ry = y;
                n->rvalid = true;
            }
            if (n->role == kRoot || n->role == kJoin)
                return draw_element(n->element, item_ptr, info_ptr, original);
            return original(item_ptr, info_ptr);
        }

        void __fastcall detour_get_trans(void* info, void*, float* x,
                                         float* y, float* z)
        {
            g_get_trans(info, x, y, z);
            if (g_element == kNone || !x || !y)
                return;
            *x += g_dx;
            *y += g_dy;
            ++g_moved[g_element];
        }
    }

    void hud_elements_init()
    {
        void* tramp = nullptr;
        if (!hook_install((void*)kGetTransXYZ, (void*)&detour_get_trans,
                          &tramp, kGetTransSignature,
                          sizeof(kGetTransSignature),
                          "UIDrawInfo::GetTransXYZ (HUD elements)"))
        {
            log("hud elements: off (interface position getter not found)");
            return;
        }
        g_get_trans = reinterpret_cast<PFN_GetXYZ>(tramp);

        // Check every table first: either all are swapped or none.
        for (const TypeTable& t : kTables)
        {
            uintptr_t draw = 0;
            __try
            {
                draw = *reinterpret_cast<const uintptr_t*>(t.table +
                                                           kDrawFnOffset);
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
            }
            if (draw != t.draw)
            {
                log("hud elements: off (the %s draw table at 0x%08X holds "
                    "0x%08X, expected 0x%08X)", t.name, (unsigned)t.table,
                    (unsigned)draw, (unsigned)t.draw);
                return;
            }
        }
        for (unsigned i = 0; i < 15; ++i)
        {
            uintptr_t* slot = reinterpret_cast<uintptr_t*>(kTables[i].table +
                                                           kDrawFnOffset);
            DWORD old = 0;
            if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
            {
                log("hud elements: could not unprotect the %s draw table",
                    kTables[i].name);
                for (unsigned j = 0; j < i; ++j)
                {
                    uintptr_t* back = reinterpret_cast<uintptr_t*>(
                        kTables[j].table + kDrawFnOffset);
                    *back = kTables[j].draw;
                }
                return;
            }
            g_original[i] = reinterpret_cast<PFN_Draw>(kTables[i].draw);
            *slot = reinterpret_cast<uintptr_t>(&draw_item);
            DWORD unused = 0;
            VirtualProtect(slot, sizeof(*slot), old, &unused);
        }
        const Config& c = config();
        log("hud elements: on (15 item draws wrapped); item wheel x=%+.2f "
            "y=%+.2f%s, ammo x=%+.2f y=%+.2f%s%s", c.hud_items_x,
            c.hud_items_y, c.hud_items_hide ? " hidden" : "", c.hud_ammo_x,
            c.hud_ammo_y, c.hud_ammo_hide ? " hidden" : "",
            c.hud_debug ? ", hud_debug on" : "");
    }
}
